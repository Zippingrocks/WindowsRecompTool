#include "winrecomp/process.hpp"
#include "winrecomp/win32_state.hpp"
#include <fstream>
#include <iostream>
#include <random>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace {
unsigned checks{};
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("module assertion: " #x);}while(0)
bool step(wr::Cpu&,wr::Memory&,std::uint64_t&){return false;}
wr::Image image(){wr::Image im;im.base=0x400000;im.entry=0x401000;im.size=0x3000;im.headers_size=512;im.bytes.resize(1024);im.bytes[512]=0xc3;im.sections={{".text",0x1000,512,512,512,0x60000020}};return im;}
wr::U32 call(wr::Process& p,const char* name,std::initializer_list<wr::U32> args) {
    const auto saved=p.cpu;
    try {
        for(auto i=args.end();i!=args.begin();)wr::push(p.cpu,p.memory,*--i);
        wr::push(p.cpu,p.memory,saved.eip);p.cpu.eip=p.resolve("kernel32.dll",name);
        CHECK(p.cpu.eip && p.dispatch_api());CHECK(p.cpu.r[wr::ESP]==saved.r[wr::ESP]);
        CHECK(p.cpu.eip==saved.eip);auto result=p.cpu.r[wr::EAX];p.cpu=saved;return result;
    }catch(...){p.cpu=saved;throw;}
}
template<class F> void unsupported(F action){bool caught=false;try{action();}catch(const wr::GuestFault& e){CHECK(e.kind==wr::FaultKind::unsupported);caught=true;}CHECK(caught);}
struct Root {
    std::filesystem::path path=std::filesystem::temp_directory_path()/("wr-module-"+std::to_string(std::random_device{}()));
    Root(){if(!std::filesystem::create_directory(path))throw std::runtime_error("fresh module directory required");}
    ~Root(){std::error_code error;std::filesystem::remove_all(path,error);}
};
}
int main(){
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
#endif
try{
    Root root;wr::ProcessOptions o;o.data_root=root.path;
    {
        wr::Process p(step,o);p.load(image());
        const auto name=p.put_string("wr_unconfigured_8547e761.dll");
        unsupported([&]{call(p,"LoadLibraryA",{name});});
        CHECK(p.state().module_lookups.back().result=="search_unconfigured");
    }
    o.dll_directories={"."};wr::Process p(step,o);p.load(image());
    auto kernel=p.module("kernel32.dll",true);p.set_error(0x13579);
    const auto k=p.put_string("KERNEL32"),lower=p.put_string("kernel32.dll");
    CHECK(call(p,"LoadLibraryA",{k})==kernel);CHECK(p.last_error==0x13579);
    CHECK(call(p,"LoadLibraryA",{lower})==kernel);CHECK(call(p,"GetModuleHandleA",{k})==kernel);
    CHECK(call(p,"FreeLibrary",{kernel})==1);CHECK(call(p,"FreeLibrary",{kernel})==1);
    CHECK(call(p,"GetModuleHandleA",{lower})==kernel);
    unsupported([&]{call(p,"FreeLibrary",{kernel});});
    CHECK(call(p,"FreeLibrary",{0})==0 && p.last_error==6);
    CHECK(call(p,"FreeLibrary",{0xdeadbeef})==0 && p.last_error==6);
    const auto empty=p.put_string("");CHECK(call(p,"GetModuleHandleA",{0})==p.image_base);
    CHECK(call(p,"GetModuleHandleA",{empty})==0 && p.last_error==126);
    p.register_api("wr_bridge.dll","Answer",0,[](auto&){return 42u;});
    const auto dyn=p.put_string("wr_bridge.dll"),symbol=p.put_string("Answer");
    CHECK(call(p,"GetModuleHandleA",{dyn})==0);
    const auto d1=call(p,"LoadLibraryA",{dyn}),d2=call(p,"LoadLibraryA",{dyn});CHECK(d1 && d1==d2);
    CHECK(call(p,"GetProcAddress",{d1,symbol})==p.resolve("wr_bridge.dll","Answer"));
    CHECK(call(p,"FreeLibrary",{d1})==1);CHECK(call(p,"GetModuleHandleA",{dyn})==d1);
    CHECK(call(p,"FreeLibrary",{d2})==1);CHECK(call(p,"GetModuleHandleA",{dyn})==0);
    CHECK(call(p,"FreeLibrary",{d1})==0 && p.last_error==6);
    const auto d3=call(p,"LoadLibraryA",{dyn});CHECK(d3 && d3!=d1);
    CHECK(call(p,"FreeLibrary",{d3})==1);
    const std::string missing="wr_missing_9e177d4ed3cd4ceea39e145e3e6e9b3b.dll";
    const auto absent=p.put_string(missing);
    CHECK(call(p,"LoadLibraryA",{absent})==0 && p.last_error==126);
    CHECK(p.state().module_lookups.back().result=="absent_in_declared_namespace");
    CHECK(p.state().module_lookups.back().searched.size()==1);
#ifdef _WIN32
    // Independent OS result for a name absent on this runner. Never load a
    // game library or fixture containing code just to obtain a reference.
    SetLastError(0);const auto native=::LoadLibraryA(missing.c_str());
    const auto error=GetLastError();if(native)::FreeLibrary(native);
    CHECK(!native && error==ERROR_MOD_NOT_FOUND);
#endif
    // A present-but-untranslated file MUST NOT be reported missing; otherwise
    // the library resolver could force an incorrect game fallback branch.
    std::ofstream(root.path/"WR_PRESENT.dll")<<"not a DLL";
    auto present=p.put_string("wr_present");unsupported([&]{call(p,"LoadLibraryA",{present});});
    CHECK(p.state().module_lookups.back().result=="present_untranslated");
    std::ofstream(root.path/"no_extension")<<"present without extension";
    auto noext=p.put_string("no_extension.");unsupported([&]{call(p,"LoadLibraryA",{noext});});
    CHECK(call(p,"LoadLibraryA",{p.put_string("no_extension")})==0 && p.last_error==126);
    for(const auto& invalid:{"..","../wr.dll","sub\\wr.dll","C:\\wr.dll","*.dll","wr.dll ","wr.dll\n"})
        unsupported([&]{call(p,"LoadLibraryA",{p.put_string(invalid)});});
    CHECK(p.module("C:\\elsewhere\\kernel32.dll")==0); // no basename alias of a different path
    auto other=root.path/"other";std::filesystem::create_directory(other);
    std::ofstream(other/"second.dll")<<"present in second directory";
    p.options.dll_directories.push_back(other);
    unsupported([&]{call(p,"LoadLibraryA",{p.put_string("second")});});
    CHECK(p.state().module_lookups.back().searched.size()==2);
    std::filesystem::remove_all(other);
    unsupported([&]{call(p,"LoadLibraryA",{absent});});
    CHECK(p.state().module_lookups.back().result=="search_failed");
    CHECK(p.report().find("explicit-directories-and-owned-bridges")!=std::string::npos);
    std::cout<<checks<<" module lookup/refcount/absence assertions passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
