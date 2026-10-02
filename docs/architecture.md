# Architecture

`PE32 bytes -> validated image -> Zydis operands -> conservative CFG and guarded
tables -> checked C++ emitter -> native x64 compiler -> explicit guest state,
sparse memory and marshalled host APIs`

The detailed implementation boundaries, host differences and failure behavior are
in [runtime-scope.md](runtime-scope.md). The unchanged weighted project gates are
in [roadmap.md](roadmap.md); the accepted evidence is in [status.md](status.md).

Generated code is statically compiled, split by guest code pages for manageable
compiler memory use. The original executable's image bytes remain required for
loading and immutable instruction checks. No generic runtime decoder, bytecode
interpreter, emulator fallback or original-game native EXE execution is used.

Zydis is pinned and linked into analysis. The native runtime uses our own checked
integer/state/memory code and fixed GAS/MASM floating-point helpers. Unicorn and
native fixture runners are independent tests, not product runtime dependencies.
The other pinned repositories remain isolated references with explicit licensing
boundaries; this is not a merger of whole engines.
