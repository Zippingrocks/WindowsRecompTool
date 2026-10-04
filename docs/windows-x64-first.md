# Windows x64 first

The primary product is a native Windows AMD64 executable generated from the
original PE32/i386 input. Linux is a development host, not a substitute for
Windows execution. Build, OS startup, a game scene and player control are
separate acceptance milestones.

## Actual E3 build completed

The private `blam_x64.exe` is 21,219,664 bytes, AMD64/PE32+, SHA-256
`17ec240504ca34a8c291598f02fcea8db4bb4ca145abc79a618c829ab283f370`.
It was generated from the SHA-bound E3 input with 113,949 admitted instruction
locations and no partial mode, then cross-compiled on Linux using Windows
MinGW/GCC 14 POSIX, `-O1 -DNDEBUG`, and static GCC/C++ runtime linking.
The Win64 ABI assembly helpers were used, not Linux SysV helpers. Both the
format checker and MinGW objdump identify a Windows x64 executable.

Its direct DLL imports are DDRAW, GDI32, KERNEL32, MSVCRT, OPENGL32 and USER32.
No separate MinGW runtime DLL appears in that import table. The original
executable is still required as SHA-matched guest data, together with game
assets. Guest pointers/registers deliberately retain their original 32-bit
layout; host execution is x64. This is not recovered original C++ source.

**This actual E3 EXE has not been run on native Windows in this checkpoint.**
It is not a gameplay result. More code locations, successful linking and valid
headers do not establish that every path, import or game behavior works.

## Native Windows/MSVC build route

Use 64-bit Python 3, Git, CMake 3.21+, Visual Studio 2022 or its Build Tools
with Desktop development with C++ and a Windows SDK. The driver selects
`Visual Studio 17 2022`, `-A x64` and `-T host=x64` explicitly.

```powershell
git submodule update --init --recursive third_party/zydis
py -3 tools/windows_x64.py --input "D:\E3 2000\blam.exe" --profile examples/e3_2000/windows-x64.json --out local/windows-e3
```

Use a fresh output directory. The driver builds and architecture-checks the
tool, generates C++ without partial mode, builds the generated program and
copies the verified output to `local/windows-e3/blam_x64.exe`. The original
is not modified or launched. Stage logs and `build-report.json` are retained.
The report explicitly says `built_windows_x64_not_executed`, `run_status:
not_run` and `playability: not_assessed`. It rejects ELF, x86, ARM64, CLR and
DLL outputs rather than trusting an EXE filename.

A later, explicitly chosen private Windows run can use:

```powershell
.\local\windows-e3\blam_x64.exe "D:\E3 2000\blam.exe" --root "D:\E3 2000" --report .\local\windows-e3\process.json --budget 100000000
```

Writes remain disabled unless `--allow-write` is intentionally supplied.
The runtime is not an OS security sandbox. Do not commit or upload game
inputs, assets, generated source or the game-derived executable.

## Source and verification boundaries

Integrated source: `06e7e94343663af907a6a145a8504d0f49abf8c5`.
The new driver was first tested at `58ae1f3`. It was combined with the existing
`7f6064e` runtime, which already passed 20 native Windows MinGW suites.
Runtime/core/build/test files remain byte-identical to that existing branch;
this pass did not invent a new MinGW backend. Its bounded DirectDraw bridge
is not a full graphics/audio/input implementation.

Native Windows/MSVC run `37167613799` passed the combined driver tests and
executed the authored original PE32 and generated x64 fixture with identical
framebuffers. This synthetic result is not execution of the actual E3 EXE.
See `verification/windows-x64-build.json` for exact identities and scopes.

The previous local-only discovery commit `00dbad0` was absent from this
restored workspace and is not assumed included. The Windows E3 profile
instead supplies its rechecked static window-procedure candidate as a
SHA-bound seed, without claiming all indirect targets have been recovered.

## Secondary cross-build reproduction

The completed private artifact used the Linux analyzer to generate the E3
project, then a Windows-target MinGW toolchain. The toolchain must set
CMAKE_SYSTEM_NAME=Windows, CMAKE_SYSTEM_PROCESSOR=AMD64 and the matching
x86_64-w64-mingw32 C/C++/ASM/resource compilers. Do not compile the generated
project with the Linux native compiler and merely rename its output.

```sh
cmake -S . -B build-host -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build-host --target winrecomp --parallel 2
python tools/create_project.py --tool build-host/winrecomp --exe local/e3/blam.exe --profile examples/e3_2000/windows-x64.json --out local/e3-windows-project
cmake -S local/e3-windows-project -B local/e3-windows-build -G Ninja -DCMAKE_TOOLCHAIN_FILE=/absolute/path/to/mingw64.cmake -DWINRECOMP_SOURCE=/absolute/path/to/WinRecomp -DCMAKE_BUILD_TYPE=Release '-DCMAKE_CXX_FLAGS_RELEASE=-O1 -DNDEBUG' '-DCMAKE_EXE_LINKER_FLAGS=-static -static-libgcc -static-libstdc++'
cmake --build local/e3-windows-build --target recompiled_program --parallel 4
```

Inspect `local/e3-windows-build/recompiled_program.exe` before copying it to
`blam_x64.exe`. Compiler versions, paths and PE timestamps can change the
binary hash: the recorded hash identifies this build, not a promise of
bit-for-bit reproducibility. Native Windows/MSVC remains the primary route.

Specifications:
- https://cmake.org/cmake/help/latest/generator/Visual%20Studio%2017%202022.html
- https://learn.microsoft.com/en-us/windows/win32/debug/pe-format
