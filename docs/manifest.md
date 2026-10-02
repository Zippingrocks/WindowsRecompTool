# Block manifest v1

`winrecomp cfg input.exe output.json [--entry VA] [--max-instructions N]`

The schema name is `winrecomp.cfg.v1`. Addresses and exclusive block ends are
unsigned 32-bit guest VAs represented as JSON numbers. `image` contains
`winrecomp.pe.v1`, sections, all 16 recorded directory descriptors, normal imports,
exported-code entries and TLS callbacks. No machine-specific file paths or run
timestamps appear in the manifest, so identical input/options/pins yield the same
bytes.

`instructions` contain address, size, raw bytes as hexadecimal, disassembly,
mnemonic and typed visible operands. Register and memory widths are explicit.
Immediate values are hexadecimal strings (`value_hex`), with separate `signed`
and `relative` flags. This preserves all decoder bits even in JSON consumers that
otherwise round large integers. Instruction bytes remain the authoritative encoding.

`blocks` list start, exclusive end and ordered instruction addresses. Every
admitted instruction is assigned exactly once. `edges` refer to the terminating
or originating instruction, not necessarily to a block start. Kinds include
fallthrough, call_continuation, branch_taken/not_taken, call_direct, jump_direct,
call_import, jump_import, call_indirect, jump_indirect, return and stop.

Null targets are intentional: the detail field identifies an import or unresolved
operand where available. An import edge is resolved *by name*, not implemented.
`function_candidates` and `jump_table_candidates` state evidence, not certainty.
`diagnostics` records rejected paths. `budget_exhausted` makes a truncated run
machine-detectable and the CLI returns 3. `whole_program_complete` is always false
in this version; absence of diagnostics never overrides unknown indirect flow.
