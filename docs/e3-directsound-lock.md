# E3 DirectSound Lock/Unlock checkpoint

The E3-focused Windows x64 DirectSound bridge now implements guest-safe
`IDirectSoundBuffer::Lock` / `Unlock` staging.

Tested source: `6bf867d2af99db39536125459c838b87c6c3ae3a`.
Native Windows workflow: `37709329311`.

Native DirectSound memory pointers are never written into guest state. A
successful Lock is copied into temporary guest 32-bit allocations; guest audio
writes are copied back into the native DirectSound regions before Unlock. The
bridge supports the two-region wraparound form, validates the exact Unlock
pointer/length tuple, rejects nested locks and Release while locked, and retires
outstanding locks during backend shutdown.

The Windows test contains a 2000-byte wraparound round trip: write a pattern
through guest memory, Unlock, Lock the same region again and verify the bytes.
It also verifies that a mismatched Unlock tuple is rejected.

The hosted Windows runner has previously reported `DSERR_NODRIVER`. CTest
suppresses successful test stdout, so workflow `37709329311` proves native
Windows compilation, Windows AddressSanitizer cleanliness, and conditional
boundary correctness, but does **not** independently prove that the real
audio-device round-trip branch executed on that particular runner.

No proprietary E3 executable, assets or generated game source are present in
CI. This is not a playback or playability claim.
