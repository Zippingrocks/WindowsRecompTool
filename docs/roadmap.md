# Roadmap and the 15-point foundation gate

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

The first four accepted gates sum to **15/100 planning points**. This is the
only precise meaning of the project's “15% foundation milestone.” No statement
that 15% of the entire unknown engineering workload is finished is justified.

A reference collection, an empty interface, a compile-only stub, a self-consistent
but unvalidated model, or an unpushed working directory does not pass these gates.
Full E3 boot and universal PE recompilation remain explicitly unachieved.
