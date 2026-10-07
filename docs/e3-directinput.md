# E3 DirectInput checkpoint

The E3-focused Windows x64 branch now has a native-Windows-verified bounded
legacy DirectInput v3 bridge.

Tested source: `e8323522bb38892c44c7659152295ee29046c502`.
Native Windows workflow: `37680286857`.

The accepted slice covers `DirectInputCreateA` version 3.00, the system mouse
and keyboard, foreground/nonexclusive cooperative level, explicit x86
`DIDATAFORMAT` translation, the observed mouse granularity property,
Acquire/Unacquire and bounded GetDeviceState reads. Native COM pointers, host
HWNDs and native data-format pointers never enter guest memory.

For modern Windows SDK compatibility, the backend lazily resolves
`DirectInputCreateA` from `dinput.dll`; it does not require the removed
legacy `dinput.lib` import library.

Both normal MSVC and Windows AddressSanitizer jobs passed, retaining the E3
DirectDraw v1 and renderer/runtime boundary checks. This does not mean E3 is
playable or that a game-generated frame has been rendered on native Windows.
