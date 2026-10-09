"""Package XIAO Wio builds and assemble a self-contained, verified Pages site."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

TARGETS = {
    'Xiao_S3_WIO_repeater': ('repeater', 'Repeater'),
    'Xiao_S3_WIO_room_server': ('room', 'Chatroom'),
    'Xiao_S3_WIO_companion_radio_ble': ('ble', 'Companion Bluetooth'),
    'Xiao_S3_WIO_companion_radio_usb': ('usb', 'Companion USB'),
    'Xiao_S3_WIO_companion_radio_serial': ('serial', 'Companion UART'),
    'Xiao_S3_WIO_companion_radio_wifi': ('wifi', 'Companion Wi-Fi'),
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def partitions(data):
    result = []
    for pos in range(0, len(data) - 31, 32):
        magic, kind, subtype, offset, size, label, flags = struct.unpack_from('<HBBII16sI', data, pos)
        if magic != 0x50AA:
            break
        result.append({'name': label.split(b'\0')[0].decode(), 'type': kind,
                       'subtype': subtype, 'offset': offset, 'size': size})
    by_name = {p['name']: p for p in result}
    if (by_name.get('app0', {}).get('offset') != 0x10000 or
            by_name.get('app0', {}).get('size') != 0x330000 or
            by_name.get('app1', {}).get('offset') != 0x340000 or
            by_name.get('spiffs', {}).get('offset') != 0x670000):
        raise ValueError('Unexpected partition layout: review before offering browser installation')
    return result


def package(target, sha, output):
    if target not in TARGETS:
        raise ValueError('Unsupported target')
    build = Path('.pio/build') / target
    app = build / 'firmware.bin'
    layout = partitions((build / 'partitions.bin').read_bytes())
    version = 'v1.17.1-sq-' + subprocess.check_output(
        ['git', 'rev-parse', '--short', sha], text=True).strip()
    if len(version) > 19 or version.encode() not in app.read_bytes():
        raise ValueError('Firmware does not contain the expected compact commit version')
    if app.stat().st_size > 0x330000:
        raise ValueError('Application exceeds its partition')
    output.mkdir(parents=True, exist_ok=True)
    shutil.copy2(app, output / 'firmware.bin')
    shutil.copy2(build / 'partitions.bin', output / 'partitions.bin')
    core = Path(os.environ.get('PLATFORMIO_CORE_DIR', Path.home() / '.platformio'))
    # ESP Web Tools cannot patch flash mode while writing. Use the documented
    # DIO merged-image header for qio_opi boards, retaining the normal application.
    subprocess.run([
        sys.executable, str(core / 'packages/tool-esptoolpy/esptool.py'),
        '--chip', 'esp32s3', 'merge_bin', '-o', str(output / 'firmware-merged.bin'),
        '--flash_mode', 'dio', '--flash_freq', '80m', '--flash_size', '8MB',
        '0x0', str(build / 'bootloader.bin'), '0x8000', str(build / 'partitions.bin'),
        '0xe000', str(core / 'packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin'),
        '0x10000', str(app),
    ], check=True)
    merged = (output / 'firmware-merged.bin').read_bytes()
    if merged[0] != 0xE9 or merged[2] != 2 or merged[0x10000:] != app.read_bytes():
        raise ValueError('Merged image/header/application validation failed')
    metadata = {'target': target, 'commit': sha, 'version': version,
                'built_at': datetime.now(timezone.utc).isoformat(),
                'chip': 'ESP32-S3', 'flash_bytes': 0x800000, 'partitions': layout,
                'sha256': {name: digest(output / name) for name in
                           ('firmware.bin', 'firmware-merged.bin', 'partitions.bin')}}
    write_json(output / 'build.json', metadata)


def assemble(artifacts, sha, output):
    bundles = {}
    for path in artifacts.rglob('build.json'):
        metadata = json.loads(path.read_text(encoding='utf-8'))
        target = metadata['target']
        if target not in TARGETS or target in bundles or metadata['commit'] != sha:
            raise ValueError('Unexpected, duplicate or stale build artifact')
        if metadata['chip'] != 'ESP32-S3' or metadata['flash_bytes'] != 0x800000:
            raise ValueError('Incorrect chip or flash size')
        if set(metadata['sha256']) != {'firmware.bin', 'firmware-merged.bin', 'partitions.bin'}:
            raise ValueError('Incomplete firmware bundle')
        for name, expected in metadata['sha256'].items():
            if digest(path.parent / name) != expected:
                raise ValueError('Firmware checksum mismatch')
        actual_layout = partitions((path.parent / 'partitions.bin').read_bytes())
        if actual_layout != metadata['partitions']:
            raise ValueError('Partition metadata mismatch')
        bundles[target] = (path.parent, metadata)
    if set(bundles) != set(TARGETS):
        raise ValueError('All six successful builds are required before publishing')
    if len({json.dumps(meta['partitions'], sort_keys=True) for _, meta in bundles.values()}) != 1:
        raise ValueError('Inconsistent partition layouts')
    shutil.copytree(Path(__file__).resolve().parent.parent / 'web/flasher', output, dirs_exist_ok=True)
    catalog = {'commit': sha, 'built_at': datetime.now(timezone.utc).isoformat(), 'builds': []}
    for target, (slug, title) in TARGETS.items():
        source, metadata = bundles[target]
        relative = Path('firmware') / sha / slug
        destination = output / relative
        destination.mkdir(parents=True, exist_ok=True)
        for name in ('firmware.bin', 'firmware-merged.bin', 'partitions.bin', 'build.json'):
            shutil.copy2(source / name, destination / name)
        # Legacy/full-install manifest for external ESP Web Tools users only.
        # The page's own installer checks partitions and selects the active slot
        # for a data-preserving application update; it does not use this manifest.
        write_json(destination / 'manifest.json', {
            'name': f'MeshCore Enhanced - {title}', 'version': metadata['version'],
            'new_install_prompt_erase': False, 'new_install_improv_wait_time': 0,
            'builds': [{'chipFamily': 'ESP32-S3', 'improv': False,
                        'parts': [{'path': 'firmware-merged.bin', 'offset': 0}]}],
        })
        catalog['builds'].append({'slug': slug, 'title': title,
                                 'base_url': relative.as_posix(), **metadata})
    write_json(output / 'builds.json', catalog)
    (output / '.nojekyll').touch()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    pack = commands.add_parser('package')
    pack.add_argument('--target', choices=TARGETS, required=True)
    pack.add_argument('--sha', required=True)
    pack.add_argument('--output', type=Path, required=True)
    site = commands.add_parser('site')
    site.add_argument('--artifacts', type=Path, required=True)
    site.add_argument('--sha', required=True)
    site.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'package':
        package(args.target, args.sha, args.output)
    else:
        assemble(args.artifacts, args.sha, args.output)
