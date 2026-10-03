# Verified native Windows windowing checkpoint

This implementation adds a Windows-host USER32/GDI/WGL boundary. It does not add a
headless pretend-window backend, an x86 interpreter, or game-specific API
success stubs. Non-Windows GUI calls remain unsupported.

## Boundary rules

The guest keeps 32-bit pointers and handles. Native HWND/HDC/HGLRC/icon/cursor
objects are held only by the backend, behind process-local typed tokens.
WNDCLASSEXA, CREATESTRUCTA, legacy public MSG (28 bytes), PAINTSTRUCT,
WINDOWPOS, NCCALCSIZE_PARAMS and pointer-free RECT/MINMAXINFO payloads are
marshalled explicitly. Callback scratch reservations expire after the callback.
The window procedure executes through Process.callback and compiled dispatch.
Exceptions are caught before returning across WNDPROC, retained, and rethrown
at the enclosing API boundary. Teardown never executes additional guest code.

The current profile is one GUI-owning guest per native thread, application-owned
top-level ANSI window classes, and a bounded set of message payloads. Unknown
system pointer payloads fail explicitly. Host-only post-Windows-2000 non-client
and accessibility transport is delegated to the native default procedure and
counted in diagnostics; it is not copied as guest pointers. Menus, dialogs,
child/control windows, foreign guest DLLs, arbitrary SendMessage structures,
IME integration and full window subclass semantics are not claimed complete.

PE resource directories are parsed as data, bounded by their declared extent,
entry budget and three-level type/name/language shape. Cycles, duplicate keys,
truncated names and unbacked payloads are rejected. Icon data is handed to the
native icon decoder without loading/executing the original module. Resource
selection is exact name, en-US/neutral, or a single available language; it is
not a universal Windows language-fallback implementation.

## Driver bootstrap, not the game renderer

The backend creates an actual device context and WGL context, sets a native
pixel format, and provides a small immediate-mode OpenGL bootstrap. Pixel
readback is bounded RGBA/UNSIGNED_BYTE with the default pack layout. Texture
and buffer object marshalling, extension coverage, DirectDraw/Direct3D and
faithful E3 rendering are not implemented by this change.

`tests/window_program.py` builds an entirely author-written PE32 fixture. On
Windows the original PE is executed through the Windows loader; the generated
x64 program must also create a real window, pass creation/paint/custom-message
callbacks, load a resource icon, render a triangle, and produce identical pixel
output. Creation and destruction counts and output canaries are checked. The
result is a synthetic driver/window test, never evidence that E3 is playable.

## Completed acceptance

Implementation `b02db47c2dcd5a073679dcf90e2ad99fb0d7133c` passed run
`37162230094`: Windows/MSVC 18 suites (one window fixture plus 17 runtime
suites), Linux/GCC 16 suites, and all three native Windows ASan suites.
The original PE32 and generated x64 framebuffers match exactly (SHA-256
`df59acf2780167d7a0c7192876c77242d1f1694ca4902fd60437f96fe925c69c`).
The fixture created and destroyed one actual window, executed 22 compiled
window callbacks, created one WGL context, and swapped one frame. These counts
belong to the controlled fixture, not the game.

The previous Windows ASan hang was fixed by preserving `/EHsc` even when a
caller replaces `CMAKE_CXX_FLAGS`. Without C++ unwinding, handled memory faults
could retain recursive mutex locks. The public compile requirement, an MSVC
header guard, and 70 nonblocking exception-unwind assertions prevent silent
recurrence. The 60-second GUI timeout and all fault tests remain intact;
527 GUI and 28 resource assertions also passed under Windows ASan.

See `verification/windowing-ci.json` for source/artifact hashes and test scope.
The 50/100 planning checkpoint is unchanged: this is progress within the later
GUI/graphics gate, not acceptance of the entire graphics/audio/input system.

## What will count as playable

A synthetic window or triangle does not qualify. The actual SHA-identified E3
build must load its game content, display a game-generated scene, and sustain
an interactive loop in which player input produces the corresponding in-game
movement/actions. Record the tested host, input identity, duration/scenario,
and remaining defects. An early playable build can still have documented
fidelity or sound defects; that is separate from a complete port.

**E3 is not demonstrated playable here.** This pass reran its deterministic
CFG contract and five isolated routine checks locally (5,120 comparisons), not
its full startup on Windows. The earlier Linux `LoadIconA` stop remains a
non-Windows-backend limitation, not evidence that Windows `LoadIconA` is still
missing. The Windows GUI implementation now has independent synthetic tests;
actual E3 window creation, its selected renderer, game frames and controls
need their own execution evidence. The binary's DirectDraw/DirectInput/DirectSound
imports also mean this OpenGL fixture cannot establish whole-game rendering.

No E3 input or game-derived code is sent to CI. For a private Windows run, use
the SHA-bound `tools/create_project.py` profile with a new output directory,
build that generated CMake project, and retain its process report locally.
Only observed executable callback/indirect targets should inform new seeds;
do not turn unknown APIs into successful no-ops to advance a milestone.

## Primary specifications

- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nc-winuser-wndproc
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-createstructa
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerclassexa
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-loadicona
- https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-wglcreatecontext
- https://learn.microsoft.com/en-us/windows/win32/debug/pe-format
