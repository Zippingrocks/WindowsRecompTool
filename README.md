# WinRecomp

A developing static-recompilation tool for **PE32/i386 Windows programs**, with
**native Windows x64** as the first intended game host. Guest registers, pointers,
flags and addresses remain 32-bit; they are not widened into host pointers.

## What exists now

A buildable C++20 CLI with a pinned in-process Zydis decoder, bounded PE32 parser,
import inventory, recursive control-flow discovery, deterministic JSON manifests,
and a fail-closed subset C++ emitter. The emitted blocks compile and execute as
native x64, using the independent header-only guest state/memory runtime.

This is an **experimental foundation**, not a game-booting recompiler. The E3 2000
integration target has been analyzed, and five routines from that executable have
been translated and checked against an independent x86 oracle. That is not a
claim of gameplay, complete function recovery, complete instruction coverage,
or Win32 compatibility. See `docs/status.md` and `docs/roadmap.md`.

## Build

Use CMake 3.20+, a C++20 compiler, and Git. On Windows, use an x64 Visual Studio
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

## Test translated code

Unicorn is a separate optional test dependency, not part of the product runtime:

```sh
python -m pip install unicorn==2.1.2
cmake -S . -B build -DWINRECOMP_CONFORMANCE=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The suite compiles 60 author-created x86 fixtures into a native x64 library and
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
Generated game-derived source, manifests and test images remain under ignored
`build/`. No game files are needed by CI.

## Reference repositories

`research/upstreams.lock.json` and Git submodules pin Zydis, xboxrecomp,
recomp-kit, Remill, pe-parse, LIEF, Wine, ReactOS and Unicorn. Only Zydis links
into the CLI. The other repositories are isolated research/test references,
not an unreviewed merged engine. Fetch selected references on demand:

```sh
python tools/references.py xboxrecomp recomp-kit remill
python tools/references.py --all
```

`--all` includes large repositories. Exact commits and roles are documented;
no updater follows a moving branch silently. See `NOTICE.md` before copying or
linking upstream code. The main repository contains no proprietary game binary,
asset archive, PDB or game-derived generated source.
