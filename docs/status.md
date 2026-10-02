# Verified runtime checkpoint — 50/100 roadmap planning points

The original first seven roadmap gates are accepted for the documented target
profile. The weights have not changed: foundation 15, integer/indirect execution
10, x87/SIMD fidelity 10, and the loader/memory/Win32 ABI/import boundary 15.
This is **50/100 planning points**, not a measured percentage of unknown remaining
work, all possible x86 instructions, universal Windows compatibility or gameplay.

**Verified implementation:** `a9988b3c19571356b74f6ca7b7f50ac46d9cd54b`.
**Independent CI:** run `36993865298` (Runtime source publication and verification).
Windows/MSVC, Linux/GCC and the separate Windows AddressSanitizer job completed
successfully. All 47 recorded implementation/build/test/measurement file hashes
match the tested source. Documentation updates do not change that implementation.

## What works end to end

`winrecomp project input.exe output-directory` generates a complete CMake project,
including native compiled dispatch and the runtime, for the supported PE32 profile.
The input is SHA-256-bound; guest pointers/registers remain 32-bit. No interpreter,
JIT or Unicorn execution fallback is used. Unknown imports, unknown code targets,
changed code and unsupported behaviors stop explicitly rather than returning fake
success. `lift` remains available for independently tested slices.

The Windows end-to-end fixture runs an author-written PE32 through the **actual
Windows loader**, then runs its generated native x64 version. Both must produce
the same 16-byte file and exit successfully. The 15 real API calls cover virtual
memory, heap creation/growth/freeing, last-error state, file creation/writing/size,
handle closing and process exit. The output SHA-256 is
`5272460991e36a2ac7fe76a71c895c4a8c3a3e74cb41527dc9786eb597c7a7e5`.
This test passed on Windows; it is not merely comparison against our own model.

## Completed independent tests

| Test | Windows x64/MSVC | Linux x64/GCC |
|---|---:|---:|
| CTest suites | 14/14 | 14/14 |
| Integer fixtures vs Unicorn (230 cases) | 58,880 comparisons | 58,880 comparisons |
| x87 vs uninterrupted native x87 (95 cases) | 18,240 comparisons | 18,240 comparisons |
| SSE/SSE2 vs uninterrupted native instructions (156 cases) | 29,952 comparisons | 29,952 comparisons |
| Compiled indirect/table cases | 1,024 comparisons + 8 negative tests | 1,024 comparisons + 8 negative tests |
| Compiled guest-host-guest callback ABI | 256 cases | 256 cases |
| Generated standalone program | Original PE32/output comparison passed | Generated program/output check passed |

Linux additionally passed **14,720 native-i386 comparisons** across all 230
integer fixtures. That oracle executes ELF32 x86 instructions, not Unicorn.
The floating-point hardware oracle is distinct: original dual-mode x87/SSE
instructions execute on x64 hardware. It is not an i386 whole-game oracle.
Defined flags, control/status state and data are compared; host pointer/selector
fields and undefined empty-stack payloads are excluded explicitly. Guest
pointer/selector environment behavior has separate deterministic regressions.

Windows AddressSanitizer passed both native `process` and `nls` suites. Additional
local Clang Release and GCC ASan/UBSan configurations each passed all 14 suites.
Not every separately compiled generated-code DLL/project inherits sanitizer
flags; these results are not a claim that an entire game was sanitizer-checked.

## Real E3 evidence — local Linux x64

Input SHA-256:
`3de853fe163d6c869107a861fe57b394cfe198d823d47331c5ffce5fa28b9fda`.

The no-extra-seed graph contains **113,463 instructions**, **23,422 blocks**,
**1,308 function-entry candidates** and **72 recovered guarded tables**. It has
no decode/overlap diagnostics and is byte-identical across repeated runs and the
sanitized analyzer. **440 unresolved indirect transfers** and **115 table
candidates** remain; this is not a complete function/call-graph recovery claim.

With the SHA-bound observed seed profile, a project containing **113,903 admitted
instructions** was generated and compiled **without `--partial`**. That means
those admitted forms have emission support, not that every possible target was
statically discovered or every game behavior was verified.

The latest repeated native startup run completed **20,284,053 translated guest
instructions** and **478 implemented API dispatches**, then stopped explicitly
at **`user32.dll!LoadIconA`**. The instruction count is translated x86 instructions,
not a physical x64 instruction count. The API counter can include normal failure
returns and is not a count of successful API requests. Observed counts can vary
slightly with file/environment state; the meaningful checkpoint is pre-window
initialization reaching the same unsupported API.

Five original routines still pass **5,120 Unicorn comparisons** with the current
lifter. In addition, a SHA-bound safe selector extracted **24 distinct contiguous
x87 sequences**, **207 unchanged original instructions**, and passed **9,216
native-hardware comparisons** across 12 precision/rounding modes. These are
controlled instruction sequences, not 24 recovered gameplay functions.

No original E3 binary, asset, disassembly or generated game source was uploaded
to CI or committed. Actual E3 execution and its real-input FP corpus were tested
locally on Linux x64, **not on Windows or as a whole original i386 game**.

## Defects fixed during acceptance

The failed CI history is retained. Repairs include the MSVC calling-convention
identifier collision; RCL/RCR undefined overflow-flag handling for nonzero
modulo-zero counts; in-place bounded-heap growth bypass; API allocation ownership;
Windows file-size marshalling; x87 flat selector restore boundaries; and an actual
Windows heap corruption caused by oversized `GetVolumeInformationA` host buffers.
The last defect was localized with native Windows ASan and fixed using the API's
MAX_PATH+1 bound, followed by direct native-volume and optional-buffer regressions.
These were fixed rather than suppressed or counted as passes.

## What remains unaccepted

Static PE TLS, SEH/unwind/exception delivery, guest thread scheduling, arbitrary
guest DLL loading, rebasing support, GUI/window callbacks, graphics, audio/input,
networking and the E3 playable loop remain unfinished. Some callback and dynamic
TLS-slot building blocks exist; they do not earn the later TLS/SEH/threading gate.
There is **no game window, rendered frame or playable E3 port** at this checkpoint.
The exact supported/unsupported profile is in `runtime-scope.md`.

Machine-readable current evidence is in `verification/runtime-ci.json` and
`verification/runtime-local.json`. Historical foundation evidence remains in
`verification/ci.json` and `verification/local.json`; it has not been substituted
for the new source's tests. Source history and dependency bundles are retained in
CI artifacts even when later test runs fail. Source itself is committed in Git.
