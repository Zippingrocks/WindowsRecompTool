# E3 WM_MOUSEACTIVATE checkpoint

The private E3 Windows-x64 diagnostic reached a GUI marshalling blocker while
dismissing the rasterizer chooser: `WM_MOUSEACTIVATE` (0x0021).

This branch adds an explicit Win32 translation for that message. The native
`wParam` HWND is converted to the guest's existing 32-bit window token; the
packed `lParam` hit-test/mouse-message value is treated as a scalar. No native
HWND is exposed to guest state.

A native Windows regression injects `WM_MOUSEACTIVATE` with host
`SendMessageA` and checks that the guest callback receives the guest window
token and the same packed scalar.

Candidate branch: `work/e3-mouseactivate`.
Verification workflow: `37695062703`.

Both the normal native-Windows/MSVC job and Windows AddressSanitizer job passed.
The retained DirectInput, E3 DirectDraw v1 and D3D9 alpha-test boundaries also
passed in workflow `37695062703`.

The private E3 executable/assets/generated game source were not uploaded to CI.
This is not a rendered-frame or playability milestone.
