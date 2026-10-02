#pragma once
#include <string>
namespace wr {
class Process;
void install_gui(Process& process);
void shutdown_gui(Process& process) noexcept;
std::string gui_report(const Process& process);
}
