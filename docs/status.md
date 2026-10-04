# Current development status: native Windows modal dialogs verified

**E3 is not demonstrated playable.** Windows x64 remains the primary target.
The saved source was recovered and the modal-dialog defects were repaired,
pushed and independently tested. No new roadmap points are awarded; the earlier
50/100 planning checkpoint is not a measured percentage of total game work.

## Latest source and acceptance

Implementation: `b9115719a2f9be2695880532e8e64865217340ec`.
Source tree: `4d2b7bd6a0cfcf09c3977a53bba083dfabfea8b1`.
Development branch: `work/dialog-fixes`.
CI run: `37185303944`; all three jobs completed successfully.

| Scope | Verified result |
|---|---|
| Native Windows/MSVC selected modal/GUI tests | 8/8 suites passed |
| Native Windows AddressSanitizer | 5/5 C++ boundary suites passed |
| Linux/GCC applicable regressions | 20/20 suites passed |
| Linux native i386 oracle | 15,168 comparisons, 237 authored cases |
| Additional local Linux Release | 20 suites passed in completed batches |

The native Windows comparison executes an authored original PE32 program and
its recompiled x64 version. Both create/end two modal dialogs and produce the
same 32-byte semantic output record. The generated run performs 42 compiled
dialog callbacks; UI cue state, list/check controls, initialization, result
marshalling and handle lifetimes are checked. This is not E3 dialog execution.

The fixes address scalar UI-state messages and bounded host-only non-client
transport at DLGPROC, retaining native default processing and diagnostic counts.
Windows ASan passed 1,847 modal, 336 template, 547 GUI, 28 resource and 70
unwind assertions. Existing timeouts and deliberate-failure tests remain.
See `modal-dialogs.md` and `../verification/modal-dialog-native.json`.

## Why the development branch is not yet promoted to main

Full native D3D7 acceptance remains blocked independently of these dialog fixes.
The original API probe at run `37182410794` shows IDirect3D7 available for x86
but E_NOINTERFACE for x64 on its Windows-2022 runners. Direct3D9 device/clear
works in both probes. This is scoped host evidence, not every Windows machine.
Issue #1 records the required Windows-first graphics follow-through.

The eight Windows suites here are targeted modal/GUI acceptance, not a passing
full 33-suite run or a successful E3 game. Main remains `28903ea` while the
accumulated candidate is safely preserved on the development branches.

## Actual game evidence and limitations

This pass did not run the actual E3 executable or choose its renderer. Earlier
Windows-x64 execution under Wine reached the Choose Rasterizer dialog request;
that historical result does not establish native Windows D3D7 availability.
No new E3 scene, input response, sound or interactive game-loop evidence exists.
Unsupported graphics, SEH, static TLS and guest-threading paths remain unfinished.

No original game bytes/assets, generated game source or game executable are
committed or uploaded to CI. A local Wine keyboard-cue test failure is retained
in the evidence and is not substituted for the passing native Windows result.
Earlier runtime/windowing/build evidence is preserved in the verification folder;
it is not silently reused as current-source or gameplay acceptance.
