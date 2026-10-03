# Native windowing candidate

This branch adds a Windows-host USER32/GDI/WGL boundary. It does not add a
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

Native acceptance is pending until the branch's Windows CI actually passes.
The 50-point checkpoint on main is not raised by this candidate documentation.

## Primary specifications

- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nc-winuser-wndproc
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-createstructa
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerclassexa
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-loadicona
- https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-wglcreatecontext
- https://learn.microsoft.com/en-us/windows/win32/debug/pe-format
