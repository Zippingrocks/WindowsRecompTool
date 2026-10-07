#pragma once
#include <memory>
#include <string>
namespace wr {
class Process;
// Bounded legacy DirectSound boundary. Native COM pointers and HWNDs remain
// host-only; guest code receives 32-bit wrapper tokens and translated layouts.
class DirectSoundBackend {
public:
    virtual ~DirectSoundBackend()=default;
    virtual void shutdown() noexcept=0;
    virtual std::string report() const=0;
};
std::unique_ptr<DirectSoundBackend> install_directsound(Process&);
}
