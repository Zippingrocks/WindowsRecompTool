# Development status: D16 depth verified on native Windows x64

**E3 is not demonstrated playable.** Native Windows x64 remains the first
product target; reliability and faithful visual output take precedence over
rushing a release or increasing a planning percentage. Other ports are deferred.

## Latest implementation and independent Windows proof

Tested source: `3bcf4348e19b6f8e09b936f4e74037f1fb97aa5a`.
Tree: `dedc464692730c2b80353dfeea102d946e70268f`.
Branch: `work/d3d9-depth`, based on the accepted indexed component at `5ef5819`.
Native Windows run: `37223574327`; all three jobs completed successfully.
The renderer remains an explicit `--legacy-renderer d3d9` selection.

An authored original PE32 program uses native legacy Direct3D 7 and its generated
x64 counterpart uses our D3D9 bridge. Both execute twice on actual Windows.
All four outputs match for every RGB pixel of 14 depth-test scenes: 4,096
pixels per scene, 57,344 per execution. Each scene uses a nonindexed draw and
an indexed draw in separate scenes. Eight comparison functions, disabled
testing/writes, independent color/depth clears and detach/rebind retention
are exercised. Only the same unused X byte is masked on both sides; no image
region is excluded. Independent expected-color checks also pass.

Output SHA-256: `8956f636f1d483d1f9e8a20024114360366b90dc1b7b7aac24671c4cd98b0e4e`.
These are synthetic offscreen rendering results, not E3 or presentation.

## What was added

The bounded backend now has real D16 native GPU depth storage, checked legacy
descriptors and attachment/COM ownership, depth testing/writes/comparisons,
color-versus-depth clear separation and retained contents across scenes and
reattachment. Unsupported depth formats, stencil, W-buffer mode, CPU depth
access and cross-device migration are explicitly rejected. Capability reports
are checked against real native format and comparison support.

See `depth-buffering.md` and `../verification/depth-native.json` for precise
scope, source identities, output files, artifact hashes and retained failures.

## Completed verification

| Scope | Result |
|---|---|
| Native Windows/MSVC new depth boundary and original/generated scenes | 2/2 suites passed |
| Retained selected Windows indexed/texture/runtime/GUI regressions | 19/19 suites passed |
| Native Windows AddressSanitizer boundary tests | 9/9 suites passed |
| Secondary local Linux Release regressions | 21/21 passed in four completed batches |

The Windows ASan depth suite passed 1,108 assertions, alongside the previous
index, texture, GUI, modal, resource and unwind tests. Its result does not cover
the separately generated executable or a whole game. The first native attempt
failed compilation because a test variable collided with an SDK macro; renaming
that variable fixed the compilation without changing runtime behavior, tests'
assertions, timeouts or the original PE32 fixture. That failed run is retained.

The CI source bundle was restored into an independent checkout. All 102 recorded
source hashes match the locally prepared source, and Git object integrity
passes. All four downloaded pixel files were independently compared, and the
original authored PE32 bytes were reconstructed and matched to their SHA.

## Remaining gates

D16 is not complete depth/stencil support or a promise about every precision
boundary and graphics driver. The new depth scenes are untextured; textures
are regressed separately, not claimed as a combined textured-depth acceptance.
Presentation/fullscreen, broader graphics states/formats, lighting/blending,
error recovery and the actual E3 Windows integration still need work.

Issue #2's original/generated default display-mode mismatch remains unresolved.
Its equality test is unchanged. These 21 selected Windows tests are not a green
full integration run, and main remains separate at its earlier checkpoint.
No actual E3 or Wine execution occurred in this pass. The historical E3 startup
under Wine does not establish native Windows game rendering or player control.

No original game executable/assets, disassembly or generated game source is
committed or uploaded. No new planning points or playability milestone are
awarded. The implementation and evidence are preserved on the development branch.
