# Current status: Windows x64 product built

Windows x64 is the primary target. The actual E3-derived `blam_x64.exe`
has been generated, linked and inspected as AMD64/PE32+. It is 21,219,664
bytes, SHA-256 `17ec240504ca34a8c291598f02fcea8db4bb4ca145abc79a618c829ab283f370`.
The input-bound project contains 113,949 admitted instruction locations and
was generated without partial mode.

**Actual E3 execution on native Windows is not yet verified. E3 is not
claimed playable.** OS startup, a game-generated frame and responsive player
control are distinct milestones. No new roadmap points are awarded.

## Exact implementation and new evidence

Integrated source: `06e7e94343663af907a6a145a8504d0f49abf8c5`.
The new Windows/MSVC driver and profile are combined with the previously
published `7f6064e` runtime, unchanged at the implementation level.

- The actual E3 output was cross-compiled locally on Linux with Windows
  MinGW/GCC, using Win64 ABI helpers. A completed linker and two independent
  PE inspections establish a Windows x64 build, not Windows game execution.
- The combined native Windows/MSVC build route passed run `37167613799`:
  seven driver-test groups and original/recompiled synthetic window/frame
  comparison. Both fixture programs ran on Windows with identical pixels.
- The existing `7f6064e` implementation previously passed 20 native Windows
  MinGW suites in run `37165676428`. Those finite synthetic/runtime tests
  are not a fresh run of the real game.

The driver explicitly selects x64, checks the produced EXEs, retains logs,
protects the original input/existing output, and never launches a guest by
default. See `docs/windows-x64-first.md` and
`verification/windows-x64-build.json` for commands, identities and limitations.

Game-derived outputs remain private. No original executable, assets,
disassembly or generated game source/executable was uploaded to hosted CI
or committed. The original executable remains required as guest data.

## What remains

Actual E3 Windows startup and its first game-generated scene, broader API
and renderer support, audio/input, static TLS, SEH, guest threading and the
interactive game loop are not complete. The bounded DirectDraw bridge now
present is not full DirectDraw/Direct3D or acceptance of the graphics gate.
The old Linux LoadIconA stop remains historical diagnostic-host evidence;
it is not a fresh Windows stop or Windows failure result.

## Historical accepted checkpoints

The accepted planning total remains 50/100, not a measured fraction of all
unknown engineering work or gameplay. Runtime `a9988b3` was validated in run
`36993865298`; windowing `b02db47` in `37162230094`. Their exact evidence is
retained in `verification/runtime-ci.json`, `verification/runtime-local.json`,
`verification/windowing-ci.json` and the earlier foundation/revalidation
records. None is substituted for actual E3 native Windows execution.
