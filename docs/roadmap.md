# Roadmap and accepted engineering gates

The first scope is a **validated E3 2000-to-Windows-x64 recompiler/runtime**, not
universal support for arbitrary Windows programs. The weights below are planning
weights, NOT measurements of engineering effort, instruction coverage, remaining
time or probability of completion. Unknown loader/runtime problems can change
this plan. In particular, copying reference repositories earns no progress points.

| Gate | Planning weight | Acceptance evidence |
|---|---:|---|
| Reproducible native toolchain and source retention | 2 | Linux and Windows x64 build/tests, pinned dependencies, published source |
| Validated PE image model | 3 | Malformed-input tests; exact E3 identity/layout and 155 normal imports |
| In-process decoder and bounded CFG | 4 | Real target traversal; deterministic output; block splitting; explicit unknown edges |
| First independently checked native translation slice | 6 | Decoded operands feed codegen; generated x64 executes; synthetic and real-target differential tests |
| Remaining integer semantics and indirect flow recovery | 10 | Broad operand/prefix coverage, jump tables, callbacks and verified dispatch |
| x87/SIMD and floating-point fidelity | 10 | Independent defined/undefined behavior tests and target corpus |
| Full guest loader, memory, Win32 ABI and imports | 15 | Correct startup, marshalling, allocations, handles and imports |
| TLS, callbacks, SEH and threading | 15 | Correct guest-to-host-to-guest transitions and failures |
| Graphics, audio and input | 20 | Faithful host backends, not dummy success stubs |
| E3 initialization and playable loop | 10 | Real initialization and gameplay through recompiled code |
| Fidelity/regression and packaging | 5 | Repeatable original-versus-recompiled tests and redistributable tool packaging |

The original first four gates remain the **15/100 foundation milestone**.
The next three gates now have published source and independent Windows/Linux
acceptance evidence, bringing the cumulative checkpoint to **50/100 planning
points**. **The weights and acceptance requirements above are unchanged.**

Acceptance is for the first scope stated above and the explicit runtime profile
in `runtime-scope.md`: fixed-base normal-import PE32 loading and the non-GUI
startup/service boundary needed by the target, not every Windows API, instruction
encoding, executable feature or host. Unsupported cases remain explicit failures.
Static image TLS/SEH/thread execution, GUI/graphics/audio/input and playable E3
remain in the later, unaccepted gates. No extra credit is taken for isolated
callback/TLS-slot building blocks that do not complete those later gates.

See `status.md`, `verification/runtime-ci.json` and
`verification/runtime-local.json` for the exact evidence. No claim that precisely
50% of the entire unknown engineering workload is finished is justified.

A reference collection, an empty interface, a compile-only stub, a self-consistent
but unvalidated model, or an unpushed working directory does not pass these gates.
Full E3 boot and universal PE recompilation remain explicitly unachieved.
