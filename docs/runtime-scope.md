# Native runtime profile and limits

The 0.2 runtime is a correctness-first static x86-to-x64 pipeline. It generates
C++, compiles it with a native compiler, and dispatches guest program counters
to compiled instruction bodies. It does not run the input EXE, interpret its
opcodes, or use Unicorn as a runtime fallback. Test oracles are separate.

## Guest machine

Guest GPRs, flags, stack addresses and pointers remain 32-bit. Byte/word register
writes preserve unaffected bits. Explicit EFLAGS definedness controls differential
comparisons. Integer operations cover shifts/rotates, wide multiply/divide,
conditional moves, loop/control instructions, LOCK operations and restartable
REP/REPE/REPNE strings. Instructions still undergo operand/prefix preflight;
there is no claim to support every far/system/MMX/AVX form of each mnemonic.
Indirect calls snapshot their original target before stack mutation. Calls,
returns, switches and callbacks all use checked guest addresses.

The sparse memory model tracks reservations, commitments, page protections,
allocation ownership and 32-bit range overflow. Multi-byte writes validate the
entire range before modifying memory. Failed divide operations and repeated
string faults preserve the architecturally relevant restart state. A process
budget also bounds REP iteration and callback recursion. This is not a security
sandbox for hostile programs, a scheduler, or a resumable Windows exception model.

## Floating point

x87 values use the 80-bit format inside an aligned FXSAVE64 representation, not
host `double` or MSVC `long double`. Build-time generated GAS/MASM helpers execute
fixed, whitelisted x87/SSE/SSE2 operations and preserve the host FP environment.
There is no runtime JIT or arbitrary native execution of input instruction bytes.
Precision control, rounding, sticky status, DAZ/FTZ and guest stack TOP are tested
against native uninterrupted instruction fixtures. Host FIP/FDP addresses never
leak into guest state. FLDENV restores physical/logical stack association correctly.

The oracle tests x87/SSE on x64 hardware. It is not a proof of bit-identical
transcendental behavior on every historical x86 CPU. Native i386 integer tests
are a distinct Linux CI suite. Unmasked FP exceptions stop explicitly, before
committing data, rather than pretending to deliver Windows SEH. Noncanonical
full x87 tags that the abridged backend cannot represent, and unsupported
16-bit environment formats, are rejected. MMX/AVX and general privileged/system
instruction behavior are not implemented.

## PE startup and ABI

A generated project is SHA-256-bound to its original input file. It loads the
fixed-base PE image, zero-fill, section/header permissions and a guarded stack;
creates a minimal TEB/FS model; and binds normal IAT slots to guest thunk tokens.
API buffers are copied and validated, and host pointers/handles do not enter
32-bit guest structures. Runtime opcode checks reject modified code rather than
silently running a stale translation. A report path cannot overwrite the input,
including through a hard link or an argument-error path.

The current guest profile has one thread, dynamic TLS slots, a scoped virtual
C: drive, an explicit virtual user (`WinRecomp`), and process-local guest handles.
Named mutex operations on Windows use native mutex objects; the Linux diagnostic
backend models a single-guest-process namespace. Critical-section recursion is
modelled for the current single thread; this is NOT completed guest multithreading.
Static PE TLS, relocations/rebasing, arbitrary guest DLL loading, callback-based
window dispatch, SEH/unwind and thread creation still have explicit boundaries.
Unknown dynamic exports fail closed; they are not passed off as absent exports.

## Files, text and host differences

Host file operations are confined to a configured data root, with writes off by
default. Explicit `--allow-write` enables actual file/directory mutations there.
The lexical/canonical path guard is not an adversarial filesystem race sandbox.
Windows calls use native sharing and handle operations behind guest marshalling.
The Linux diagnostic backend has documented narrower flags and sharing semantics;
it does not promise cross-process Windows sharing, every DOS wildcard, or 8.3 names.
A guest cannot directly access unrelated host drives. Linux presents one fixed
virtual drive and UTC; Windows derives supported time/volume facts from native APIs.

Windows NLS calls use native APIs with validated guest buffers. Portable CP1252
classification/casing/conversion is restricted to a recorded Windows-measured
corpus: 283 codepoints and 284 encoding inputs, including measured best-fit results.
The exact data and reproducible header generator are retained. This is not a full
Unicode or historical Windows 2000 database. Portable arbitrary collation, formatted
system-message text and pipe-data peeking are explicit unsupported boundaries.
Windows supports those paths through native API marshalling, within the documented
argument/flag profile. Variadic FormatMessage inserts and guest resource-module
formatting remain rejected instead of forwarding guest pointers to host varargs.

## Real E3 checkpoint

The metadata-only runtime profile pins input SHA-256 and observed extra entry
seeds. With the supplied assets kept locally, native compiled E3 execution reaches
`USER32!LoadIconA`, after CRT, tag-file reads and substantial pre-window initialization.
That call currently stops explicitly. This does NOT mean a window, rendered frame,
input system, networking, audio, playable loop or original-versus-recompiled
whole-game fidelity has been completed. The real-input run is local Linux x64;
Windows CI uses only author-written synthetic PE files and API fixtures, not Halo.
