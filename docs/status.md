# Development status: indexed rendering verified on native Windows x64

**E3 is not demonstrated playable.** The primary target is native Windows x64,
with reliability and faithful visual output before release. Other platform ports
are deferred. See `windows-quality-gates.md` for the enduring acceptance policy.

## Latest verified component

Implementation: `4ada6f5784aeb1fdd662cbf5d27c49bbbf62592c`.
Source tree: `d7d0cd964f4efae9f1ab892f0c2eef817ffbe8e1`.
Development branch: `work/d3d9-indexed`, based on texture source `75ef8dd`.
Native Windows run: `37220645029`, all three jobs completed successfully.
The opt-in renderer remains `--legacy-renderer d3d9`.

The legacy DrawIndexedPrimitive call now supports checked WORD-indexed triangle
lists with transformed/diffuse vertices, optionally one texture coordinate set.
The native backend submits actual indexed D3D9 drawing, not a fake success,
interpreter, or 32-bit renderer subprocess. Shared/sparse vertices, nonzero
minimum indices, legal degenerate triangles and high unsigned WORD indices
are covered. Only referenced vertex payloads are treated as geometry.

Two authored original x86/D3D7 scenes and their recompiled x64/D3D9 versions
each executed twice on Windows: eight runs, every RGB pixel equal across the
four outputs for each scene. Each output has 4096 pixels; no image region is
excluded. Both fixtures mask only the same unused high X byte. The textured
case also retains a texture across guest release and samples a post-bind edit.
The previous unindexed original fixture bytes remain unchanged.

## Completed verification for that source

| Scope | Result |
|---|---|
| Native Windows/MSVC indexed range, boundary and original-vs-generated scenes | 3/3 suites passed |
| Previous selected native Windows texture/runtime/dialog regressions | 16/16 suites passed |
| Native Windows AddressSanitizer | 8/8 C++ boundary suites passed |
| Secondary local Linux Release | 21/21 suites passed in a completed invocation |
| Secondary portable index-validator ASan/UBSan | 6252 assertions passed |

The Windows ASan indexed checks include 6252 range/payload assertions and 228
actual GPU-bridge bounds, pixel and lifetime assertions, plus unchanged texture,
GUI, modal, resource and unwind tests. This is not a whole-game sanitizer claim.
No new Wine or actual E3 run occurred. All 99 recorded source hashes match a
fresh checkout restored from the native Windows CI bundle; Git integrity passed.
See `../verification/indexed-native.json` for identities, output hashes and scope.

## Integration remains incomplete

These 19 selected Windows suites are not a passing full-runtime/game run.
Issue #2's original/generated default display-mode mismatch remains unresolved;
its test and exact equality check are unchanged. Main remains at `28903ea`
pending acceptance of the accumulated integration, not merely this component.
Depth buffering, presentation/fullscreen, additional geometry and state paths,
lighting/blending and actual E3 gameplay are unfinished. No such capability
is advertised as implemented by this indexed change.

The earlier E3-derived Windows x64 program reached its Choose Rasterizer dialog
request under Wine. That remains historical game evidence, not native Windows
game execution or a result from this candidate. Synthetic renderer/dialog tests
cannot be combined into an imaginary successful E3 run. No new game scene,
input response, sound or playability result is claimed.

The original 50/100 roadmap planning checkpoint remains historical bookkeeping;
no new points are awarded here. The source and evidence are published on the
development branch, while game files remain private and outside Git/CI.
