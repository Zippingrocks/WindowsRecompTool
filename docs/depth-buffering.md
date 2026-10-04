# Bounded D16 depth buffering on native Windows x64

The opt-in `--legacy-renderer d3d9` path now supports a checked D16 Z-buffer
subset of the legacy Direct3D 7 surface/device interface. The game state remains
32-bit, while host objects and rendering execute in the native x64 process.
This is not an E3 scene or a full legacy Direct3D implementation.

## Accepted subset and ownership

A guest depth surface describes DDPF_ZBUFFER, a 16-bit depth field and a
0xffff depth mask. The first profile allows one attachment to a same-size
X8R8G8B8 render target on the same guest DirectDraw root, up to the existing
2048-by-2048 bound. Native D3D9 format/matching and comparison-capability checks
must pass before the profile advertises D16 support. Unsupported depth formats,
stencil, W-buffer mode and CPU access to depth pixels remain explicit failures.

Guest descriptors, output pointers and COM tokens are checked before mutation.
An attachment retains its depth resource after the application's initial guest
reference is released. GetAttachedSurface and COM aliases preserve the resource
identity. The storage is a real native D3D9 depth-stencil surface, lazily allocated
when a native device is available, not an RGB surface masquerading as depth.

The depth resource belongs to its creating native device. Rebinding on that
device preserves its contents; cross-device migration is rejected rather than
silently creating a fresh buffer. Attachment mutation is also rejected while
the color surface is locked, a scene is active, or native-device construction
is invoking reentrant callbacks. Device/target ownership is retained even when
the application releases an earlier handle.

## Rendering semantics

The implemented states are boolean ZENABLE and ZWRITEENABLE, and the eight
standard Z comparison functions when the native capabilities permit them.
With an attached buffer, initial values are enabled, writes enabled, and
less-or-equal. An explicit color clear does not erase Z, and a depth clear does
not erase color. Beginning/ending scenes or detaching/reattaching the same
resource does not clear depth. Invalid active clear values do not mutate the
rendered result; the unused depth parameter of a color-only clear is ignored.

Both indexed and unindexed triangle paths use the same checked depth state.
The C++ boundary tests additionally check viewport-limited clearing, descriptor
bounds, cancellation/fault handling in format callbacks, stale handles and
failure cases which must leave the earlier attachment and rendered result intact.

## Independent native Windows acceptance

`tests/d3d9_depth_program.py` constructs an author-written x86 PE32 which calls
real legacy Direct3D 7. Its generated Windows x64 counterpart calls the guest
bridge and real Direct3D 9. Four executions (original/generated, twice each)
cover 14 offscreen scenarios. Each scenario contains a near nonindexed draw
and a later indexed draw in separate scenes. Every defined RGB pixel is compared
across original/generated and repeated outputs, with independent expected-color
checks to prevent a shared blank result from passing.

Cases cover all eight comparisons, disabled testing/writes, color-only and
depth-only clears, and depth retention through attachment changes. Exactly
representable depth values are used; these tests do not establish all D16
quantization boundaries, hardware/driver equivalence or pixel-accurate E3 depth
behavior. The previous indexed and texture fixtures remain intact and are
rerun separately. Combined textured-depth scenes are not specifically accepted
by this new fixture.

See `../verification/depth-native.json` for exact source, run, artifact and pixel
hashes and the final pass/failure results. Preserve the initial failed MSVC
compile: the test identifier `small` collided with an SDK macro. Its repair
renamed that variable only; runtime behavior, assertions and original PE32
fixture bytes were unchanged. A successful compiler repair is not itself a
rendering result.

## Limits retained

Presentation/fullscreen, other depth/stencil formats, broader state/geometry
coverage, lighting/blending, multi-device resource migration and actual E3
Windows execution remain outside this checkpoint. Issue #2's default-path
mode-enumeration mismatch is unchanged. Selected component tests do not accept
full integration or justify promoting all accumulated work into main.

No game input, asset, disassembly or generated game code is used in hosted CI.
No new planning percentage or playability milestone is awarded for this change.

## Primary specifications

- https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-createdepthstencilsurface
- https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-setdepthstencilsurface
- https://learn.microsoft.com/en-us/windows/win32/direct3d9/depth-buffering-state
- https://learn.microsoft.com/en-us/windows/win32/api/ddraw/nf-ddraw-idirectdrawsurface7-addattachedsurface
