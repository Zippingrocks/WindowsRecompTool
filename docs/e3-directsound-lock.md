# E3 DirectSound Lock / Unlock checkpoint

E3's channel upload routine was traced in the supplied private input and reaches
legacy `IDirectSoundBuffer::Lock` at guest PC `0x00482A61`, followed by
`Unlock` at `0x00482AB6`.

The observed call is narrow: flags are zero, the byte offset is the game's
sample offset multiplied by two, and the byte count is the sample count
multiplied by two. DirectSound may return a second region when the request wraps
around the end of the circular buffer.

The Windows x64 bridge now models that shape without leaking native pointers.
Native Lock regions remain host-only. WinRecomp allocates bounded guest buffers,
copies the current native bytes into them, and returns only 32-bit guest
addresses/counts. Unlock must present the exact same guest regions and sizes;
the guest bytes are copied back to the native regions before the real
DirectSound Unlock is called.

Nested locks, mismatched Unlock calls, primary-buffer Lock, oversized requests,
and unobserved flags fail closed. Outstanding lock state is also unwound during
backend shutdown.

Native Windows workflow `37705556609` passed normal MSVC and Windows
AddressSanitizer jobs and retained the DirectInput, E3 DirectDraw v1 and GUI
boundaries.

The hosted Windows runner still reports `DSERR_NODRIVER`, so its DirectSound
test exits before successful buffer creation. Therefore the workflow proves the
new code builds cleanly, preserves the no-driver path, and is ASan-clean, but it
does **not** prove the Lock success path on a native Windows audio device.

The success-path regression is present and will automatically verify:
- contiguous sample upload followed by re-lock/readback,
- exact Unlock identity checks,
- a 4096-byte circular-buffer wrap at offset 4000 for 512 bytes (96 + 416),
- rejection of `DSBLOCK_ENTIREBUFFER`.

No proprietary E3 binary, assets, or generated game source are present in CI.
This is not an audio-playback or playability milestone.
