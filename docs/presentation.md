# Native Windows presentation checkpoint

The explicit `--legacy-renderer d3d9` backend now has a verified, bounded
windowed presentation path on native Windows x64.

The tested implementation is `4d5c69f54f1906631ac4306fd9c451d3ee882e59`.
GitHub Actions run `37230044412` completed successfully for source publication,
native Windows/MSVC verification, and the Windows AddressSanitizer boundary job.

The guest-facing operation remains DirectDraw/Direct3D 7 shaped: a primary
surface blit is checked against the owning render target and translated into a
real Direct3D 9 swap-chain Present. Presentation is allowed only outside an
active scene. Foreign roots, invalid rectangles, scaling, unsupported effects,
and unowned targets remain failures rather than silent success.

The accepted profile is intentionally narrow: an exact-size windowed client.
Fullscreen switching, scaling and broad legacy blit semantics are not accepted.

This is component evidence. It does **not** establish that E3 has rendered one
of its own frames on native Windows, and it does not establish playability.
Issue #2 display-mode parity remains open.

See `verification/presentation-native.json` for the machine-readable scope.
