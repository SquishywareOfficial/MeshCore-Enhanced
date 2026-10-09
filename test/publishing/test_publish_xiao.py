import importlib.util
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location('publishing', Path(__file__).resolve().parents[2] / 'tools/publish_xiao.py')
publishing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publishing)


class PublishingFixture(unittest.TestCase):
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


class PublishingTests(PublishingFixture):
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
        self.assertTrue((self.output / 'flash-core.js').is_file())
        self.assertIn('Erase data', (self.output / 'index.html').read_text())

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


class ReleaseTests(PublishingFixture):
    def setUp(self):
        super().setUp()
        self.output = self.root / 'release'
        self.tag = 'v1.17.1-sq3'
        self.notes = self.root / 'RELEASE_NOTES.md'
        self.notes.write_text('# Notes\n\n## v1.17.1-sq4\nFuture version\n\n'
                              '## v1.17.1-sq3\nBattery and DHT11 support.\n\n'
                              '### Details\nAll six roles.\n\n'
                              '## v1.17.1-sq2\nPrevious version\n', encoding='utf-8')

    def make_release(self):
        publishing.release(self.artifacts, self.sha, self.tag, self.notes, self.output)

    def test_release_contains_exactly_six_verified_archives(self):
        self.make_release()
        catalog = json.loads((self.output / 'release.json').read_text())
        self.assertEqual(self.tag, catalog['tag'])
        self.assertEqual(self.sha, catalog['commit'])
        self.assertEqual(set(publishing.TARGETS), {b['target'] for b in catalog['builds']})
        self.assertEqual(6, len(list(self.output.glob('*.zip'))))
        for entry in catalog['builds']:
            archive_path = self.output / entry['file']
            self.assertEqual(publishing.digest(archive_path), entry['sha256'])
            with zipfile.ZipFile(archive_path) as archive:
                self.assertEqual({'firmware.bin', 'firmware-merged.bin', 'partitions.bin',
                                  'build.json', 'release.json', 'README.md', 'SHA256SUMS.txt'},
                                 set(archive.namelist()))
                for line in archive.read('SHA256SUMS.txt').decode().splitlines():
                    checksum, filename = line.split('  ', 1)
                    self.assertEqual(checksum, hashlib.sha256(archive.read(filename)).hexdigest())
                meta = json.loads(archive.read('release.json'))
                self.assertEqual(entry['target'], meta['target'])
                self.assertEqual(self.tag, meta['tag'])
                self.assertEqual(self.sha, meta['commit'])
                # Existing compact firmware versions are kept, not replaced by the tag.
                self.assertEqual('sq-test', meta['firmware_version'])
                self.assertEqual(b'application', archive.read('firmware.bin'))
                instructions = archive.read('README.md').decode()
                self.assertIn('app0 OR app1', instructions)
                self.assertIn('erases settings and identity', instructions)
        for line in (self.output / 'SHA256SUMS.txt').read_text().splitlines():
            checksum, filename = line.split('  ', 1)
            self.assertEqual(checksum, publishing.digest(self.output / filename))
        notes = (self.output / 'release-notes.md').read_text()
        self.assertIn('Battery and DHT11 support.', notes)
        self.assertIn('### Details', notes)
        self.assertNotIn('Previous version', notes)
        self.assertNotIn('Future version', notes)
        self.assertIn(self.sha, notes)

    def test_incomplete_or_stale_or_corrupt_release_is_rejected_before_output(self):
        with self.assertRaisesRegex(ValueError, 'stale'):
            publishing.release(self.artifacts, 'b' * 40, self.tag, self.notes, self.output)
        self.assertFalse(self.output.exists())
        binary = self.build_metadata().parent / 'firmware.bin'
        binary.write_bytes(b'corrupt')
        with self.assertRaisesRegex(ValueError, 'checksum'):
            self.make_release()
        self.assertFalse(self.output.exists())
        self.build_metadata().unlink()
        with self.assertRaisesRegex(ValueError, 'All six'):
            self.make_release()
        self.assertFalse(self.output.exists())

    def test_missing_empty_or_duplicate_notes_are_rejected(self):
        for content in ('## v1.17.1-sq2\nOther version\n',
                        '## v1.17.1-sq3\n\n## v1.17.1-sq2\nOther version\n',
                        '## v1.17.1-sq3\nFirst\n## v1.17.1-sq3\nDuplicate\n'):
            with self.subTest(content=content):
                self.notes.write_text(content, encoding='utf-8')
                with self.assertRaisesRegex(ValueError, 'Exactly one'):
                    self.make_release()
                self.assertFalse(self.output.exists())

    def test_invalid_or_upstream_or_prerelease_tags_are_rejected(self):
        for tag in ('../escape', 'repeater-v1.17.1', 'v1.17.1-sq3-beta', 'v1.17.1-sq',
                    'v1.17.1-sq0', 'v1.17.1-sq3\n'):
            with self.subTest(tag=tag), self.assertRaisesRegex(ValueError, 'stable Enhanced tag'):
                publishing.release(self.artifacts, self.sha, tag, self.notes, self.output)
        self.assertFalse(self.output.exists())

    def test_old_outputs_are_rejected_without_overwriting(self):
        self.output.mkdir()
        old = self.output / 'old.zip'
        old.write_bytes(b'old release')
        with self.assertRaisesRegex(ValueError, 'must be empty'):
            self.make_release()
        self.assertEqual(b'old release', old.read_bytes())

    def test_notes_with_windows_line_endings(self):
        self.notes.write_bytes(b'## v1.17.1-sq3\r\nRelease notes.\r\n')
        self.assertEqual('Release notes.', publishing.release_notes(self.notes, self.tag))


if __name__ == '__main__':
    unittest.main()
