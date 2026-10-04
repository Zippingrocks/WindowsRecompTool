# Native Windows x64 modal-dialog checkpoint

This is bounded native dialog support, not a demonstrated E3 game boot.
The recovered dialog implementation now passes the same native Windows suites
that failed before recovery, with additional UI-state regressions.

## Exact accepted source and results

Implementation `b9115719a2f9be2695880532e8e64865217340ec`, tree
`4d2b7bd6a0cfcf09c3977a53bba083dfabfea8b1`, passed Actions run `37185303944`.
All 88 recorded implementation/build/test file hashes match.

- Windows x64/MSVC: all eight selected modal/GUI/callback suites passed.
- Windows AddressSanitizer: all five native C++ boundary suites passed,
  including 1,847 modal, 336 template and 70 exception-unwind assertions.
- Linux x64/GCC: all 20 applicable suites passed; the separate native-i386
  oracle passed 15,168 comparisons across 237 authored fixtures.

The original authored PE32 test runs through the Windows loader. Its generated
x64 version runs through compiled guest dispatch. Both open and close two real
modal dialogs and produce the same 32-byte semantic record. The generated run
executed 42 dialog callbacks and preserved EndDialog results 209 and zero,
control text/check state, DWL_MSGRESULT, child-handle lifetimes, initialization
parameters, and keyboard focus/accelerator cue state. The template and C++ suites
also check rejection, nested callbacks, signed results and deliberate faults.

This is not a run of E3's Choose Rasterizer dialog. No game content is present
in the original/generated fixture, CI uploads or committed files.

## Repairs and scope

The first Windows failure was WM_CHANGEUISTATE (0x127). CHANGEUISTATE,
UPDATEUISTATE and QUERYUISTATE are now passed as scalar messages to the guest
procedure and native default processing. Tests explicitly set/clear/query cue
bits on the dialog and controls; they do not swallow these notifications.

The next failure was native host-only message 0x90. The modal procedure now
applies the same bounded host-transport policy as ordinary WinRecomp windows,
counts delegated notifications, and returns FALSE so the dialog manager handles
it. It does not call DefWindowProc from a DLGPROC or truncate native pointers
into guest state. Other unknown system payloads still stop explicitly.

Standard templates are checked before native use. This is not arbitrary support
for extended templates, custom classes, owner-drawn controls, menus, unknown
message structures, foreign guest modules or replacing dialog procedures.
The original timeouts and fault assertions remain; failed runs 37183412399 and
37184930009 are retained rather than treated as successes.

An auxiliary Wine attempt failed the new fixed keyboard-cue assertion. It is
not counted as a pass; actual Windows original/generated execution establishes
acceptance here. Local Linux tests passed in completed batches after an initial
combined invocation was interrupted. That interruption was not a test pass.

## Separate blocker for the Windows-first product

The existing direct native graphics probe, source `566acf4b`, run `37182410794`,
was retrieved again. On its Windows-2022 runners, x86 IDirect3D7 QueryInterface
returned S_OK; x64 returned E_NOINTERFACE (0x80004002). Direct3D9 HAL device
creation and Clear succeeded for both architectures. This is a host observation,
not a WinRecomp graphics implementation and not proof about every Windows host.

The current D3D7 pass-through cannot assume Wine's available implementation is
also supplied by native Windows x64. A real compatibility backend or an actually
verified supported renderer path must close this gap. Issue #1 preserves the
observations and acceptance requirements. Do not fabricate capabilities or make
the game x86 again merely to produce a passing result.

## Publication and continuation

The candidate is published on work/dialog-fixes. Main remains the older
28903ea checkpoint because selected dialog tests do not accept every accumulated
D3D7/runtime path. No new roadmap points, game frame, player-control or playability
claim is made. See verification/modal-dialog-native.json for exact evidence.

The actual E3 Windows run still needs its own private execution report. These
changes neither choose a renderer for it nor auto-answer production dialogs.

Primary specifications:
- https://learn.microsoft.com/en-us/windows/win32/menurc/wm-changeuistate
- https://learn.microsoft.com/en-us/windows/win32/menurc/wm-updateuistate
- https://learn.microsoft.com/en-us/windows/win32/menurc/wm-queryuistate
- https://learn.microsoft.com/en-us/windows/win32/dlgbox/dlgbox-programming-considerations
