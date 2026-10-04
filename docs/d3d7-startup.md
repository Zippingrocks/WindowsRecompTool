# Local Direct3D 7 startup boundary

This candidate targets Windows AMD64/PE32+, not a native Linux game port.
Its execution evidence is Wine 10 on Linux with a software-rendering host.
Native Windows/MSVC and Windows AddressSanitizer have NOT run this candidate.
The game is not demonstrated playable. No new roadmap points are accepted.

## Actual E3 result

The SHA-bound input is
`3de853fe163d6c869107a861fe57b394cfe198d823d47331c5ffce5fa28b9fda`.
The Windows x64 project now admits 115,823 instruction locations without partial
translation. The final MinGW output is 21,645,313 bytes, SHA-256
`e23cbbac94df9f4a479743d40235c7838d8a00395032e13397e32741963363e8`.
Independent format inspections identify AMD64/PE32+. The original is unchanged.

Two final runs each executed 20,355,349 translated guest instructions and 637
API dispatches, created one Win32 window via Wine, ran 36 window callbacks,
created four real host IDirect3DDevice7 objects, and completed 56 texture-format
callbacks. Twenty-one guest COM wrappers were created and retired before the
next unsupported API. These are startup probing objects, not four rendered
frames or four physical GPUs. API counts include ordinary failure returns.

The actual next call is `user32.dll!DialogBoxParamA`. Static resource metadata
identifies resource 101 as **Choose Rasterizer**, with 18 controls. This resource
has NOT been displayed by WinRecomp. No fabricated dialog success, selection,
or cancel response was supplied. There is still no game frame, DirectDraw blit,
OpenGL context/swap or player-control evidence. A successful device creation
must not be reported as playable gameplay.

## Supported interface increment

- IDirect3D7: identity/reference handling, EnumDevices, EnumZBufferFormats,
  CreateDevice and EvictManagedTextures. CreateVertexBuffer still stops.
- IDirect3DDevice7: identity/reference handling, GetCaps, EnumTextureFormats,
  GetDirect3D and GetRenderTarget. BeginScene/DrawPrimitive and other unwrapped
  methods still stop; this change does not implement the game renderer.
- IDirectDraw7: GetDeviceIdentifier, GetCaps, modern EnumDisplayModes,
  CreateClipper, native primary creation, and bounded offscreen 3D targets.
- IDirectDrawClipper: typed identity/references, SetHWnd/GetHWnd and
  IsClipListChanged. Surface7 SetClipper/GetClipper preserve native ownership.
  Arbitrary clip lists and clipper initialization remain outside the profile.

The guest still holds 32-bit pointers and typed COM objects. Native interfaces,
window handles and driver memory are not exposed by truncating host pointers.
The 1068-byte x86 DDDEVICEIDENTIFIER2, 124-byte DDSURFACEDESC2 and 380-byte DX7
DDCAPS are checked independently; the legacy 108-byte enumeration remains.
D3DDEVICEDESC7 has a verified pointer-free 236-byte layout with 220 defined bytes
and four reserved DWORDs. Reserved fields and inactive pixel-format unions are
normalized, not populated with undefined native storage.

Callback descriptors are bounded read-only guest scratch, expire when the
callback returns, and are cleaned up on faults. Device names/descriptions use
bounded process-owned read-only storage that survives enumeration. A native
reference keeps enumeration receivers alive through nested calls or guest
release. Callback failures are carried out of the native callback and rethrown
at the surrounding API; they are not converted into successful enumeration.
The guest is single-thread-owned for these interfaces. Unsupported encodings,
interfaces, targets, aggregation, flags or callback returns remain explicit.

GetCaps comparisons exclude only volatile video-memory totals, not capability
bits. Duplicate primary-surface creation is checked against the actual host's
HRESULT rather than assuming a specific failure. No capability or device GUID
is substituted to coerce a path through the game.

## Current-source testing

Seventeen Windows-target C++ executables passed under Wine. New tests cover
Direct3D7 (372 assertions), Device7 (253), adapter identity/caps (2074), DD7 modes
(167), and primary/render surfaces plus clippers (143). They compare native host
methods with the guest adapter, invalid outputs and stale/wrong-interface
objects, cancellation, descriptor lifetimes, nested callbacks and reference
ownership. None uses game bytes.

`tests/d3d7_execution.py` also emits and compiles an author-written x86 program.
Thirty-two executions alternate continue/cancel results and compare every
returned defined device-description byte plus strings with direct native host
IDirect3D7 enumeration. It compared 64 rows on this Wine host. This is not an
original native-PE32 process comparison, and not full Device7 rendering coverage.
Its native Windows CI integration is prepared but has not run this candidate.

Linux Release passed all 19 suites in one completed invocation. All 12 Linux
ASan/UBSan suites passed after the interrupted build was resumed and linked.
Those sanitizer results do NOT instrument or execute Windows-only code paths.
Existing integer/x87/SIMD suites and five E3 routine checks also passed.
The Windows NLS test reported two differences from the old recorded Windows
snapshot for U+02C6 while direct-host comparisons passed; no historical Windows
or all-Unicode fidelity is claimed. Earlier interrupted commands were not passes.

## Reproduction and next blocker

Build the Windows project with `examples/e3_2000/windows-x64.json`. The profile
contains seven additional SHA-bound, runtime-observed executable entry seeds.
The original executable is not patched. Actual runs use the explicitly selected
`scalar-v1` CPU profile and an explicit guest DLL root; missing Glide is a checked
absence in that root, not a fake failure. Private asset-copy writes were enabled
for these tests, not arbitrary writes from uploaded code.

On native Windows, the generated executable should be tested with the same
original SHA and assets, and the resulting process report retained privately.
Under Wine all game execution remains a compatibility-host diagnostic. Do not
upload the original, assets, generated source or game executable to hosted CI.

The next implementation needs the real dialog resource, a guest DLGPROC callback
boundary and controls/message behavior. Modal dialog construction, initialization
and return values must not be bypassed to simulate a renderer selection.
Only an actual game scene with sustained input-driven actions qualifies as a
playability result. Source/evidence are local until a push is verified.

## Primary specifications consulted

- https://learn.microsoft.com/en-us/windows/win32/api/ddraw/nf-ddraw-idirectdraw7-getdeviceidentifier
- https://learn.microsoft.com/en-us/windows/win32/api/ddraw/ns-ddraw-dddeviceidentifier2
- https://learn.microsoft.com/en-us/windows/win32/api/ddraw/nf-ddraw-idirectdraw7-getcaps
- https://learn.microsoft.com/en-us/windows/win32/api/ddraw/ns-ddraw-ddcaps_dx7
- https://learn.microsoft.com/en-us/windows/win32/api/ddraw/nf-ddraw-idirectdraw7-createsurface
- https://learn.microsoft.com/en-us/windows/win32/api/ddraw/nf-ddraw-idirectdrawsurface7-setclipper
- https://learn.microsoft.com/en-us/windows/win32/api/ddraw/ns-ddraw-ddpixelformat
- https://learn.microsoft.com/en-us/windows/win32/api/unknwn/nf-unknwn-iunknown-queryinterface(refiid_void)
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-dialogboxparama
- Mingw-w64 DirectX 7 interface declarations in the separately retained compiler SDK.

No upstream implementation source was copied into this increment.
