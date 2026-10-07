# E3 DirectSound checkpoint

The E3-focused Windows x64 runtime now has a bounded legacy DirectSound bridge.

Tested source: `615fe66cc7c466bef39617b91d3106465c1f26c9`.
Native Windows workflow: `37704812368`.

The bridge accepts E3's ordinal-1 DirectSound creation import and models the
observed initialization shape: cooperative level, legacy caps, 32-bit
`DSBUFFERDESC`/PCM translation, primary 3D buffer setup, 3D-listener wrapping,
secondary buffers, 3D-buffer wrapping, common buffer controls, and the observed
3D setters. The EAX/IKsPropertySet probe intentionally returns
`E_NOINTERFACE` so E3 follows its own fallback instead of seeing fake EAX
support.

The hosted Windows runner does not expose a DirectSound output device. Its
`DirectSoundCreate` takes the `DSERR_NODRIVER` path. That path is now
verified to propagate honestly, create no fake guest object, report the
DirectSound backend, and remain clean under Windows AddressSanitizer. Retained
DirectInput, E3 DirectDraw v1 and GUI boundaries also passed.

This is therefore **native-Windows error-path and memory-safety evidence**, not
proof of successful DirectSound playback. A Windows machine with an actual
audio device is still required to accept the successful create/buffer/3D path.

Streaming `IDirectSoundBuffer::Lock/Unlock` is intentionally outside this
checkpoint and is a likely next audio target. No proprietary E3 binary, assets
or generated game source are present in CI, and this is not a playability
milestone.
