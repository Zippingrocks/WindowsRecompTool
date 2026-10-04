# WinRecomp

> Windows x64 first: the development renderer now has native Windows
> original-x86 versus recompiled-x64 acceptance for indexed untextured/textured
> test scenes. **E3 itself is not demonstrated playable.** New source and
> evidence are on development branches, not yet promoted to main.
> Read `docs/status.md` for exact current scope and `docs/windows-quality-gates.md`
> for the reliability and visual-fidelity gates.

A developing static-recompilation tool for **PE32/i386 Windows programs**, with
**native Windows x64** as the first intended game host. Guest registers, pointers,
flags and addresses remain 32-bit; they are not widened into host pointers.

## What exists now

The first seven gates total **50/100 roadmap planning points**. This is a
verified engineering checkpoint for the documented target profile, not a measure
of total unknown work, gameplay completion or universal Windows compatibility.
The exact source, CI and local evidence are recorded in `docs/status.md`.

The 0.2 runtime generates complete CMake/x64 projects for a bounded
PE32 profile, as well as individual translated slices. It has a Zydis-backed
analyzer, guarded switch recovery, broad tested integer execution, 80-bit x87
and SSE/SSE2 helpers, sparse guest memory, fixed-image loading, checked import
marshalling, heap/file/text APIs and compiled guest callbacks.

The Windows host now has a tested, bounded USER32/GDI/WGL backend: native window
creation, compiled window callbacks, checked icon resources and an OpenGL
bootstrap. An author-written original PE32 and its generated x64 version create
real windows and produce identical triangle framebuffers. Windows, Linux and
native Windows GUI AddressSanitizer checks passed. See `docs/windowing.md` and
`verification/windowing-ci.json` for the exact source and acceptance evidence.

The opt-in `--legacy-renderer d3d9` backend implements a bounded D3D7-on-D3D9
path with single-stage textures and WORD-indexed triangle lists. Native Windows
CI compares authored original PE32 and generated x64 output pixel-for-pixel;
depth, presentation and broad state/geometry compatibility remain unfinished.

**E3 is not playable yet.** The last recorded actual E3 startup reached its
Choose Rasterizer dialog request under Wine. That is distinct from both the
older Linux-only LoadIconA stop and the newer native Windows synthetic tests.
The game itself still needs native Windows integration and its own rendered
scene and player-control evidence. The default display-mode mismatch in issue
#2 is also unresolved. No passing component test accepts the whole game.

## Build

Use CMake 3.20+, Python 3, a C++20 compiler, and Git. On Windows, use an x64 Visual Studio
developer shell. Only the decoder is needed for normal builds:

```sh
git submodule update --init --recursive third_party/zydis
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The executable is `build/winrecomp` on a single-configuration Unix generator,
or `build/Release/winrecomp.exe` with Visual Studio. Ninja on Windows places it
at `build/winrecomp.exe`.

```sh
winrecomp analyze input.exe pe.json
winrecomp cfg input.exe cfg.json
winrecomp lift input.exe slice.cpp --entry 0x00401000
```

`lift` emits **C++ source**, not a replacement game EXE. Link emitted source with
`include/winrecomp/runtime.hpp` and provide an initialized guest CPU/memory.
The checked examples in `tests/conformance.py` demonstrate this end to end.
Unsupported instructions, imports or indirect transfers are errors, never no-ops.
An unsupported translation does not overwrite the selected output file.
The CLI also refuses to overwrite its input executable.

## Generate a native project

```sh
winrecomp project input.exe generated/project
cmake -S generated/project -B generated/project/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DWINRECOMP_SOURCE=/absolute/path/to/WinRecomp
cmake --build generated/project/build --parallel 2
```

Run `recompiled_program` (or `.exe` on Windows) with the original `input.exe`
and `--root /path/to/data`. The SHA-256 must match. Writes are disabled unless
`--allow-write` is explicitly supplied. `--report report.json` preserves the
actual stop/failure, and `--budget N` bounds execution. No interpreter fallback
is used. Unsupported imports/instructions stop, rather than returning dummy success.

For the private E3 integration, first apply the SHA-bound observed seed profile:

```sh
python tools/create_project.py --tool build/winrecomp --exe local/e3_2000/blam.exe --profile examples/e3_2000/runtime.json --out generated/e3
```

Supply the original assets locally. Do not commit generated game source, images,
or data. A generated project is a development tool, not a redistributable game.

## Test translated code

Unicorn is a separate optional test dependency, not part of the product runtime:

```sh
python -m pip install unicorn==2.1.2
cmake -S . -B build -DWINRECOMP_CONFORMANCE=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The integer suite compiles 237 author-created x86 fixtures into a native x64 library and
checks 256 states per fixture against Unicorn, including registers, EIP, defined
status flags, and the complete 64 KiB test stack. Windows CI builds the generated
code with MSVC; Linux CI additionally runs an actual i386 hardware oracle.
The hardware runner returns **77 (unsupported)**, not success, when the kernel
cannot execute ELF32. It does not accept game binaries.

## E3 2000 integration

Keep the user-owned executable under ignored `local/`, never in Git. The contract
is tied to one exact SHA-256, so addresses cannot silently apply to another build:

```sh
python tools/validate_target.py --tool build/winrecomp --exe local/e3_2000/blam.exe --contract examples/e3_2000/target.json --out build/e3 --conformance
```

Use the appropriate Windows executable path and `--cxx cl` in a developer shell.
The real-input floating-point corpus can also be reproduced locally on Linux:

```sh
python tests/floating.py --tool build/winrecomp --native-lib build/libwinrecomp_native.a --target local/e3_2000/blam.exe --contract examples/e3_2000/target.json --out local/e3_fp --vectors 384
```

This selects bounded original x87 sequences, not entire game functions. Generated
game-derived source, manifests and test images remain under ignored `build/` or
`local/`. No game files are needed by CI. Historical actual-game startup under Wine and real-input FP checks on Linux
remain separately labeled. Windows CI compares authored programs and fixtures;
those checks do not substitute for native Windows E3 execution.

## Reference repositories

`research/upstreams.lock.json` and Git submodules pin Zydis, xboxrecomp,
recomp-kit, Remill, pe-parse, LIEF, Wine, ReactOS and Unicorn. Only Zydis is an external source dependency of the CLI. Native floating-point
helpers are generated from WinRecomp-owned scripts at build time. The other repositories are isolated research/test references,
not an unreviewed merged engine. Fetch selected references on demand:

```sh
python tools/references.py xboxrecomp recomp-kit remill
python tools/references.py --all
```

`--all` includes large repositories. Exact commits and roles are documented;
no updater follows a moving branch silently. See `NOTICE.md` before copying or
linking upstream code. The main repository contains no proprietary game binary,
asset archive, PDB or game-derived generated source.
