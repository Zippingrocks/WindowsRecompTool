# E3 compatibility slice — native Windows checkpoint

The current E3-focused development branch now has a green native Windows x64
compatibility slice.

Tested source: `9bb99c6978db2825e3a4e5ada6e600c7ea9de950`.
Native Windows workflow: `37587402612`.

The accepted slice covers the E3-observed startup path through canonical
32-bit `CreateFileA` security attributes, rasterizer-dialog callback roots,
legacy `IDirectDraw` exclusive cooperative mode, display-mode switching,
legacy 380-byte `DDCAPS`, one-backbuffer primary flip-chain creation,
backbuffer lookup, color fill and `Flip`.

Both the normal MSVC job and the Windows AddressSanitizer job passed. Retained
renderer checks for the D3D9 compatibility path, alpha blending, alpha testing,
adapter selection and process/runtime boundaries also remained green.

No proprietary E3 executable, assets or generated game source were uploaded to
GitHub Actions. This is a bounded compatibility checkpoint, not an E3 rendered
frame and not a playability claim.

The next step is a private local E3 x64 diagnostic from this exact source to
capture the next concrete unsupported API/COM/state request.
