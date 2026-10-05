# Native Windows alpha-test checkpoint

The bounded Windows x64 D3D7-on-D3D9 renderer now supports capability-checked
fixed-function alpha testing for legacy cutout textures.

Tested source: `bb3aee6ac3c51ca94b66fb279dec2ea5b7e7d805`.
Native Windows workflow: `37245895360`.

The acceptance test uses alpha-bearing textures with D16 depth enabled. Alpha-zero
texels are discarded and, critically, do not write depth; farther geometry remains
visible behind those holes. Opaque texels write both color and depth. The same
behavior is checked through ordinary and WORD-indexed textured drawing, followed
by presentation.

The guest-facing Direct3D 7 device capabilities advertise the accepted alpha
comparison support, and guest alpha-reference/function/enable render states
round-trip through the compatibility boundary.

Both the normal native Windows/MSVC job and Windows AddressSanitizer job passed,
retaining alpha blending, combined rendering, presentation, depth, indexed
geometry and texture checks.

This remains synthetic component evidence, not an E3-rendered frame or
playability result. Fog, lighting, fullscreen and display-mode parity remain
outside this checkpoint.
