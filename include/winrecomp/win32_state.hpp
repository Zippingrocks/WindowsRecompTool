#pragma once
#include "winrecomp/runtime.hpp"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
#include <deque>
#include <set>
namespace wr {
struct Win32State {
    struct ModuleLookup {std::string name,result;std::vector<std::string> searched;};
    std::deque<ModuleLookup> module_lookups;
    struct Heap {U32 maximum{};std::map<U32,U32> blocks;};
    struct File { std::FILE* stream{};bool owned{},read{},write{};U32 type{1};std::filesystem::path path;U32 share{7}; };
    struct Find {
        std::uintptr_t native{};std::function<void()> close;
        std::vector<std::filesystem::path> entries;std::size_t next{};
        ~Find(){if(close)close();}
    };
    std::map<U32,std::shared_ptr<Find>> finds;
    std::map<U32,Heap> heaps;
    std::map<U32,File> files;
    std::map<U32,U32> tls;
    std::set<U32> tls_used;
    std::map<std::string,std::string> environment;
    std::map<U32,std::size_t> environment_blocks;
    std::map<U32,U32> global_blocks;
    std::set<U32> virtual_reservations;
    std::set<U32> critical_sections;
    std::map<U32,unsigned> critical_depth;
    struct Mutex {
        std::string name;unsigned depth{};std::uintptr_t native{};
        std::function<void()> close;
        ~Mutex(){if(close)close();}
    };
    std::map<U32,std::shared_ptr<Mutex>> mutexes;
    std::map<std::string,std::weak_ptr<Mutex>> mutex_names;
    U32 next_heap{0xd0000000},next_handle{0xc0000000},process_heap{},command_line{},wide_command_line{};
    U32 stdin_handle{},stdout_handle{},stderr_handle{};
    unsigned callback_depth{};
    std::chrono::steady_clock::time_point clock_start=std::chrono::steady_clock::now();
    ~Win32State(){for(auto& [id,file]:files){(void)id;if(file.owned && file.stream)std::fclose(file.stream);}}
};
}
