# DLL fallback and CPU-detection startup candidate

This is a local Windows-x64 development candidate, not accepted native Windows
execution or gameplay. It starts from the published `work/windows-startup`
commit `9c7e077ed8615cdad2da4c847cf2b173084e7117`. That branch's last CI run
`37170800794` is **overall failed**: its original/generated Windows graphics
comparison failed while the other Windows regressions, Windows ASan boundary
job and Linux job passed. Those are historical results, not validation of
this candidate. Main was still `28903ea` when this work started.

## Explicit DLL namespace; not the host Windows search order

The repeatable `--dll-root directory` option mounts ordered guest lookup roots.
The roots must exist and be readable. Missing-module results are not inferred
from the absence of an API implementation. Without this option an unknown
LoadLibraryA request continues to stop explicitly.

The implemented profile accepts ASCII DLL basenames, optionally adding `.dll`,
and performs case-insensitive lookup. It uses already-loaded guest modules
first and registered, implemented bridge libraries as a modeled system set.
It then checks every configured root. Only an actually absent name returns
NULL and ERROR_MOD_NOT_FOUND (126), reflected in the guest last-error field and
TEB. An existing file/directory/symlink candidate stops as untranslated; it is
not executed as host code, omitted from the lookup, or labeled missing.
Missing roots, enumeration errors, invalid/path-based names and excessive
searches stop instead of returning a guessed result. The final 32 probe records
are retained in the process report.

This **does not implement universal Windows DLL search order**: system folders,
PATH, SxS redirection, API sets, application/current-directory ordering, Unicode
case folding, relative/absolute DLL paths and arbitrary guest DLL initialization
are outside this bounded namespace. The configured absence of Glide is not a
claim it is absent everywhere on the user's computer.

Loaded libraries have reference counts. A final dynamic bridge release retires
its token; previously issued export thunks reject execution and do not revive
on a later reload. Releasing the last imported dependency stops until static
IAT/dependency unloading is supported. Unknown export names or ordinals stop;
GetProcAddress does not invent host function pointers or success stubs.

## Explicit scalar CPU profile

PUSHF/PUSHFD and POPF/POPFD now use checked guest stack operations and user-mode
CPL3/IOPL0 semantics. Status flags, direction and the writable ID bit are handled
at the correct width. IF/IOPL are not writable by this guest. RF/VM are not saved
in flag images. Unsupported trap, nested-task, alignment-check and virtual-8086
modes fail without consuming the stack or changing flags. Reading an undefined
arithmetic flag retains the model's chosen value; this is not a guarantee of
matching unspecified physical-CPU flag values.

CPUID **requires explicit `--cpu-profile scalar-v1`**. The default still stops.
The profile has the transparent vendor `WinRecompCPU`, maximum basic leaf 1,
FPU and CMOV capability bits, and a hypervisor identification leaf. It does not
advertise MMX, 3DNow, SSE/SSE2, AVX, XSAVE, TSC or other optional instruction
families as complete merely because the host supports them. Some SIMD forms
can already be translated, but the full families are deliberately unadvertised.
IsProcessorFeaturePresent is consistent with this profile. Unsupported leaves
return the profile's specified zero result. No guest instruction or branch is
patched. This profile selects a supported generic CPU path; it is not claimed
to match the original demonstration machine or the user's physical processor.
Guest threading, hardware interrupt/trap delivery and a complete virtual CPU
are not implemented by this change.

## Controlled tests and actual E3 result

The FLAGS test compares 8,192 controlled states per build against uninterrupted
native POPF/POPFQ and PUSHFQ sequences. It tests the user-mode flags rules, not
whole-game original-i386 execution. Unsafe TF/NT/AC settings are never enabled
on the host. Safe rejection/atomicity and the deterministic CPUID contract
have separate assertions. The seven new decoded-x86 flag fixtures also run
through the emitter and independent Unicorn comparison.

The DLL tests exercise refcounts, live and stale exports, two search roots,
case folding, new file insertion, missing/unavailable roots, invalid names,
unimplemented bridge placeholders and guest stack/last-error state. A compiled
x86 fixture executes the load/failure/dynamic-export/release/CPU sequence in
64 positive scenarios and three fail-closed configurations. This fixture is
not an original-versus-Windows CPUID comparison: the selected CPU is explicit.

With the actual E3 input and a private writable copy of its assets, the rebuilt
Windows AMD64 program runs through the configured missing-Glide branch, CPUID
detection and DirectDraw adapter callback to **IDirect3D7 QueryInterface**.
Wine's native implementation returns that interface successfully. WinRecomp
then reports the real unsupported IID rather than fabricating E_NOINTERFACE.
The IID is `f5049e77-4861-11d2-a407-00a0c90629a8`.

The SHA-bound profile gains two individually observed executable targets,
`0x004639E0` and `0x004630C0`; the second is the observed adapter callback. The
original executable is unchanged. More admitted instructions do not establish
complete discovery or execution of every path.

Example explicit runtime configuration after the Windows-target build:

```powershell
.\blam_x64.exe "D:\private-e3\blam.exe" --root "D:\private-e3" --dll-root "D:\private-e3" --cpu-profile scalar-v1 --allow-write --report process.json --budget 100000000
```

This command permits writes in the chosen data root; use a private copy of the
original assets and executable. The runtime is not a security sandbox. The
build-only driver still never launches the guest or enables these options.

There is **no E3 game frame, buffer swap, blit or player-control result**.
Current execution evidence is a Windows-target EXE under Wine 10 on Linux,
not native Windows. A present fake/plaintext Glide candidate deliberately
stops before fallback; no candidate file is loaded. Native Windows validation
and the Direct3D7 wrapper are still required.

## Primary specifications

- https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibrarya
- https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order
- https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-freelibrary
- https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getprocaddress
- https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html
- https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-isprocessorfeaturepresent
