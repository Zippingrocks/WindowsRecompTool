"""Windows-first build/format regressions; --integration uses only our own PE32."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import windows_x64 as w


def image(x64=True):
    data = bytearray(1024)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    opt_size = 240 if x64 else 224
    struct.pack_into('<HHIIIHH', data, 0x84, 0x8664 if x64 else 0x14c, 1, 0, 0, 0, opt_size, 2)
    struct.pack_into('<H', data, 0x98, 0x20b if x64 else 0x10b)
    struct.pack_into('<I', data, 0x98 + 16, 0x1000)
    struct.pack_into('<I', data, 0x98 + 56, 0x2000)
    struct.pack_into('<I', data, 0x98 + 60, 512)
    struct.pack_into('<H', data, 0x98 + 68, 3)
    struct.pack_into('<I', data, 0x98 + (108 if x64 else 92), 16)
    s = 0x98 + opt_size
    data[s:s+8] = b'.text\0\0\0'
    struct.pack_into('<IIII', data, s + 8, 512, 0x1000, 512, 512)
    struct.pack_into('<I', data, s + 36, 0x60000020)
    data[512] = 0xc3
    return data


class DriverTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.path = self.root / 'input.exe'

    def tearDown(self):
        self.temp.cleanup()

    def test_machine_format_pair(self):
        for x64 in (False, True):
            self.path.write_bytes(image(x64))
            self.assertEqual(w.inspect_pe(self.path, x64=x64)['architecture'], 'AMD64' if x64 else 'i386')
            with self.assertRaises(ValueError):
                w.inspect_pe(self.path, x64=not x64)

    def test_rejects_wrong_platform_and_header_lies(self):
        for offset, fmt, value in [(0x84, '<H', 0xaa64), (0x98, '<H', 0x10b),
            (0x96, '<H', 0x2002), (0x96, '<H', 0x1002), (0x96, '<H', 0x102),
            (0x96, '<H', 0), (0x86, '<H', 0), (0x86, '<H', 97),
            (0x98+16, '<I', 0), (0x98+16, '<I', 0x1800),
            (0x98+60, '<I', 1), (0x98+68, '<H', 1),
            (0x98+108, '<I', 17), (0x98+112+14*8, '<I', 0x1000),
            (0x98+240+36, '<I', 0x40000040), (0x98+240+20, '<I', 999),
            (0x98+56, '<I', 0x1001), (0x94, '<H', 64)]:
            with self.subTest(offset=offset, value=value):
                raw = image()
                struct.pack_into(fmt, raw, offset, value)
                self.path.write_bytes(raw)
                with self.assertRaises(ValueError):
                    w.inspect_pe(self.path, x64=True)

    def test_truncated_and_non_pe_rejected(self):
        for raw in (b'', b'MZ', b'\x7fELF' + bytes(1000), image()[:70], image()[:1023]):
            self.path.write_bytes(raw)
            with self.assertRaises(ValueError):
                w.inspect_pe(self.path, x64=True)

    def test_profile_is_sha_bound_and_deduplicated(self):
        profile = self.root / 'profile.json'
        def write(seeds, sha='a'*64, schema='winrecomp.runtime-profile.v1'):
            profile.write_text(json.dumps({'schema': schema, 'sha256': sha, 'seeds': seeds}))
        write([8, 1, 8])
        self.assertEqual(w.profile_seeds(profile, 'a'*64), [1, 8])
        for seeds, sha, schema in [([1], 'b'*64, 'winrecomp.runtime-profile.v1'),
            ([1], 'a'*64, 'invalid'), ([True], 'a'*64, 'winrecomp.runtime-profile.v1'),
            ([-1], 'a'*64, 'winrecomp.runtime-profile.v1'),
            ([0x100000000], 'a'*64, 'winrecomp.runtime-profile.v1'),
            ('1', 'a'*64, 'winrecomp.runtime-profile.v1')]:
            write(seeds, sha, schema)
            with self.assertRaises(ValueError):
                w.profile_seeds(profile, 'a'*64)

    def test_private_output_guards(self):
        source = self.root / 'source'
        source.mkdir()
        protected = source / 'input.exe'
        for output in (source, source/'tracked-output', self.root, source/'input.exe'):
            with self.assertRaises(ValueError):
                w.private_output(source, output, [protected])
        w.private_output(source, source/'local'/'new build', [protected])
        w.private_output(source, self.root/'outside build', [protected])
        for output in (source/'local'/'bad;path', source/'generated'/'bad\npath'):
            with self.assertRaises(ValueError):
                w.private_output(source, output, [protected])

    def test_explicit_windows_x64_configuration(self):
        command = w.configure(Path('source with spaces'), Path('build with spaces'))
        self.assertIn('source with spaces', command)
        self.assertEqual(command[command.index('-A')+1], 'x64')
        self.assertEqual(command[command.index('-T')+1], 'host=x64')
        self.assertIn('Visual Studio 17 2022', command)

    def test_linux_is_not_reported_as_windows(self):
        with patch.object(w, 'os') as fake_os:
            fake_os.name = 'posix'
            with self.assertRaisesRegex(RuntimeError, 'native Windows'):
                w.build(self.path, self.root/'out', None)
        self.assertFalse((self.root/'out').exists())


def integration(out: Path):
    if sys.platform != 'win32':
        raise RuntimeError('The integration requires actual Windows; unavailable is not a pass')
    import window_program as fixture
    raw, procedure, _ = fixture.fixture()
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    original = out / 'synthetic_original.exe'
    original.write_bytes(raw)
    profile = out / 'synthetic_profile.json'
    profile.write_text(json.dumps({'schema': 'winrecomp.runtime-profile.v1',
                                  'sha256': w.sha256(original), 'seeds': [procedure]}))
    # The tested user-facing driver only builds. Execution is this test's choice.
    subprocess.run([sys.executable, str(ROOT/'tools/windows_x64.py'), '--input', str(original),
                    '--profile', str(profile), '--out', str(out/'product with spaces')], check=True)
    report_path = out / 'product with spaces/build-report.json'
    report = json.loads(report_path.read_text())
    assert report['status'] == 'built_windows_x64_not_executed'
    assert report['run_status'] == 'not_run' and report['playability'] == 'not_assessed'
    executable = Path(report['output_file'])
    assert report['program']['architecture'] == 'AMD64'
    assert report['program']['format'] == 'PE32+'
    results = []
    for label, program in [('original', original), ('generated', executable)]:
        directory = out / label
        directory.mkdir()
        command = [str(program)]
        if label == 'generated':
            command += [str(original), '--root', str(directory), '--allow-write', '--report', str(directory/'process.json')]
        result = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=90)
        if result.returncode:
            raise RuntimeError(f'{label} fixture failed: {result.returncode}\n{result.stdout}\n{result.stderr}')
        pixels, record = fixture.inspect_output(directory)
        results.append((pixels, record, (directory/'packed.rgba').read_bytes()))
    assert results[0][0] == results[1][0], 'original PE32 and native x64 framebuffer differs'
    assert results[0][1][:4]+results[0][1][5:] == results[1][1][:4]+results[1][1][5:]
    assert results[0][2] == results[1][2], 'aligned readback differs'
    # Re-running may not overwrite a previously validated tree or original.
    repeat = subprocess.run([sys.executable, str(ROOT/'tools/windows_x64.py'), '--input', str(original),
                             '--out', str(out/'product with spaces')], capture_output=True, text=True)
    assert repeat.returncode != 0 and 'already exists' in repeat.stderr
    assert w.sha256(original) == report['input']['sha256']
    evidence = {'schema': 'winrecomp.windows-x64-driver-test.v1', 'status': 'passed',
                'build_driver': report, 'original_PE32_run': 'passed', 'generated_PE32plus_run': 'passed',
                'identical_framebuffers': True, 'framebuffer_sha256': hashlib.sha256(results[0][0]).hexdigest(),
                'existing_output_preserved': True, 'input_preserved': True,
                'scope': 'Synthetic author-written PE32, not E3. Build-only and run results are separate.'}
    (out/'acceptance.json').write_text(json.dumps(evidence, indent=2)+'\n')
    print('Native Windows driver -> verified AMD64 EXE -> identical original/generated synthetic frame: passed')


if __name__ == '__main__':
    if '--integration' in sys.argv:
        parser = argparse.ArgumentParser()
        parser.add_argument('--integration', type=Path, required=True)
        integration(parser.parse_args().integration)
    else:
        unittest.main()
