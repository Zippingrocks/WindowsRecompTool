# Native Windows alpha blending checkpoint

The bounded D3D7-on-D3D9 Windows x64 renderer now supports a checked fixed-function
alpha-blend subset and reports that support back through the guest-facing
Direct3D 7 device capabilities.

Tested source: `ba0a52ee1c980bb5f91dff9c459a726fa95a68f9`.
Native Windows workflow: `37245599197`.

Accepted source/destination factors are ZERO, ONE, SRCALPHA and INVSRCALPHA.
The primary tested path is SRCALPHA/INVSRCALPHA. Alpha-bearing textures blend
while D16 depth testing/writes remain active, for both ordinary and WORD-indexed
textured triangles. A farther draw submitted after a nearer translucent draw is
still rejected by depth. The guest Direct3D 7 capability structure advertises
the bounded blend support, and guest SetRenderState/GetRenderState calls round-trip it.

The same source passed its Windows AddressSanitizer boundary run and retained the
combined renderer, presentation, depth, indexed-geometry and texture checks.

This remains synthetic component evidence, not an E3 frame or playability result.
More blend modes, alpha testing, fullscreen and display-mode parity are outside
this checkpoint.
