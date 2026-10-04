# Experimental native Windows x64 legacy-D3D7-on-D3D9 backend

This opt-in profile addresses the missing native x64 IDirect3D7 provider, not
all D3D7 compatibility or E3 gameplay. Select `--legacy-renderer d3d9` on the
recompiled program; `native` remains the default. Non-Windows native builds
reject selecting this profile. It is not an x86 helper process or interpreter.

The guest still calls legacy 32-bit DirectDraw7 / Direct3D7 / Device7 vtables.
The bridge maps a supported subset to real native x64 Direct3D9 devices, and
keeps all native interfaces behind validated guest tokens. The factory retains
the underlying DirectDraw controlling IUnknown identity. A device retains its
root and original target independently of guest reference lifetime. Released
guest handles remain tombstones. One live device per target is permitted.

## Deliberately bounded profile

Only the default adapter, an application-owned window in normal cooperative
mode, X8R8G8B8 targets up to 2048x2048, and untextured triangle lists with
FVF_XYZRHW|DIFFUSE are supported. Gouraud color shading, checked culling states,
viewport set/get, whole-viewport color Clear, BeginScene/EndScene, and bounded
DrawPrimitive are implemented. The capability intersection advertises no
textures, depth, lights or transformed/untransformed vertex processing beyond
TL vertices. Unsupported formats, flags, states and methods stop explicitly;
there are no guessed capabilities or success stubs to advance E3 initialization.

The version-neutral `d3d9_host.hpp` keeps incompatible SDK d3d.h/d3d9.h public
types in separate translation units. Host device creation preserves FP state.
Vertex arrays are bounds-checked and copied into owned native memory before
DrawPrimitiveUP; no guest pointer is cast to a host vertex pointer.

## Real render data and resource synchronization

D3D9 draws into a non-multisampled render target. A matching system-memory
surface is used for upload/readback. The existing native DirectDraw surface
remains the guest-visible target: its pixels are uploaded before each scene,
and the D3D9 pixels are read back after EndScene or a standalone Clear. This
preserves CPU/DD writes between scenes instead of exposing stale images.
Access to a target during a scene, mismatched roots, invalid guest buffers and
release during an active scene are rejected. This deliberately slow path is
for correctness/bootstrapping, not an optimized presentation implementation.

## Acceptance is independent of a game run

The authored PE32 test performs actual D3D7 calls on native x86 Windows. Its
statically recompiled x64 version performs the same guest calls through this
D3D9 backend. Both must render the same non-empty triangle into a 64x64 target
and write identical low-24-bit pixel arrays. The original x86 executable is
neither used as a rendering subprocess nor executed by the generated program.
Separate boundary tests cover COM identity, capabilities, faulted buffers,
scene ordering, invalid render states, readback, CPU roundtrip and lifetimes.

Completed native Windows results are recorded below. The earlier native-x64
D3D7 provider failure is not hidden or counted as a pass.
The original native path/tests remain in the source; focused new-backend tests
do not claim universal graphics compatibility. No E3 files are used by CI.

This is not enough for E3's textured scene or a playable loop. Texture/depth
resources, additional formats, indexed geometry, broader state, presentation,
fullscreen/multiadapter support and actual game execution still need acceptance.

Primary specifications:
- https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-drawprimitiveup
- https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-getrendertargetdata
- https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-updatesurface
- https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dcreate

## Native Windows acceptance and remaining integration failure

Implementation `20394a1446971c67ae18ae44e1439b6c2baf69f9` passed the original
x86-D3D7 versus generated-x64-D3D9 rendering test on native Windows in runs
`37213956877`, `37214265230` and `37214817744`. All 4,096 defined RGB pixels
match (SHA-256 `c048e32793d8e6d82ca5ab74314b0317a7227a7f9668599c939157b366b3828b`).
This is a synthetic offscreen untextured triangle, not an E3 frame or a Present.

The native C++ renderer boundary passes 294 assertions under Windows ASan. The
first run failed because cooperative-window lookup retained a bare controlling
IUnknown pointer after releasing its reference. The fix holds those identity
references through backend teardown; no rendering assertions were removed.

Expanded Windows regression runs still fail `directdraw_program` at legacy
display-mode parity on the default path, despite matching surface pixels. The
new opt-in renderer tests pass, but the full integration is not accepted and
main is not promoted. See issue #2 and `verification/d3d9-native.json`.

Textures, depth, indexed geometry, presentation, fullscreen and actual E3
Windows execution remain outside this checkpoint. Issue #1 remains open.
