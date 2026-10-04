#include "winrecomp/process.hpp"
#include "winrecomp/win32_state.hpp"
#include <algorithm>
#include <limits>

namespace wr {
namespace {
std::string fold(std::string value) {
    for(auto& c:value)if(c>='A' && c<='Z')c=char(c-'A'+'a');
    return value;
}
std::string library_key(const std::string& name) {
    if(name.empty() || name.size()>255 || name=="." || name=="..")
        throw std::invalid_argument("empty or oversized module name");
    for(unsigned char c:name)if(c<32 || c>=127 || std::string("/\\:*?\"<>|").find(char(c))!=std::string::npos)
        throw std::invalid_argument("only literal ASCII module basenames are supported");
    if(name.back()==' ')throw std::invalid_argument("ambiguous trailing-space module name");
    auto key=fold(name);
    if(key.back()=='.')key.pop_back(); // A trailing dot suppresses the default .dll extension.
    else if(key.find('.')==std::string::npos)key+=".dll";
    return key;
}
}
U32 Process::module(const std::string& name,bool create) {
    std::string key;
    try{key=library_key(name);}catch(const std::invalid_argument&){set_error(126);return 0;}
    if(key==fold(options.image_name))return image_base;
    if(auto it=modules_.find(key);it!=modules_.end())return it->second.handle;
    if(!create){set_error(126);return 0;}
    if(next_module_>=0xef000000u)throw GuestFault(FaultKind::unsupported,cpu.eip,"module handle budget exhausted");
    const auto handle=next_module_;next_module_+=0x10000;
    modules_.emplace(key,Module{handle,1,true});return handle;
}
std::string Process::module_name(U32 handle) const {
    for(const auto& [key,value]:modules_)if(value.handle==handle)return key;
    return {};
}
U32 Process::load_library(const std::string& name) {
    std::string key;
    try{key=library_key(name);}catch(const std::invalid_argument& error){
        throw GuestFault(FaultKind::unsupported,cpu.eip,error.what());
    }
    auto& history=state().module_lookups;
    history.push_back({key,"pending",{}});if(history.size()>32)history.pop_front();
    auto& event=history.back();
    if(auto it=modules_.find(key);it!=modules_.end()) {
        if(it->second.references==std::numeric_limits<U32>::max()) {
            event.result="reference_count_overflow";set_error(8);return 0;
        }
        ++it->second.references;event.result="existing_bridge";return it->second.handle;
    }
    // A registered, owned API bridge is not an arbitrary native DLL. No host
    // function pointer or original x86 DLL code is admitted through this path.
    const bool bridge=std::any_of(apis_.begin(),apis_.end(),[&](const auto& entry){
        return entry.second.dll==key && bool(entry.second.function);
    });
    if(bridge) {
        if(next_module_>=0xef000000u)throw GuestFault(FaultKind::unsupported,cpu.eip,"module handle budget exhausted");
        const auto handle=next_module_;next_module_+=0x10000;
        modules_.emplace(key,Module{handle,1,false});event.result="registered_bridge";return handle;
    }
    // Missing optional libraries are meaningful only in an explicitly declared
    // guest namespace. No directories means unknown, NOT "file not found".
    // This is deliberately not the Windows host's PATH/SxS/registry search.
    if(options.dll_directories.empty()) {
        event.result="search_unconfigured";
        throw GuestFault(FaultKind::unsupported,cpu.eip,"module lookup requires explicit --dll-dir search roots for "+key);
    }
    try {
        for(const auto& directory:options.dll_directories) {
            event.searched.push_back(directory.generic_string());
            for(const auto& item:std::filesystem::directory_iterator(directory)) {
                if(fold(item.path().filename().string())!=key)continue;
                event.result="present_untranslated";
                throw GuestFault(FaultKind::unsupported,cpu.eip,"module present but has no compiled bridge: "+key);
            }
        }
    }catch(const std::filesystem::filesystem_error&){
        event.result="search_failed";
        throw GuestFault(FaultKind::unsupported,cpu.eip,"cannot establish module absence: directory search failed for "+key);
    }
    event.result="absent_in_declared_namespace";set_error(126);return 0;
}
U32 Process::free_library(U32 handle) {
    for(auto it=modules_.begin();it!=modules_.end();++it)if(it->second.handle==handle) {
        auto& value=it->second;
        if(value.imported && value.references==1)
            throw GuestFault(FaultKind::unsupported,cpu.eip,"unloading the final static-import module reference is unsupported");
        if(!--value.references)modules_.erase(it);
        return 1;
    }
    set_error(6);return 0;
}
}
