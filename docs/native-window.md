# Native Windows window boundary (development)

This is work beyond the existing 50-point checkpoint. No additional roadmap
points or playable-game status are assigned by adding this backend.

Generated programs accept `--gui`. With it the Windows backend uses real USER32
windows; without it, GUI operations stop explicitly. Non-Windows hosts reject
native GUI operations even with `--gui`; there is no pretend/headless success.
The existing Linux E3 startup observation therefore does not demonstrate a game
window, and the E3 game has not been run on a Windows host in this change.

The initial profile covers one owning host thread, guest-registered top-level
ANSI windows without menus/class extra bytes, shared icons (stock or a copied
PE group-icon resource), stock cursors/brushes, message retrieval/dispatch,
scalar application messages, window rectangle/text operations, and balanced
paint operations. Child/control/dialog windows, arbitrary class resources,
arbitrary messages and renderers are not implemented by this boundary.

The bridge stores all native handles in a typed table. CREATESTRUCTA (48 bytes),
MSG (28), PAINTSTRUCT (64), MINMAXINFO (40), WINDOWPOS (28) and NCCALCSIZE_PARAMS
(52 plus WINDOWPOS) are explicitly marshalled using guest layouts. Pointer-bearing
messages are allowlisted; unknown messages fail instead of truncating host pointers.
Guest calls to DefWindowProc synchronize their active native message frame.
Exceptions are contained at WNDPROC and rethrown at the controlled API boundary.
Shutdown disables guest callbacks while destroying owned native windows/icons.

Resource tables are parsed from the already validated input bytes with depth,
entry-count, cumulative-byte and file-backed-range limits. IDs and UTF-16 names
are distinct. Language lookup is intentionally deterministic (exact, neutral,
US English, first present), not Windows' entire user/thread language algorithm.

`tests/gui_program.py` builds an author-written original PE32 and its statically
translated x64 counterpart. On Windows both must really create/show/paint a window,
process WM_APP through their window procedure, destroy the window and produce an
identical 16-byte record. Native handle values and incidental message counts are
not compared. On Linux the generated project is built and explicit GUI rejection
is tested; native window execution is reported as not run, never as passed.
`tests/gui.cpp` also injects a deliberate callback exception to check containment.

References (behavior contracts, not copied implementations):
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-createwindowexa
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nc-winuser-wndproc
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-createiconfromresourceex
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-dispatchmessagea
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-createstructa

A native window/paint cycle is not a rendered game frame or verified controls.
`gui_report` keeps `rendered_game_frames: 0` and `playable_verified: false` until
separate implementation and actual-game evidence justify changing those fields.
