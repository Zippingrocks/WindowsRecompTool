#pragma once
#include <memory>
#include <string>
namespace wr {
class Process;
// Bounded legacy DirectInput boundary. Native COM pointers and HWNDs remain
// host-only; guest code sees 32-bit tokens and explicitly translated layouts.
class DirectInputBackend {
public:
    virtual ~DirectInputBackend()=default;
    virtual void shutdown() noexcept=0;
    virtual std::string report() const=0;
};
std::unique_ptr<DirectInputBackend> install_directinput(Process&);
}
