# Indexed triangle lists: Windows x64 candidate

This extends the existing opt-in `--legacy-renderer d3d9` path. The guest's
IDirect3DDevice7::DrawIndexedPrimitive call retains WORD (16-bit) indices and
32-bit pointers; owned copies are passed to native D3D9 DrawIndexedPrimitiveUP.
No guest pointer is sent to a graphics driver.

Admitted: triangle lists, transformed/diffuse vertices (FVF 0x44), or the same
vertices with one texture coordinate (FVF 0x144), flags zero. Counts are limited
to 65,535 vertices and indices, additionally bounded by actual host maximum
index/primitive capabilities. Sparse/nonzero-minimum indices, shared vertices,
and degenerate triangles are supported. Indices outside the declared vertex
range, truncated buffers, wrapped addresses, excessive counts, and invalid
referenced coordinates are rejected. Unreferenced payloads are not geometry.

Input copies, count/range checks and finite referenced-coordinate checks occur
before GPU submission. Textured draws retain the existing post-bind edit and
ownership behavior. The success-only d3d9_indexed_draws counter distinguishes
this new path from unindexed drawing.

The two authored original PE32 fixtures use actual Direct3D7. Their generated
Windows x64 counterparts use D3D9. Native Windows acceptance requires every
RGB pixel (4096 per output) to match, including a post-bind texture edit and
retained texture references; each path runs twice. The original unindexed
fixtures must remain byte-identical. New C++ boundary tests cover high WORD
indices (65532..65534), guarded guest buffers, invalid requests, unchanged
pixels after rejection, indexed UVs and resource lifetimes. The portable
index validator has a separate deterministic randomized reference check.

This is not depth buffering, presentation, a complete renderer, or an E3 run.
The original/generated display-mode mismatch remains separately unresolved
(issue #2); targeted renderer acceptance does not imply that all tests pass.
Native Windows results must be recorded for the exact candidate before this
component is called verified. Development remains off main.

Specifications and ABI reference (no implementation copied):
- https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-drawindexedprimitiveup
- https://github.com/mingw-w64/mingw-w64/blob/master/mingw-w64-headers/include/d3d.h
