# Local Windows x64 candidate: E3 reaches its renderer-selection dialog request

**E3 is not demonstrated playable. This candidate is local, not pushed or
native-Windows-CI-approved.** The accepted planning total remains 50/100;
no additional points are awarded for device creation or callback enumeration.

Implementation: `816608ddc83fcc6e9fe36f60bd4c07a0aa9c74bd`, branch
`work/d3d7-startup`, based on retained DLL-startup checkpoint `9165539`.
Remote `main` was rechecked at `28903ea`; no publishing action was available
and direct Git transport failed DNS. Work has been preserved in local Git.

## Actual game result

The E3-derived AMD64/PE32+ program now contains 115,823 admitted instruction
locations, generated without partial mode. The final executable is 21,645,313
bytes, SHA-256 `e23cbbac94df9f4a479743d40235c7838d8a00395032e13397e32741963363e8`.
It was cross-compiled with Windows MinGW GCC 14 and executed under **Wine 10
on Linux**, using a software-rendering host. It is not a native Linux game,
but Wine execution is not native Windows execution either.

Two final runs each reached 20,355,349 executed guest instructions and 637
API dispatches. Each created one window, executed 36 window callbacks,
created four real host Direct3D7 devices and ran 56 texture-format callbacks.
The next explicit stop is `user32.dll!DialogBoxParamA`. Its resource 101 is
statically titled **Choose Rasterizer** and contains 18 controls. The dialog
has not been displayed or answered by WinRecomp.

All 21 enumerated/created guest COM wrappers were retired before this stop.
The device creations are startup probes, not rendered game frames. OpenGL
contexts, buffer swaps, DirectDraw blits and demonstrated player input remain
zero/unachieved. No fake Direct3D device or successful dialog return is used.

## New support and tests

The bridge now covers bounded IDirect3D7/IDirect3DDevice7 identity, construction,
capability and format enumeration; DD7 adapter identification and modern modes;
native primary/offscreen render targets and clippers. Native pointer-bearing
structures and COM/window handles are converted to checked guest layouts.
Callback cancellation, faults, nesting, read-only scratch expiration and native
reference lifetime are tested rather than silently ignored.

- Windows-target C++ tests under Wine: all 17 executables passed.
- New emitted-x86 D3D7 callback test: 32 runs, 64 native-oracle rows passed.
- Linux Release: all 19 CTest suites passed in a completed single invocation.
- Linux GCC ASan/UBSan: all 12 suites passed; not Windows-only code coverage.
- Current integer/flags, x87 and SIMD suites passed 60,672 / 18,240 / 29,952
  comparisons respectively; finite test and undefined-state exclusions remain.
- Original E3 identity/CFG and five routine regressions passed (5,120 comparisons).
- New native Windows/MSVC, Windows ASan and native-i386 tests: not run this pass.

The Wine text-classification tests preserve two recorded-snapshot differences
for U+02C6; direct host API comparisons passed, not historical Windows fidelity.
Earlier interrupted commands were not counted as passing. The final resumed
builds and completed test invocations are recorded with their scope and hashes.

## Next boundary and preservation

The real dialog resource, guest dialog procedure and controls/message lifetime
need implementation. Device7 scene/drawing/texture state methods and many other
Windows/DirectX/audio/input/threading paths also remain unimplemented. Only an
actual game scene with sustained input-driven movement/actions qualifies as
playable; a chooser, successful API call or synthetic triangle does not.

See `d3d7-startup.md` and `verification/d3d7-startup-local.json` for current exact
results. The previous DLL/startup/runtime/windowing and native Windows build
records remain preserved. Earlier successful platform tests are not substituted
for current-source native Windows acceptance. No game binary, asset, disassembly,
generated game source or game-derived executable is committed or uploaded.
