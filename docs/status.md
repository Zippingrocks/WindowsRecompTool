# Current verified status

**E3 is not yet demonstrated playable.** The accepted roadmap total remains
**50/100 planning points**, not a measured percentage of game completion or
unknown engineering effort. No new points are awarded for the windowing work.

## Latest implementation and independent results

Implementation: `b02db47c2dcd5a073679dcf90e2ad99fb0d7133c`.
Source tree: `5062c186a3834f9fd9bf9f930f2e5043b5598a89`.
GitHub Actions run: `37162230094` (Native windowing verification).
All three jobs completed successfully; 60 recorded source hashes match.

| Verification | Result |
|---|---|
| Windows x64/MSVC | 18 suites: one original-vs-generated window test and 17 other suites |
| Linux x64/GCC | 16 suites and 14,720 native-i386 integer comparisons |
| Windows AddressSanitizer | GUI, resource and exception-unwind suites passed |
| Additional local Linux Release | 16 suites passed |
| Additional local GCC ASan/UBSan | 10 non-conformance suites passed |

Both independent platforms also passed 58,880 integer, 18,240 x87, 29,952 SIMD,
1,024 indirect-control-flow and 256 callback-ABI comparisons. These retain the
same finite-fixture and defined-state exclusions as the earlier runtime tests.
The native floating-point oracle uses suitable original instruction sequences
on x64 hardware; it is not a native-i386 whole-game execution oracle.

## What is newly accepted

A bounded Windows USER32/GDI/WGL backend now creates actual windows, marshals
legacy structures and typed handles, runs compiled guest window callbacks,
loads icon resources, creates a native OpenGL context and renders a synthetic
triangle. The author-written original PE32 and generated x64 version produce
identical framebuffers and semantic callback records. The controlled fixture
created/destroyed one window, executed 22 window callbacks and swapped one frame.
This is driver/window infrastructure, not the E3 renderer.

The existing development branch initially failed its Windows ASan test. This
pass fixed the configuration: replacing CMAKE_CXX_FLAGS had removed C++ stack
unwinding. /EHsc now propagates to consumers and generated projects; missing
unwinding is rejected at compile time. The new nonblocking regression checks
that 14 fault paths release guest-memory locks for another thread. All 70 new
unwind assertions, 527 GUI assertions and 28 resource assertions passed on ASan.
The GUI timeout and original fault checks were not removed or relaxed.

See `windowing.md` and `../verification/windowing-ci.json` for current scope,
source identities, artifact hashes and the explicit playability criterion.

## Actual E3 evidence and current limits

This pass locally revalidated the exact E3 input identity, deterministic CFG
contract and five original routines (5,120 comparisons). It did not execute
full E3 startup on Windows, demonstrate an E3 frame or test player control.

The earlier real-target Linux startup reached more than 20 million translated
guest instructions and stopped at USER32!LoadIconA. That remains a limitation
of the non-Windows diagnostic backend; Windows LoadIconA now has synthetic
acceptance evidence. It must not be treated as a fresh Windows game-run result.

DirectDraw/Direct3D, complete GL texture/buffer/extension coverage, static image
TLS, SEH/exception delivery, guest threading, full audio/input/networking and
the interactive E3 loop remain unfinished. A synthetic triangle does not accept
the whole graphics/audio/input gate. No game bytes or game-derived code were
uploaded to CI or committed.

## Preserved historical evidence

The 50-point runtime implementation is `a9988b3c19571356b74f6ca7b7f50ac46d9cd54b`,
verified by run `36993865298`. Its records remain in `verification/runtime-ci.json`
and `verification/runtime-local.json`, with the later clean revalidation in
`verification/checkpoint-revalidation-2026-10-02.json`. The original 15-point
foundation results remain in `verification/ci.json` and `verification/local.json`.
Those historical tests are not substituted for current-source results.
