import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('publishing', Path(__file__).resolve().parents[2] / 'tools/publish_xiao.py')
publishing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publishing)


class PublishingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.artifacts = self.root / 'artifacts'
        self.output = self.root / 'site'
        self.sha = 'a' * 40
        entries = [('nvs', 1, 2, 0x9000, 0x5000), ('otadata', 1, 0, 0xe000, 0x2000),
                   ('app0', 0, 0x10, 0x10000, 0x330000), ('app1', 0, 0x11, 0x340000, 0x330000),
                   ('spiffs', 1, 0x82, 0x670000, 0x180000)]
        self.table = b''.join(struct.pack('<HBBII16sI', 0x50aa, kind, subtype, offset, size,
                                         name.encode(), 0) for name, kind, subtype, offset, size in entries)
        for target in publishing.TARGETS:
            path = self.artifacts / target
            path.mkdir(parents=True)
            (path / 'firmware.bin').write_bytes(b'application')
            (path / 'firmware-merged.bin').write_bytes(b'merged')
            (path / 'partitions.bin').write_bytes(self.table)
            publishing.write_json(path / 'build.json', {
                'target': target, 'commit': self.sha, 'version': 'sq-test',
                'chip': 'ESP32-S3', 'flash_bytes': 0x800000,
                'partitions': publishing.partitions(self.table),
                'sha256': {name: publishing.digest(path / name) for name in
                           ('firmware.bin', 'firmware-merged.bin', 'partitions.bin')},
            })

    def build_metadata(self):
        return self.artifacts / next(iter(publishing.TARGETS)) / 'build.json'

    def test_complete_bundle_produces_six_matching_manifests(self):
        publishing.assemble(self.artifacts, self.sha, self.output)
        for slug, _ in publishing.TARGETS.values():
            directory = self.output / 'firmware' / self.sha / slug
            manifest = json.loads((directory / 'manifest.json').read_text())
            self.assertEqual('ESP32-S3', manifest['builds'][0]['chipFamily'])
            self.assertEqual(0, manifest['builds'][0]['parts'][0]['offset'])
            self.assertTrue((directory / manifest['builds'][0]['parts'][0]['path']).is_file())
            self.assertIn(self.sha, str(directory))
        self.assertEqual(6, len(json.loads((self.output / 'builds.json').read_text())['builds']))

    def test_missing_build_stops_publication(self):
        self.build_metadata().unlink()
        with self.assertRaisesRegex(ValueError, 'All six'):
            publishing.assemble(self.artifacts, self.sha, self.output)
        self.assertFalse(self.output.exists())

    def test_stale_build_stops_publication(self):
        with self.assertRaisesRegex(ValueError, 'stale'):
            publishing.assemble(self.artifacts, 'b' * 40, self.output)

    def test_modified_binary_stops_publication(self):
        (self.build_metadata().parent / 'firmware.bin').write_bytes(b'corrupted')
        with self.assertRaisesRegex(ValueError, 'checksum'):
            publishing.assemble(self.artifacts, self.sha, self.output)

    def test_unexpected_partition_layout_is_rejected(self):
        data = bytearray(self.table)
        struct.pack_into('<I', data, 2 * 32 + 4, 0x20000)
        with self.assertRaisesRegex(ValueError, 'partition layout'):
            publishing.partitions(data)


if __name__ == '__main__':
    unittest.main()
