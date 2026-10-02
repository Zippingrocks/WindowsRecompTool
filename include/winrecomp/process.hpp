#pragma once
#include "winrecomp/core.hpp"
#include "winrecomp/compiled.hpp"
#include <functional>
#include <memory>
#include <deque>
#include <filesystem>
namespace wr {
using StepFunction=bool(*)(Cpu&,Memory&,std::uint64_t&);
struct ProcessOptions {
    U32 stack_base{0x07000000},stack_size{0x00100000},teb{0x7ffde000};
    std::uint64_t instruction_budget{10000000};
    std::filesystem::path data_root{"."};
    std::string command_line{"program.exe"},image_name{"program.exe"};
    bool allow_file_write{false};
    std::string user_name{"WinRecomp"};
};
class Process {
public:
    struct Api { std::string dll,name;unsigned arguments{};bool cdecl{};std::function<U32(Process&)> function; };
    struct Transfer { U32 pc{},return_address{};std::string api;std::vector<U32> arguments;std::string detail; };
private:
    std::unique_ptr<struct Win32State> win32_;
    std::map<U32,Api> apis_;
    std::map<std::string,U32> api_names_;
    std::map<std::string,U32> modules_;
    U32 next_thunk_{0xf0000000},next_module_{0xe0000000};
    StepFunction step_{};
    bool loaded_{},exited_{};
    U32 exit_code_{};

public:
    Cpu cpu;
    Memory memory;
    ProcessOptions options;
    U32 image_base{},image_size{},entry{},last_error{};
    std::string input_sha256;
    std::uint64_t budget{},executed_instructions{},api_calls{};
    std::deque<Transfer> recent_transfers;
    explicit Process(StepFunction step,ProcessOptions opts={});
    ~Process();
    Process(const Process&)=delete;Process& operator=(const Process&)=delete;
    void load(const Image& image);

    void register_api(const std::string& dll,const std::string& name,unsigned arguments,std::function<U32(Process&)> fn,bool cdecl=false);
    U32 resolve(const std::string& dll,const std::string& name,bool allow_unimplemented=false);
    U32 module(const std::string& dll,bool create=false);
    std::string module_name(U32 handle) const;
    bool dispatch_api();
    U32 argument(unsigned index);
    U32 callback(U32 address,std::span<const U32> args,bool stdcall=true);
    U32 run();
    void exit(U32 code){exited_=true;exit_code_=code;}
    bool exited() const{return exited_;}
    U32 virtual_alloc(U32 address,U32 size,U32 allocation,U32 protection);
    bool virtual_free(U32 address,U32 size,U32 operation);
    bool virtual_protect(U32 address,U32 size,U32 protection,U32 old_protection);
    U32 allocate_bytes(std::size_t size,unsigned permission=Memory::Read|Memory::Write);
    std::string read_string(U32 address,std::size_t maximum=32768);
    std::u16string read_wstring(U32 address,std::size_t maximum=32768);
    U32 put_string(const std::string& string);
    U32 put_wstring(const std::u16string& string);
    void set_error(U32 value);
    Win32State& state();
    std::string report() const;
};
void install_win32(Process& process);
int run_program(int argc,char** argv,StepFunction step,const char* expected_sha256);
}
