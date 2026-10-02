# Architecture and boundaries

`PE32 bytes -> validated image -> Zydis instructions + operands -> conservative
CFG -> supported integer/control-flow subset -> generated C++ blocks -> native
x64 compiler -> explicit 32-bit guest CPU and checked memory`

The generated program does not decode opcodes or invoke Unicorn at run time.
Its switch dispatches **statically compiled basic blocks** by guest EIP. Direct
calls push guest return addresses, not host pointers. Returns pop a guest address.
This is intentionally a correctness-first implementation, not an optimized IR.

## Image model

The parser accepts ordinary PE32/i386 images up to 512 MiB, validates header and
section bounds with widened arithmetic, rejects overlapping raw/virtual ranges,
reads normal name/ordinal imports with OriginalFirstThunk fallback, inventories
directories, and seeds exported code and TLS callbacks. It maps header/section
bytes and zero-fills virtual tails. Relocations, bound/delay import resolution,
TLS allocation and full Windows loader behavior are not implemented.

This strict profile is not a claim to accept every unusual PE that Windows accepts.
The input is never executed by the analyzer. Mapped image creation is separate
from parsing so malformed headers cannot force an unbounded allocation.

## Discovery

An ordered worklist follows direct calls/branches and possible continuations,
only in executable file-backed ranges. Decoding is on demand and uses Zydis
operand objects, not parsed disassembly strings. A second pass creates/splits
blocks at all discovered leaders. Overlapping instruction paths, unbacked targets,
decode failures and exhausted budgets are explicit diagnostics.

Function entries are **candidates**, not recovered source-level functions.
Calls are conservatively assumed to have a possible return continuation.
Register-indirect flow remains unresolved; IAT references are named external
edges, not invented internal targets. Indexed jumps are only jump-table
candidates. Callback pointers, vtables, compiler helper tables and non-returning
function knowledge need later recovery/annotations. Zero decode errors therefore
does NOT mean full-program discovery.

## Initial semantics

Supported forms include scalar MOV/MOVZX/MOVSX/LEA, ADD/ADC/SUB/SBB/CMP,
AND/OR/XOR/TEST, INC/DEC/NEG/NOT, near direct CALL/JMP/RET, ordinary Jcc and
JCXZ/JECXZ, register PUSH/POP, immediate/memory PUSH, LEAVE, NOP and carry controls.
The emitter checks widths/register classes/prefixes and refuses unsupported forms.
There is no blanket claim that every form of every listed mnemonic is implemented.

8-, 16- and 32-bit registers preserve the correct unaffected register bits.
Arithmetic uses unsigned/widened values, explicit status flags and defined-flag
masks. Logical instructions leave AF undefined; tests do not demand one arbitrary
AF value. Address arithmetic remains uint32_t, with 16-bit addressing masked.
FS/GS accesses, LOCK/REP semantics, floating point, SIMD, indirect dispatch,
imports and far transfers have no fallback that silently pretends success.

## Memory and host boundary

The small runtime has explicit guest ranges and permissions; unaligned scalar
loads/stores are little-endian, unmapped accesses throw, and multi-byte stores
validate all touched bytes before writing. The loader initialization API is
separate from guest writes. Fault exceptions currently terminate a test run;
they do not yet implement x86 fault delivery or Windows SEH.

A full game needs guest heap/stack allocation, precise fault semantics, import
marshalling, handle translation, callbacks, FS/TEB/TLS, SEH, threading and the
render/audio/input backends. These remain future milestones. No host DLL address
may be placed directly in a 32-bit guest IAT.
