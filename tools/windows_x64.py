"""Build a real Windows AMD64 program from a local PE32 input.

No guest execution, uploads, dependency downloads, or silent partial lifting.
Use on Windows with VS 2022 C++/SDK, CMake >=3.21, Python 3 and initialized Zydis.
The output is private input-derived material. This script does not make it playable.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
GENERATOR = 'Visual Studio 17 2022'


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def inspect_pe(path: Path, *, x64: bool) -> dict[str, Any]:
    """Bounded format/architecture gate, not a full Windows-loader validator."""
    data = path.read_bytes()

    def need(offset: int, count: int) -> None:
        if offset < 0 or count < 0 or offset > len(data) - count:
            raise ValueError('Truncated PE image: ' + str(path))

    def u16(offset: int) -> int:
        need(offset, 2)
        return struct.unpack_from('<H', data, offset)[0]

    def u32(offset: int) -> int:
        need(offset, 4)
        return struct.unpack_from('<I', data, offset)[0]

    need(0, 64)
    if data[:2] != b'MZ':
        raise ValueError('Expected a Windows PE image, not ELF or a renamed file')
    pe = u32(0x3c)
    need(pe, 24)
    if pe < 64 or data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('Invalid PE signature/offset')
    machine, count, opt_size, flags = u16(pe + 4), u16(pe + 6), u16(pe + 20), u16(pe + 22)
    opt = pe + 24
    need(opt, opt_size)
    expected = (0x8664, 0x20b) if x64 else (0x14c, 0x10b)
    if opt_size < (112 if x64 else 96) or (machine, u16(opt)) != expected:
        raise ValueError('Expected ' + ('AMD64/PE32+ output' if x64 else 'i386/PE32 input'))
    if not flags & 2 or flags & (0x2000 | 0x1000):
        raise ValueError('Expected an application EXE, not a DLL/system image')
    if x64 and flags & 0x100:
        raise ValueError('AMD64 output incorrectly carries IMAGE_FILE_32BIT_MACHINE')
    if not 1 <= count <= 96 or u16(opt + 68) not in (2, 3):
        raise ValueError('Unsupported section count or Windows subsystem')
    directory_count = u32(opt + (108 if x64 else 92))
    directory_base = 112 if x64 else 96
    if directory_count > (opt_size - directory_base) // 8:
        raise ValueError('Data directory count exceeds optional header')
    if directory_count > 14 and (u32(opt + directory_base + 14 * 8) or
                                 u32(opt + directory_base + 14 * 8 + 4)):
        raise ValueError('Managed CLR images are not native WinRecomp programs')
    table = opt + opt_size
    need(table, count * 40)
    entry, image_size, header_size = u32(opt + 16), u32(opt + 56), u32(opt + 60)
    if not entry or entry >= image_size or not table + count * 40 <= header_size <= len(data):
        raise ValueError('Invalid entry point, image size, or headers')
    backed_entry = False
    for index in range(count):
        section = table + index * 40
        virtual, rva = u32(section + 8), u32(section + 12)
        size, raw, attributes = u32(section + 16), u32(section + 20), u32(section + 36)
        if size:
            need(raw, size)
        if rva + max(virtual, size) > image_size:
            raise ValueError('Section exceeds declared image')
        if rva <= entry < rva + size and attributes & 0x20000000:
            backed_entry = True
    if not backed_entry:
        raise ValueError('Entry point is not file-backed executable code')
    return {'format': 'PE32+' if x64 else 'PE32', 'machine': hex(machine),
            'architecture': 'AMD64' if x64 else 'i386', 'entry_rva': hex(entry),
            'sections': count, 'sha256': hashlib.sha256(data).hexdigest(),
            'size_bytes': len(data), 'validation': 'format and architecture, not execution'}


def profile_seeds(profile: Path | None, input_sha: str) -> list[int]:
    if profile is None:
        return []
    value = json.loads(profile.read_text(encoding='utf-8'))
    if value.get('schema') != 'winrecomp.runtime-profile.v1' or value.get('sha256') != input_sha:
        raise ValueError('Profile schema/input SHA-256 mismatch; no seed addresses applied')
    seeds = value.get('seeds')
    if not isinstance(seeds, list) or any(type(n) is not int or not 0 <= n <= 0xffffffff for n in seeds):
        raise ValueError('Profile seeds must be uint32 addresses')
    return sorted(set(seeds))


def private_output(source: Path, output: Path, protected: list[Path]) -> None:
    if output.exists():
        raise ValueError('Output already exists; choose a fresh directory, nothing overwritten')
    for item in [source, *protected]:
        if item == output or output in item.parents:
            raise ValueError('Output must not contain or replace an input/source path')
    if source in output.parents and output.relative_to(source).parts[0] not in ('local', 'generated'):
        raise ValueError('Inside the repository use ignored local/ or generated/ for private output')
    for path in [source, output, *protected]:
        if any(char in str(path) for char in ('\n', '\r', ';')):
            raise ValueError('Newlines and semicolons are unsupported in CMake paths')


def configure(source: Path, build: Path) -> list[str]:
    return ['cmake', '-S', str(source), '-B', str(build), '-G', GENERATOR,
            '-A', 'x64', '-T', 'host=x64', '-DPython3_EXECUTABLE=' + sys.executable]


def execute(command: list[str], log: Path) -> None:
    print('Running: ' + subprocess.list2cmdline(command), flush=True)
    with log.open('w', encoding='utf-8') as stream:
        stream.write(json.dumps(command) + '\n')
        stream.flush()
        result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, shell=False)
    if result.returncode:
        tail = log.read_text(encoding='utf-8', errors='replace')[-6000:]
        raise RuntimeError(f'Build step failed ({result.returncode}); see {log}\n{tail}')


def build(input_file: Path, output: Path, profile: Path | None, jobs: int = 2) -> dict[str, Any]:
    if os.name != 'nt' or struct.calcsize('P') != 8:
        raise RuntimeError('This route requires native Windows with 64-bit Python; Linux is not a Windows build result')
    if jobs < 1 or jobs > 32:
        raise ValueError('Parallel jobs must be between 1 and 32')
    input_file, output = input_file.resolve(strict=True), output.resolve()
    profile = profile.resolve(strict=True) if profile else None
    private_output(ROOT, output, [input_file, *([profile] if profile else [])])
    input_info = inspect_pe(input_file, x64=False)
    seeds = profile_seeds(profile, input_info['sha256'])
    for required in ('third_party/zydis/CMakeLists.txt', 'third_party/zydis/dependencies/zycore/CMakeLists.txt'):
        if not (ROOT / required).is_file():
            raise RuntimeError('Initialize pinned dependencies first: git submodule update --init --recursive third_party/zydis')
    if not shutil.which('cmake'):
        raise RuntimeError('CMake >= 3.21 must be on PATH')
    output.mkdir(parents=True, exist_ok=False)
    logs = output / 'logs'
    logs.mkdir()
    evidence: dict[str, Any] = {'schema': 'winrecomp.windows-x64-build.v1', 'status': 'building',
        'input': input_info, 'source_commit': None, 'tracked_source_dirty': None,
        'host': 'Windows x64', 'generator': GENERATOR, 'target': 'x64',
        'run_status': 'not_run', 'playability': 'not_assessed', 'seeds': seeds,
        'driver_sha256': sha256(Path(__file__)),
        'profile_sha256': sha256(profile) if profile else None}
    report = output / 'build-report.json'
    try:
        for key, args in [('source_commit', ['rev-parse', 'HEAD']),
                          ('tracked_source_dirty', ['status', '--porcelain', '--untracked-files=no'])]:
            result = subprocess.run(['git', '-C', str(ROOT), *args], capture_output=True, text=True, check=True)
            evidence[key] = result.stdout.strip()
        tool_build = output / 'tool-build'
        execute(configure(ROOT, tool_build) + ['-DBUILD_TESTING=OFF'], logs / '01-tool-configure.log')
        execute(['cmake', '--build', str(tool_build), '--config', 'Release', '--target', 'winrecomp',
                 '--parallel', str(jobs)], logs / '02-tool-build.log')
        tool = tool_build / 'Release/winrecomp.exe'
        evidence['tool'] = inspect_pe(tool, x64=True)
        project = output / 'project'
        command = [str(tool), 'project', str(input_file), str(project)]
        for seed in seeds:
            command += ['--seed', hex(seed)]
        execute(command, logs / '03-translation.log')
        manifest = json.loads((project / 'manifest.json').read_text(encoding='utf-8'))
        if manifest.get('schema') != 'winrecomp.program.v1' or manifest.get('partial') is not False or manifest.get('sha256') != input_info['sha256']:
            raise ValueError('Missing/mismatched complete-translation manifest')
        evidence['translation'] = manifest
        native_build = output / 'native-build'
        execute(configure(project, native_build) + ['-DWINRECOMP_SOURCE=' + str(ROOT)], logs / '04-native-configure.log')
        execute(['cmake', '--build', str(native_build), '--config', 'Release', '--target', 'recompiled_program',
                 '--parallel', str(jobs)], logs / '05-native-build.log')
        binary = native_build / 'Release/recompiled_program.exe'
        evidence['program'] = inspect_pe(binary, x64=True)
        if evidence['program']['sha256'] == input_info['sha256'] or sha256(input_file) != input_info['sha256']:
            raise ValueError('Output equals original or original was modified during build')
        # This copies the verified native binary. It does not relabel the input.
        destination = output / 'blam_x64.exe'
        shutil.copy2(binary, destination)
        if sha256(destination) != evidence['program']['sha256']:
            raise ValueError('Final artifact copy mismatch')
        evidence['output_file'] = str(destination)
        evidence['status'] = 'built_windows_x64_not_executed'
    except Exception as error:
        evidence['status'] = 'failed'
        evidence['error'] = str(error)
        raise
    finally:
        report.write_text(json.dumps(evidence, indent=2) + '\n', encoding='utf-8')
    print('Verified Windows AMD64/PE32+ executable: ' + str(destination))
    print('Compiled only. No original or recompiled guest program was launched. See ' + str(report))
    return evidence


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', required=True, type=Path, help='Local original PE32 executable')
    parser.add_argument('--out', required=True, type=Path, help='Fresh private output directory')
    parser.add_argument('--profile', type=Path, help='SHA-bound runtime seed profile (use the E3 profile for blam.exe)')
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    try:
        build(args.input, args.out, args.profile, args.jobs)
        return 0
    except Exception as error:
        print('Windows x64 build: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
