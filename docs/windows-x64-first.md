# Primary target: E3 PE32 to a native Windows x64 executable

Windows x64 is the product target. Linux remains an analysis/regression host,
not the place to demonstrate native Windows startup. The next actual-game
acceptance result is a compiled **AMD64/PE32+ blam_x64.exe**, then its execution
on Windows. Further generic discovery/research is secondary unless it blocks
that exact input and target. Planning percentages do not replace these results.

## One build command on Windows

Prerequisites: 64-bit Python 3, CMake 3.21+, Git, Visual Studio 2022 (or its Build
Tools) with Desktop development with C++, an installed Windows SDK, and the
pinned Zydis/Zycore source. The driver explicitly selects VS 2022, x64 target
and x64 host tools. It does not require a separately initialized developer shell.

```powershell
git submodule update --init --recursive third_party/zydis
py -3 tools/windows_x64.py --input "D:\E3 2000\blam.exe" --profile examples/e3_2000/windows-x64.json --out local/windows-e3
```

The input must match the E3 profile SHA-256. The output directory must be new.
The script builds the analyzer, generates input-bound C++ without partial mode,
builds the generated program with the actual Windows compiler/SDK and inspects
both the analyzer and generated executable. Merely naming an ELF or a PE32 file
`.exe` cannot pass. A native application EXE must have machine 0x8664, optional
header magic 0x20B, valid file-backed entry code and a Windows subsystem. This
bounded inspection is an architecture check, not proof the OS will load/run it.

Successful output:

```text
local/windows-e3/blam_x64.exe
local/windows-e3/build-report.json
local/windows-e3/logs/
local/windows-e3/project/
local/windows-e3/native-build/
```

`blam_x64.exe` is copied from the validated newly compiled artifact; the original
x86 executable is never relabeled, patched or launched by this build driver.
Build failures retain their stage logs and a failed report. Successful build
reports say `built_windows_x64_not_executed`, `run_status: not_run` and
`playability: not_assessed`. No success text claims that the game works.

The generated program still needs the original SHA-bound executable as data,
the original assets, and the MSVC runtime required by the compiler. The old
32-bit guest memory layouts remain intentional; the program running them is
native x64. This is not a recovered original C++ source project.

## Separate, explicit execution on Windows

Only after compilation and inspection, a private trusted-input run can use:

```powershell
.\local\windows-e3\blam_x64.exe "D:\E3 2000\blam.exe" --root "D:\E3 2000" --report .\local\windows-e3\process.json --budget 100000000
```

This command does not enable guest filesystem writes. Add `--allow-write` only
when intentionally permitting the game to write within its data root. Keep
assets and generated game code local; do not upload them to Git or hosted CI.
The runtime is not an operating-system security sandbox.

Record Windows startup, the first **E3-generated** scene and player control as
separate checkpoints. A successful synthetic program, tool build, ELF run,
triangle, or expected unsupported API stop does not establish E3 playability.

## Exact implementation scope

This route is built on published `f093d0c` / implementation `b02db47`. The earlier
reported local-only discovery commit `00dbad0` is not present in the restored
workspace and is not secretly treated as included or tested. The Windows E3
profile carries the verified static window-procedure candidate as a metadata
seed so the published implementation can admit it without that optional pass.
The seed is not a guarantee that its downstream message paths are complete.

The new Windows CI job runs this same build driver with a wholly author-written
PE32 fixture, then executes original and recompiled programs on Windows and
compares their framebuffers and semantic callback records. Its artifact contains
only that synthetic executable and the WinRecomp tool, never E3. Actual-game
build/startup results still require their own private Windows evidence.

## Specifications

- https://cmake.org/cmake/help/latest/generator/Visual%20Studio%2017%202022.html
- https://learn.microsoft.com/en-us/windows/win32/debug/pe-format
