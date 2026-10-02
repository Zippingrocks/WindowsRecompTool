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
wr::Image icon_image(){
    auto i=image();i.directories[2]={0x2000,512};
    auto w16=[&](unsigned at,unsigned value){i.bytes[1024+at]=std::uint8_t(value);i.bytes[1025+at]=std::uint8_t(value>>8);};
    auto w32=[&](unsigned at,unsigned value){w16(at,value);w16(at+2,value>>16);};
    w16(14,2);w32(16,3);w32(20,0x80000020);w32(24,14);w32(28,0x80000038);
    w16(0x20+14,1);w32(0x30,1);w32(0x34,0x80000058);
    w16(0x38+12,1);w16(0x38+14,1);w32(0x48,0x80000100);w32(0x4c,0x80000070);w32(0x50,101);w32(0x54,0x80000070);
    w16(0x58+14,1);w32(0x68,0x409);w32(0x6c,0x88);
    w16(0x70+14,1);w32(0x80,0x409);w32(0x84,0x98);
    w32(0x88,0x20a8);w32(0x8c,48);w32(0x98,0x20d8);w32(0x9c,20);
    // One authored opaque-red 1x1 BGRA icon with a DWORD AND mask.
    w32(0xa8,40);w32(0xac,1);w32(0xb0,2);w16(0xb4,1);w16(0xb6,32);w32(0xbc,4);w32(0xd0,0xffff0000);
    w16(0xda,1);w16(0xdc,1);w16(0xde,0x0101);w16(0xe2,1);w16(0xe4,32);w32(0xe6,48);w16(0xea,1);
    w16(0x100,4);unsigned pos=0x102;for(auto c:std::u16string(u"icon")){w16(pos,c);pos+=2;}
    return i;
}
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
    {wr::Process resource_process(step,options);resource_process.load(icon_image());
     auto id=call(resource_process,"LoadIconA",{resource_process.image_base,101});CHECK(id);
     auto name=resource_process.put_string("icon");auto named=call(resource_process,"LoadIconA",{resource_process.image_base,name});CHECK(named);
     CHECK(named==call(resource_process,"LoadIconA",{resource_process.image_base,name}));
     const std::array<std::uint8_t,5> missing{'g','o','n','e',0};resource_process.memory.copy_in(name,missing);
     CHECK(!call(resource_process,"LoadIconA",{resource_process.image_base,name}));CHECK(resource_process.last_error==1814);}
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
