# Foundation status

The four foundation gates are accepted: **15/100 roadmap planning points**.
The source is published and Windows/Linux CI has completed successfully.
It is not a claim that E3 2000 boots or that the whole project is mechanically
15% complete. See the planning-gate definition in `roadmap.md`.

## Verified in the development workspace

- C++20 analyzer and native x64 generated-code test library build successfully.
- PE/CFG/runtime tests pass, including malformed headers, imports, ordinal imports,
  TLS/export seeds, overlapping branches, raw-byte limits and fail-closed codegen.
- E3 input SHA-256: `3de853fe163d6c869107a861fe57b394cfe198d823d47331c5ffce5fa28b9fda`.
- Image base `0x00400000`, entry `0x004AF118`, image size `0x00131000`, four sections,
  and 155 imports across nine DLLs.
- Entry/export/TLS-rooted traversal: 101,542 admitted instructions, 21,063 blocks,
  1,234 function-entry candidates, zero reported decode/overlap diagnostics.
- Unknowns remain: 371 unresolved indirect transfers and 103 jump-table candidates.
  Import transfers are separately labeled, not treated as implemented APIs.
- An initial Debug traversal took 1.88 seconds, including JSON output, in this
  environment. This is one observation, not a general performance guarantee.
- 60 synthetic byte fixtures compiled to native x64: 15,360 Unicorn comparisons
  passed (256 per fixture), comparing eight GPRs, EIP, defined status flags and
  complete 64 KiB stack contents.
- Five routines from the real E3 executable compiled to native x64: 5,120 additional
  comparisons passed (1,024 each). Entries: `0x004AE3C3`, `0x004AE455`, `0x004B2D61`,
  `0x004B3218`, `0x004B7EB5`. Names and gameplay roles are not established; some
  look like compiler/runtime helpers. This is not proof of gameplay recompilation.

## Verified on independent CI runners

Source commit: `03fb54a2e99c8b1000a1250b0387109fa9a86eb3`.
GitHub Actions run: `36958529481` (Build and conformance).
Both `verify (windows-2022)` and `verify (ubuntu-24.04)` completed successfully.
The source-policy check verified all nine upstream gitlinks and the exact hashes
of all 16 locally tested implementation/build/test files on both runners.

- Windows x64/MSVC: parser/runtime/frontend tests passed; generated native x64
  passed all 60 fixtures, 15,360 Unicorn comparisons.
- Linux x64/GCC: the same suites and 15,360 Unicorn comparisons passed.
- Linux native i386 oracle: 60 synthetic fixtures, 3,840 comparisons against
  actual ELF32 instruction execution passed. This is not Unicorn or a bytecode
  interpreter. It compares the original x86 fixture with the generated x64 code.
- Additional local Clang Release build: all suites and 15,360 comparisons passed.
- Local GCC Debug, Release, ASan/UBSan tests and full E3 CFG sanitizer run passed.

Machine-readable results are retained in `verification/local.json` and
`verification/ci.json`. The E3-derived routines were validated locally on Linux
x64 against Unicorn, not on Windows or native i386. No game material was uploaded
to CI. The local kernel's unsupported ELF32 result (77) is still documented;
it was not counted as a pass. The separate CI native i386 run really passed.

Nine upstream repositories are pinned as submodules; only Zydis/Zycore is linked
into the tool. Other pins are isolated references, not merged implementations.

## Explicitly not implemented

Full instruction coverage, x87/SIMD, reliable indirect dispatch, Win32 import
execution, FS/TEB/TLS allocation, Windows exceptions, guest threads, rendering,
audio/input, game initialization and gameplay. `lift` emits a supported code
slice, not a standalone replacement game executable. No earlier claimed saved
implementation was counted: this change was rebuilt from the actually available
README/bootstrap and validated against the supplied binary.
