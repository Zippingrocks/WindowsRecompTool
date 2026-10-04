# Development status: first native Windows x64 D3D7-on-D3D9 rendering proof

**E3 is not demonstrated playable.** This checkpoint proves a bounded renderer
path on native Windows, not a new actual-game run. The original 50/100 planning
points remain unchanged; they are not a measurement of unknown remaining work.

## What now works on native Windows

Implementation: `20394a1446971c67ae18ae44e1439b6c2baf69f9`.
Development branch: `work/d3d9-backend`.
Select the explicit `--legacy-renderer d3d9` profile; native remains the default.

An authored original x86 PE32 program executes real legacy D3D7. Its compiled
x64 counterpart executes the same guest calls through our native D3D9 HAL
backend. Both exit zero and produce identical 64x64 low-24-bit RGB arrays:
4,096 pixels, including 1,176 triangle pixels and 2,920 background pixels.
SHA-256: `c048e32793d8e6d82ca5ab74314b0317a7227a7f9668599c939157b366b3828b`.
The unused X byte is excluded equally by the original and generated fixtures;
no image region is omitted. This is an offscreen target, not a presented frame.

The generated fixture creates one owned window, one D3D9 device, clears once,
draws once and performs two readbacks. No x86 rendering helper process or
interpreter is used by the generated runtime. No E3 data is used in these tests.

## Verification: passing component, incomplete expanded integration

Run `37213956877` passed all jobs: the two new Windows renderer suites plus
12 selected Windows runtime/modal suites, three Windows ASan suites, and all
20 Linux suites plus 15,168 native-i386 comparisons across 237 fixtures.

The expanded run `37214265230` and diagnostic repeat `37214817744` each
passed both renderer suites, 13 of 14 other selected Windows suites, all four Windows ASan suites, and all 20 Linux
suites. **They failed `directdraw_program` at the original/generated legacy
mode-record comparison. These expanded runs are not green.** That test and its
exact equality assertion remain intact. Issue #2 records the exact mismatch
(39 x86 callbacks versus 13 x64), while both programs' surface pixels still match. This is not a full 35-suite Windows
acceptance or permission to promote the accumulated candidate to main.

Windows ASan passed 294 D3D9 bridge assertions, 130 default-DirectDraw
assertions, 28 resource assertions and 70 unwind assertions. These native C++
checks do not imply whole-game or separately generated-project sanitizer coverage.
Local Linux Release passed all 20 suites in completed batches; one interrupted
combined invocation was not counted as a pass. A Windows-target C++ cross-build
also completed, but no local Wine or actual E3 run occurred in this pass.

The first failed renderer run `37213532652` exposed a bare controlling-IUnknown
pointer retained after releasing its reference. The fix retains native identity
references while they are used as cooperative-window keys, through backend
teardown. No test assertions or timeouts were weakened for the fix.

## Remaining Windows compatibility gaps

The default display-mode mismatch is preserved independently of this new
opt-in renderer. Prior native probe run `37177109318` showed legacy x86 hosts
exposing 8/16/32-bit modes while the tested x64 hosts exposed only 32-bit modes.
These are scoped runner observations, not all Windows installations or proof
that every mode-list difference has the same cause. We do not invent extra
modes that the renderer cannot actually support just to pass the old test.

The D3D9 renderer currently supports only the default adapter, an owned normal
window, X8R8G8B8 targets, transformed-diffuse untextured triangle lists and a
bounded set of states. Texture/depth resources, indexed geometry, broad state
coverage, presentation/fullscreen and E3 rendering remain unfinished. Capabilities
do not advertise those missing features. CPU/DirectDraw writes between scenes
are synchronized through real D3D9 upload/readback as a correctness-first path.

## Preservation

The readable implementation and permanent CI are published on the development
branch. All 93 recorded source hashes were restored from a remote CI Git bundle,
compared byte-for-byte with the locally tested source, and Git integrity checked.
Main remains at the earlier `28903ea` checkpoint pending integration. Issues and
`verification/d3d9-native.json` retain both the passing proof and failed checks.
Historical runtime, windowing, modal and real-input evidence remains separate.
No game binary, assets, disassembly, generated game source or game executable
was committed or uploaded. No new E3 frame, sound, input or gameplay claim.
