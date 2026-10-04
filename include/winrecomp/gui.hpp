#pragma once
#include <memory>
#include <cstdint>
#include <stdexcept>
#include <string>
namespace wr {
class Process;
// OS objects and native pointers stay exclusively in the host backend. Only
// typed 32-bit tokens and explicit legacy structure layouts cross into guest RAM.
class GuiBackend {
public:
    virtual ~GuiBackend()=default;
    virtual void shutdown() noexcept=0;
    virtual std::string report() const=0;
    // Host-only integration boundary; never place this value in guest memory.
    virtual std::uintptr_t native_window(std::uint32_t) {throw std::runtime_error("native window lookup unavailable");}
};
// A non-Windows host returns an unavailable backend: it must never pretend that
// a window/context exists. Headless/null rendering is not a fidelity mode.
std::unique_ptr<GuiBackend> install_gui(Process& process);
}
