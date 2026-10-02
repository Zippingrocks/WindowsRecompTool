#include "winrecomp/process.hpp"
#include "winrecomp/gui.hpp"
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
#define CHECK(x) do{if(!(x))throw std::runtime_error("GUI check failed: " #x);}while(0)
namespace {
wr::Image image(){wr::Image i;i.base=0x400000;i.entry=0x401000;i.size=0x3000;i.headers_size=512;i.bytes.resize(1536);i.sections={{".text",0x1000,512,512,512,0x60000020},{".data",0x2000,512,1024,512,0xc0000040}};i.bytes[512]=0xc3;return i;}
bool step(wr::Cpu&,wr::Memory&,std::uint64_t&){throw std::runtime_error("deliberate guest callback failure");}
wr::U32 call(wr::Process& p,const std::string& name,std::initializer_list<wr::U32> args={}){
    auto pc=p.cpu.eip;for(auto it=args.end();it!=args.begin();)wr::push(p.cpu,p.memory,*--it);wr::push(p.cpu,p.memory,pc);p.cpu.eip=p.resolve("user32.dll",name);CHECK(p.cpu.eip);CHECK(p.dispatch_api());return p.cpu.r[wr::EAX];
}
}
int main(){try{
    {wr::Process p(step);p.load(image());bool caught=false;try{call(p,"LoadIconA",{0,32512});}catch(const wr::GuestFault& e){caught=e.kind==wr::FaultKind::unsupported && std::string(e.what()).find("--gui")!=std::string::npos;}CHECK(caught);}
    wr::ProcessOptions options;options.enable_gui=true;wr::Process p(step,options);p.load(image());
#ifndef _WIN32
    bool caught=false;try{call(p,"LoadIconA",{0,32512});}catch(const wr::GuestFault& e){caught=e.kind==wr::FaultKind::unsupported && std::string(e.what()).find("requires Windows")!=std::string::npos;}CHECK(caught);
    std::cout<<"GUI opt-in and non-Windows fail-closed behavior passed; native window test not run on Linux\n";
#else
    auto icon=call(p,"LoadIconA",{0,32512});CHECK(icon);CHECK(icon==call(p,"LoadIconA",{0,32512}));CHECK(!call(p,"LoadIconA",{p.image_base,101}));CHECK(p.last_error==1814);
    const auto name=p.put_string("WinRecompFailureContainment");const auto wc=p.allocate_bytes(48);p.memory.store(wc,48,32);p.memory.store(wc+8,0x401000,32);p.memory.store(wc+20,p.image_base,32);p.memory.store(wc+40,name,32);
    CHECK(call(p,"RegisterClassExA",{wc}));
    bool caught=false;try{call(p,"CreateWindowExA",{0,name,name,0x00cf0000,0,0,320,240,0,0,p.image_base,0});}catch(const std::runtime_error& e){std::cerr<<"contained callback failure: "<<e.what()<<'\n';caught=std::string(e.what()).find("deliberate guest callback failure")!=std::string::npos;}CHECK(caught);
    // A deliberately failing guest exception must return to the C++ caller;
    // it must never escape through USER32 or be swallowed by its WNDPROC frame.
    CHECK(::FindWindowA(nullptr,"WinRecompFailureContainment")==nullptr);
    std::cout<<"native GUI opt-in, shared icon, missing resources and callback exception containment passed\n";
#endif
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
