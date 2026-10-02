# Verification status — 0.2 runtime candidate

The previous 15/100 foundation gates remain accepted, against source `03fb54a`
and Windows/Linux CI run `36958529481` (see historical evidence). The unchanged
roadmap's next 35 points are under acceptance review. **Do not call this 50 until
the new source is published and Windows/Linux verification is complete.**

The local runtime candidate has complete generated-project execution, expanded
integer/indirect flow, native x87/SSE2 helpers and a bounded Win32/loader profile.
See `runtime-scope.md` for substantial explicit limitations. Old CI success does
not verify this new source; candidate results and final CI are recorded separately.

## Local evidence

- 230 integer fixtures: 58,880 Unicorn comparisons, native generated x64.
- 95 x87 fixtures: 18,240 native x87 comparisons over 12 precision/rounding modes.
- 156 SIMD fixtures: 29,952 native SSE/SSE2 comparisons over 16 control modes.
- Four compiled indirect/table fixtures: 1,024 Unicorn comparisons, plus eight
  rejection tests for guard bypasses, flag/index mutations and invalid data targets.
- 256 compiled guest-to-host-to-guest callback transitions.
- 2,555 bounded NLS checks against measured Windows data; direct Windows calls
  will also be compared on the Windows runner.
- Sparse page/VM, image/TEB/stack, heap, file/sharing, mutex, error-path and
  marshalling tests, plus explicit FP fault/host-state-preservation tests.
- A complete synthetic PE produces its own native executable and successfully
  allocates memory, writes/reads it, frees it and exits through the parsed IAT.
- E3's no-extra-seed graph: 113,463 instructions, 23,422 blocks, 1,308 function
  candidates, 72 statically recovered tables, zero decode/overlap diagnostics.
  There remain 440 unresolved indirect edges and 115 table candidates. Static
  table candidate edges remain explicitly unknown pending runtime validation.
- E3 with SHA-bound observed roots and original assets reaches USER32!LoadIconA
  after 20,284,057 translated instructions and 478 successful API calls on Linux x64.
  This is a pre-window initialization checkpoint, not a first frame or gameplay.

No game data, executable or generated game-derived source is used on CI.
The E3 observations are local Linux results, NOT yet a Windows E3 execution test.
Native x87/SSE hardware fixtures are distinct from native i386 integer testing;
local inability to run ELF32 is not a passing result.

## Open boundaries

GUI/rendering, audio/input, networking, guest threading, SEH/unwind, static PE TLS,
arbitrary guest DLL loading and universal Windows compatibility are unfinished.
The original whole-game behavior has not been differentially validated. The
runtime remains a correctness-first experimental implementation, not an optimized
shipping port. Finite test counts do not prove every possible instruction state.
