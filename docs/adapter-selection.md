# Native Windows DirectDraw adapter-selection checkpoint

The bounded Windows x64 D3D7-on-D3D9 bridge now preserves DirectDraw adapter
identity instead of treating every non-null GUID as adapter 0.

Tested source: `1dcccce58af82453cc3a7174665bb413c90e1837`.
Native Windows workflow: `37248311588`.

DirectDraw enumeration retains each non-null adapter GUID together with its
native HMONITOR. D3D9 adapter 0 exposes its own native monitor identity. A later
DirectDrawCreate/DirectDrawCreateEx request using a non-null GUID is accepted
only when that GUID was actually enumerated and its monitor is exactly the D3D9
adapter-0 monitor. Arbitrary GUIDs and special DirectDraw adapter sentinels are
still rejected by the bounded D3D9 profile.

Both native Windows/MSVC and Windows AddressSanitizer checks passed. The private
E3 diagnostic also used this path successfully and advanced past adapter
selection into its Choose Rasterizer dialog; that Wine result is diagnostic
only, not Windows acceptance or gameplay evidence.
