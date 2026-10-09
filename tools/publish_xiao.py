"""Package XIAO Wio builds, a verified Pages site and versioned release downloads."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import zipfile

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


def verified_bundles(artifacts, sha):
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
    return bundles


def assemble(artifacts, sha, output):
    bundles = verified_bundles(artifacts, sha)
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


def release_notes(notes, tag):
    if not re.fullmatch(r'v\d+\.\d+\.\d+-sq[1-9]\d*', tag):
        raise ValueError('Expected a stable Enhanced tag such as v1.17.1-sq3')
    sections = re.split(r'^## ([^\r\n]+)\r?$', notes.read_text(encoding='utf-8'), flags=re.MULTILINE)
    matches = [sections[i + 1].strip() for i in range(1, len(sections), 2)
               if sections[i] == tag]
    if len(matches) != 1 or not matches[0]:
        raise ValueError('Exactly one non-empty release notes section is required for ' + tag)
    return matches[0]


def release(artifacts, sha, tag, notes, output):
    body = release_notes(notes, tag)
    bundles = verified_bundles(artifacts, sha)
    if output.exists() and any(output.iterdir()):
        raise ValueError('Release output directory must be empty')
    output.mkdir(parents=True, exist_ok=True)
    catalog = {'tag': tag, 'commit': sha, 'builds': []}
    for target, (_, title) in TARGETS.items():
        source, metadata = bundles[target]
        name = f'{target}-{tag}.zip'
        info = {'tag': tag, 'commit': sha, 'target': target,
                'firmware_version': metadata['version']}
        readme = f'''# MeshCore Enhanced {tag} - {title}

Hardware: original Seeed XIAO ESP32-S3 + Wio-SX1262 B2B kit, 8 MB flash.
Build target: {target}
Source commit: {sha}
Firmware-reported version: {metadata['version']}

Contents:
- firmware.bin: application only.
- firmware-merged.bin: full installation image, written at offset 0x0.
- partitions.bin: partition table for inspection and compatibility checks.
- build.json: firmware version, commit, partition layout and binary SHA-256 values.
- release.json: release tag and source commit.
- SHA256SUMS.txt: checksums of all other files in this ZIP.

## Updating while preserving settings

The USB installer at https://squishywareofficial.github.io/MeshCore-Enhanced/
installs the latest successful main build, which may be newer than this Release.
Leave Erase data unchecked to preserve settings on a compatible installation.

To install THIS archived version while preserving settings, use firmware.bin
with a tool that can inspect the existing partition table and OTA boot state.
Write only the active application partition (app0 OR app1). Do not assume that
0x10000 is the active slot. Do not erase flash or write partitions.bin, boot
metadata, or firmware-merged.bin when preserving data. If the existing layout or
active slot cannot be verified, stop and use the USB installer instead.

## Fresh installation (erases settings and identity)

Only for an intentional fresh installation, using Python and esptool:

python -m pip install esptool
python -m esptool --chip esp32s3 --port <PORT> erase-flash
python -m esptool --chip esp32s3 --port <PORT> write-flash 0x0 firmware-merged.bin

Replace <PORT> with the device port, such as COM26 or /dev/ttyACM0. Erasing removes
the device identity/key, passwords, contacts and configuration. Reconnect without
holding BOOT after flashing. Never use these fresh-install commands for a
data-preserving update.
'''
        files = {filename: (source / filename).read_bytes() for filename in
                 ('firmware.bin', 'firmware-merged.bin', 'partitions.bin', 'build.json')}
        files['release.json'] = (json.dumps(info, indent=2) + '\n').encode()
        files['README.md'] = readme.encode()
        files['SHA256SUMS.txt'] = ''.join(
            f'{hashlib.sha256(data).hexdigest()}  {filename}\n'
            for filename, data in files.items()).encode()
        with zipfile.ZipFile(output / name, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
            for filename, data in files.items():
                archive.writestr(filename, data)
        catalog['builds'].append({**info, 'file': name, 'sha256': digest(output / name)})
    write_json(output / 'release.json', catalog)
    (output / 'SHA256SUMS.txt').write_text(''.join(
        f"{entry['sha256']}  {entry['file']}\n" for entry in catalog['builds']) +
        f"{digest(output / 'release.json')}  release.json\n", encoding='utf-8')
    (output / 'release-notes.md').write_text(
        body + f'\n\nSource commit: `{sha}`.\n\n'
        'Download the ZIP for your device role. Each ZIP contains the application, '
        'full installation image, partition table, metadata and flashing instructions. '
        '`SHA256SUMS.txt` verifies the six ZIPs and `release.json`.\n\n'
        'The [USB flasher](https://squishywareofficial.github.io/MeshCore-Enhanced/) '
        'continues to follow `main` and may offer a newer build than this Release. '
        'Leave **Erase data** unchecked to preserve settings on compatible devices. '
        'The archived full installation image is for fresh installations; read the '
        'included instructions before using it.\n', encoding='utf-8')


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
    rel = commands.add_parser('release')
    rel.add_argument('--artifacts', type=Path, required=True)
    rel.add_argument('--sha', required=True)
    rel.add_argument('--tag', required=True)
    rel.add_argument('--notes', type=Path, required=True)
    rel.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'package':
        package(args.target, args.sha, args.output)
    elif args.command == 'site':
        assemble(args.artifacts, args.sha, args.output)
    else:
        release(args.artifacts, args.sha, args.tag, args.notes, args.output)
