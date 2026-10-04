# Combined native Windows renderer checkpoint

The Windows x64 D3D7-on-D3D9 development backend now has a native acceptance
test that combines systems previously verified mostly in isolation.

Tested source: `da52dcc034a37248ca75df9aeb20b1d86cba90e5`.
Native Windows workflow: `37243762060`.

One D3D9 device path now passes texture sampling, D16 depth testing/writes,
nonindexed textured drawing, WORD-indexed textured drawing, render-target
readback and a real swap-chain Present in the same test. Near textured
geometry occludes farther textured geometry, a texture can be changed while
the scene is active, and the indexed textured path uses the same depth buffer.

The same candidate passed its Windows AddressSanitizer boundary run. The test
also retains negative behavior: Present is rejected inside an active scene,
and depth cannot be silently re-enabled after its resource is unbound.

This is intentionally still a synthetic renderer component test. It is not an
E3-generated frame, does not close display-mode parity issue #2, and does not
accept fullscreen, lighting, blending, broad legacy render states or complete
format compatibility.

See `verification/combined-native.json` for the machine-readable scope.
