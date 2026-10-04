#include "winrecomp/process.hpp"
#include <algorithm>
#include <cctype>
#include <limits>
#include <string_view>
namespace wr {
namespace {
std::string basename(Process& p,const std::string& input) {
    // This profile intentionally does not approximate the host's ambient DLL
    // search, SxS, API sets, PATH, relative paths or device/UNC names.
    if(input.empty() || input.size()>255 || input.back()=='.' || input.back()==' ')
        throw GuestFault(FaultKind::unsupported,p.cpu.eip,"unsupported DLL basename");
    std::string name;
    for(const unsigned char ch:input) {
        if(ch<32 || ch>126 || std::string_view("<>:\"/\\|?*").find(char(ch))!=std::string_view::npos)
            throw GuestFault(FaultKind::unsupported,p.cpu.eip,"DLL lookup accepts ASCII basenames only");
        name.push_back(char(ch>='A' && ch<='Z'?ch+('a'-'A'):ch));
    }
    if(name.find('.')==std::string::npos)name+=".dll";
    return name;
}
std::string fold(std::string name) {
    for(auto& ch:name)if(ch>='A' && ch<='Z')ch=char(ch+('a'-'A'));
    return name;
}
}
U32 Process::load_library(const std::string& input) {
    const auto name=basename(*this,input);
    module_probes.push_back({input,name,{},"searching"});
    if(module_probes.size()>32)module_probes.pop_front();
    auto& probe=module_probes.back();
    if(auto it=modules_.find(name);it!=modules_.end()) {
        if(it->second.references==std::numeric_limits<U32>::max())
            throw GuestFault(FaultKind::unsupported,cpu.eip,"DLL reference count overflow");
        ++it->second.references;probe.outcome="already_loaded";return it->second.handle;
    }
    // Do not return ERROR_MOD_NOT_FOUND based merely on missing implementation.
    // Missing-module behavior is enabled only in an explicitly mounted namespace.
    if(options.dll_search_roots.empty()) {
        probe.outcome="namespace_not_configured";
        throw GuestFault(FaultKind::unsupported,cpu.eip,"DLL search roots not configured for "+name);
    }
    // Bridge libraries form the process's modeled system DLL set. They are not
    // arbitrary native pointers; dynamic exports still require registered thunks.
    const auto prefix=name+"!";
    bool implemented=false;
    for(auto api=api_names_.lower_bound(prefix);api!=api_names_.end() && api->first.starts_with(prefix);++api)
        implemented|=bool(apis_.at(api->second).function);
    if(implemented) {
        if(next_module_>=0xef000000u)throw GuestFault(FaultKind::unsupported,cpu.eip,"DLL token space exhausted");
        const auto handle=next_module_;next_module_+=0x10000;
        modules_.emplace(name,Module{handle,1,false});probe.outcome="system_bridge";return handle;
    }
    for(const auto& root:options.dll_search_roots) {
        probe.searched.push_back(root.generic_string());
        std::error_code error;
        const auto status=std::filesystem::status(root,error);
        if(error || !std::filesystem::is_directory(status)) {
            probe.outcome="search_error";
            throw GuestFault(FaultKind::unsupported,cpu.eip,"DLL search root unavailable: "+root.generic_string());
        }
        std::filesystem::directory_iterator item(root,error),end;
        if(error) {probe.outcome="search_error";throw GuestFault(FaultKind::unsupported,cpu.eip,"cannot inspect DLL search root");}
        unsigned count=0;
        for(;item!=end;item.increment(error)) {
            if(error)break;
            if(++count>65536) {probe.outcome="search_budget";throw GuestFault(FaultKind::budget,cpu.eip,"DLL directory entry limit");}
            if(fold(item->path().filename().string())==name) {
                // A real on-disk candidate must never masquerade as missing or
                // be executed as a host x64 DLL. Guest DLL lifting is future work.
                probe.outcome="present_but_untranslated";
                throw GuestFault(FaultKind::unsupported,cpu.eip,"DLL exists but guest DLL loading is not implemented: "+item->path().generic_string());
            }
        }
        if(error) {probe.outcome="search_error";throw GuestFault(FaultKind::unsupported,cpu.eip,"DLL directory enumeration failed");}
    }
    probe.outcome="absent_from_configured_namespace";set_error(126);return 0;
}
U32 Process::free_library(U32 handle) {
    for(auto it=modules_.begin();it!=modules_.end();++it)if(it->second.handle==handle) {
        auto& module=it->second;
        // Unloading an import dependency would leave its static IAT dangling.
        // Fail explicitly until whole guest DLL dependency lifetimes are modeled.
        if(module.references==1 && module.imported)
            throw GuestFault(FaultKind::unsupported,cpu.eip,"cannot unload final static import dependency");
        if(!--module.references)modules_.erase(it);
        return 1;
    }
    set_error(6);return 0;
}
U32 Process::module_export(U32 handle,const std::string& symbol) {
    const auto name=module_name(handle);
    if(name.empty()){set_error(6);return 0;}
    const auto found=api_names_.find(name+"!"+symbol);
    if(found==api_names_.end())
        throw GuestFault(FaultKind::unsupported,cpu.eip,"unmodelled dynamic export "+name+"!"+symbol);
    const auto& api=apis_.at(found->second);
    if(!api.function)throw GuestFault(FaultKind::unsupported,cpu.eip,"dynamic export has no implementation");
    if(modules_.at(name).imported)return found->second; // final import reference cannot be unloaded
    const auto key=std::make_pair(handle,symbol);
    if(auto old=dynamic_exports_.find(key);old!=dynamic_exports_.end())return old->second;
    if(next_thunk_>=0xf1000000)throw GuestFault(FaultKind::unsupported,cpu.eip,"dynamic thunk space exhausted");
    auto thunk=api;
    thunk.function=[handle,call=api.function](Process& p)->U32{
        if(p.module_name(handle).empty())throw GuestFault(FaultKind::unsupported,p.cpu.eip,"call through unloaded DLL export");
        return call(p);
    };
    const auto address=next_thunk_;
    apis_.emplace(address,std::move(thunk));dynamic_exports_.emplace(key,address);next_thunk_+=16;
    return address;
}
}
