#include "winrecomp/process.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace {
unsigned checks{};
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("module assertion: " #x);}while(0)
wr::Image image(){wr::Image i;i.base=0x400000;i.entry=0x401000;i.size=0x3000;i.headers_size=512;i.bytes.resize(1024);i.bytes[512]=0xc3;i.sections={{".text",0x1000,512,512,512,0x60000020}};return i;}
bool step(wr::Cpu& c,wr::Memory& m,std::uint64_t&){if(c.eip!=0x401000)return false;c.eip=wr::pop(c,m);return true;}
wr::U32 call(wr::Process& p,wr::U32 address,std::initializer_list<wr::U32> args={}){
    const auto saved=p.cpu;
    try {for(auto i=args.end();i!=args.begin();)wr::push(p.cpu,p.memory,*--i);wr::push(p.cpu,p.memory,saved.eip);p.cpu.eip=address;CHECK(p.dispatch_api());CHECK(p.cpu.r[wr::ESP]==saved.r[wr::ESP]);CHECK(p.cpu.eip==saved.eip);return p.cpu.r[wr::EAX];}
    catch(...){p.cpu=saved;throw;}
}
wr::U32 call(wr::Process& p,const char* name,std::initializer_list<wr::U32> args={}){return call(p,p.resolve("kernel32.dll",name),args);}
wr::U32 load(wr::Process& p,const std::string& name){return call(p,"LoadLibraryA",{p.put_string(name)});}
template<class F>void fault(F&& fn){bool caught=false;try{fn();}catch(const wr::GuestFault& f){CHECK(f.kind==wr::FaultKind::unsupported);caught=true;}CHECK(caught);}
struct Temp {
    std::filesystem::path path=std::filesystem::temp_directory_path()/("wr-module-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp(){CHECK(std::filesystem::create_directory(path));}
    ~Temp(){std::error_code error;std::filesystem::remove_all(path,error);}
};
void run(){
    Temp a,b;wr::ProcessOptions o;o.dll_search_roots={a.path,b.path};wr::Process p(step,o);p.load(image());
    const std::string missing="WinRecomp_Absent_6CE3129F_9618_472D.dll";
    p.set_error(19);CHECK(load(p,missing)==0);CHECK(p.last_error==126);CHECK(p.memory.load(p.options.teb+0x34,32)==126);
    CHECK(p.module_probes.back().searched.size()==2);CHECK(p.module_probes.back().outcome=="absent_from_configured_namespace");
    CHECK(load(p,"WinRecomp_Absent_6CE3129F_9618_472D")==0);
#ifdef _WIN32
    // An independent host-OS missing-file check, never a proof of all search order.
    ::SetLastError(19);auto native=::LoadLibraryA((a.path/missing).string().c_str());CHECK(!native);CHECK(::GetLastError()==p.last_error);
#endif
    // An unimplemented import-name placeholder is not a usable dynamic bridge.
    p.resolve("placeholder.dll","Unknown",true);
    CHECK(load(p,"placeholder.dll")==0 && p.last_error==126);
    CHECK(p.module("placeholder.dll")==0);
    std::ofstream(b.path/"MixedCase.DLL")<<"present but not translated";
    fault([&]{load(p,"mixedcase.dll");});CHECK(p.module_probes.back().outcome=="present_but_untranslated");
    CHECK(p.module_probes.back().searched.size()==2);
    CHECK(std::filesystem::file_size(b.path/"MixedCase.DLL")==26);
    std::filesystem::create_directory(a.path/"directory.dll");fault([&]{load(p,"directory.dll");});
    for(const std::string name:std::vector<std::string>{"", "../escape", "sub/path", "sub\\path", "C:\\Windows\\kernel32.dll", "bad*", "bad?", "trailing.", "trailing ", std::string("bad\0name",8)})fault([&]{p.load_library(name);});
    wr::Process unspecified(step);unspecified.load(image());fault([&]{load(unspecified,missing);});
    // Loaded modules are preferred, do not rescan files or clobber last error.
    auto imported=p.module("kernel32.dll",true);p.set_error(55);
    CHECK(load(p,"KERNEL32")==imported);CHECK(p.last_error==55);
    CHECK(call(p,"FreeLibrary",{imported})==1);fault([&]{call(p,"FreeLibrary",{imported});});
    CHECK(p.module_name(imported)=="kernel32.dll");
    const auto name=p.put_string("GetLastError");CHECK(call(p,"GetProcAddress",{imported,name})==p.resolve("kernel32.dll","GetLastError"));
    fault([&]{call(p,"GetProcAddress",{imported,p.put_string("unknown_export")});});
    // Authored bridge: repeated loads share a handle; last release expires its
    // handle AND issued call thunk. A later load must not revive stale thunks.
    p.register_api("fixture.dll","Probe",0,[](auto&)->wr::U32{return 0x24681357;});
    auto h=load(p,"FiXtUrE"),h2=load(p,"fixture.dll");CHECK(h==h2 && h);
    auto symbol=p.put_string("Probe"),fp=call(p,"GetProcAddress",{h,symbol});
    CHECK(fp && fp!=p.resolve("fixture.dll","Probe"));CHECK(call(p,fp)==0x24681357);
    CHECK(call(p,"FreeLibrary",{h})==1);CHECK(call(p,fp)==0x24681357);
    CHECK(call(p,"FreeLibrary",{h})==1);CHECK(p.module_name(h).empty());fault([&]{call(p,fp);});
    CHECK(call(p,"FreeLibrary",{h})==0 && p.last_error==6);
    CHECK(call(p,"GetProcAddress",{h,symbol})==0 && p.last_error==6);
    auto h3=load(p,"fixture.dll");CHECK(h3!=h);fault([&]{call(p,fp);});
    CHECK(call(p,call(p,"GetProcAddress",{h3,symbol}))==0x24681357);CHECK(call(p,"FreeLibrary",{h3})==1);
    std::filesystem::remove_all(b.path);fault([&]{load(p,missing);});CHECK(p.module_probes.back().outcome=="search_error");
    // Changes after one negative lookup must be observed, not cached as missing.
    std::ofstream(a.path/missing)<<"new candidate";fault([&]{load(p,missing);});
    CHECK(p.module_probes.back().outcome=="present_but_untranslated");
    CHECK(p.report().find("explicit_guest_roots")!=std::string::npos);
}
}
int main(){try{run();std::cout<<checks<<" module namespace/lifetime assertions passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
