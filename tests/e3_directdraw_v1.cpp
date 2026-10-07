// Native Windows DirectDraw v1 exclusive-mode boundary.
// Uses the current desktop mode to avoid inventing a display mode. Synthetic, not E3.
#include "winrecomp/process.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define DIRECTDRAW_VERSION 0x0700
#include <windows.h>
#include <ddraw.h>

namespace {
using U32=wr::U32;
unsigned checks{};
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("DirectDraw fullscreen: " #x);}while(0)
constexpr U32 Entry=0x401000,Wnd=0x401020;
wr::Process* current{};
wr::Image image(){
    wr::Image im;im.base=0x400000;im.entry=Entry;im.size=0x2000;im.headers_size=512;
    im.bytes.resize(1024);im.sections={{".text",0x1000,512,512,512,0x60000020}};
    im.bytes[512]=im.bytes[544]=0xc3;return im;
}
bool step(wr::Cpu& cpu,wr::Memory& memory,std::uint64_t&){
    if(cpu.eip==Entry){cpu.eip=wr::pop(cpu,memory);return true;}
    if(cpu.eip==Wnd){cpu.eip=current->resolve("user32.dll","DefWindowProcA");return true;}
    return false;
}
U32 call(wr::Process& p,U32 target,std::initializer_list<U32> args){
    auto saved=p.cpu;
    try{
        for(auto it=args.end();it!=args.begin();)wr::push(p.cpu,p.memory,*--it);
        wr::push(p.cpu,p.memory,saved.eip);p.cpu.eip=target;
        CHECK(target && p.dispatch_api());
        CHECK(p.cpu.eip==saved.eip && p.cpu.r[wr::ESP]==saved.r[wr::ESP]);
        auto value=p.cpu.r[wr::EAX];p.cpu=saved;return value;
    }catch(...){p.cpu=saved;throw;}
}
U32 api(wr::Process& p,const char* dll,const char* name,std::initializer_list<U32> args){
    return call(p,p.resolve(dll,name),args);
}
U32 com(wr::Process& p,U32 obj,unsigned slot,std::initializer_list<U32> args){
    return call(p,p.memory.load(p.memory.load(obj,32)+4*slot,32),args);
}
template<class F>void unsupported(F fn){
    bool caught=false;try{fn();}catch(const wr::GuestFault& e){CHECK(e.kind==wr::FaultKind::unsupported);caught=true;}CHECK(caught);
}
struct DesktopRestore {
    ~DesktopRestore(){ChangeDisplaySettingsA(nullptr,0);}
};
void run(){
    DesktopRestore host_restore;
    wr::ProcessOptions options;options.legacy_d3d9=true;
    wr::Process p(step,options);current=&p;p.load(image());
    const auto data=p.allocate_bytes(4096),wc=data+64,out=data+512;
    auto cls=p.put_string("WinRecompDDrawV1Fullscreen");
    for(auto [off,value]:{std::pair<U32,U32>{0,48},{8,Wnd},{20,0x400000},{40,cls}})p.memory.store(wc+off,value,32);
    CHECK(api(p,"user32.dll","RegisterClassExA",{wc})!=0);
    auto window=api(p,"user32.dll","CreateWindowExA",{0,cls,cls,0x90000000,20,20,320,240,0,0,0x400000,0});
    CHECK(window!=0);

    CHECK(api(p,"ddraw.dll","DirectDrawCreate",{0,out,0})==DD_OK);
    auto draw=p.memory.load(out,32);CHECK(draw!=0);

    // Normal mode remains valid and unsupported flag combinations remain hard failures.
    CHECK(com(p,draw,20,{draw,window,DDSCL_NORMAL})==DD_OK);
    unsupported([&]{com(p,draw,20,{draw,window,DDSCL_FULLSCREEN|DDSCL_EXCLUSIVE});});
    unsupported([&]{com(p,draw,20,{draw,0,DDSCL_FULLSCREEN|DDSCL_EXCLUSIVE|DDSCL_NOWINDOWCHANGES});});

    DEVMODEA mode{};mode.dmSize=sizeof(mode);CHECK(EnumDisplaySettingsA(nullptr,ENUM_CURRENT_SETTINGS,&mode)!=0);
    CHECK(mode.dmPelsWidth>0 && mode.dmPelsHeight>0);
    const U32 bpp=mode.dmBitsPerPel;
    CHECK(bpp==16 || bpp==32);

    constexpr U32 exclusive=DDSCL_FULLSCREEN|DDSCL_EXCLUSIVE|DDSCL_NOWINDOWCHANGES;
    const auto coop=com(p,draw,20,{draw,window,exclusive});
    CHECK(coop==DD_OK);
    const auto set_mode=com(p,draw,21,{draw,mode.dmPelsWidth,mode.dmPelsHeight,bpp});
    CHECK(set_mode==DD_OK);

    // E3 uses the old IDirectDraw interface with a modern 380-byte DDCAPS.
    const auto caps1=data+768,caps2=data+1152,desc=data+1536,backcaps=data+1664,fx=data+1792;
    p.memory.store(caps1,380,32);p.memory.store(caps2,380,32);
    CHECK(com(p,draw,11,{draw,caps1,caps2})==DD_OK);
    CHECK(p.memory.load(caps1,32)==380 && p.memory.load(caps2,32)==380);
    CHECK(p.memory.load(caps1+60,32)>=p.memory.load(caps1+64,32));

    // Reproduce E3's exact one-backbuffer primary flip chain request.
    for(U32 off=0;off<108;off+=4)p.memory.store(desc+off,0,32);
    p.memory.store(desc,108,32);
    p.memory.store(desc+4,DDSD_CAPS|DDSD_BACKBUFFERCOUNT,32);
    p.memory.store(desc+20,1,32);
    p.memory.store(desc+104,DDSCAPS_PRIMARYSURFACE|DDSCAPS_FLIP|DDSCAPS_COMPLEX|DDSCAPS_VIDEOMEMORY,32);
    CHECK(com(p,draw,6,{draw,desc,out,0})==DD_OK);
    const auto primary=p.memory.load(out,32);CHECK(primary!=0);

    p.memory.store(backcaps,DDSCAPS_BACKBUFFER,32);
    CHECK(com(p,primary,12,{primary,backcaps,out})==DD_OK);
    const auto back=p.memory.load(out,32);CHECK(back!=0 && back!=primary);

    for(U32 off=0;off<100;off+=4)p.memory.store(fx+off,0,32);
    p.memory.store(fx,100,32);p.memory.store(fx+80,0x00102030,32);
    CHECK(com(p,back,5,{back,0,0,0,DDBLT_COLORFILL,fx})==DD_OK);
    CHECK(com(p,primary,11,{primary,0,DDFLIP_WAIT})==DD_OK);
    com(p,back,2,{back});com(p,primary,2,{primary});

    // Invalid modes are errors, not guest faults or pretend successes.
    CHECK(com(p,draw,21,{draw,0,mode.dmPelsHeight,bpp})==U32(DDERR_INVALIDMODE));
    CHECK(com(p,draw,21,{draw,mode.dmPelsWidth,mode.dmPelsHeight,24})==U32(DDERR_INVALIDMODE));

    CHECK(com(p,draw,19,{draw})==DD_OK);
    CHECK(com(p,draw,20,{draw,window,DDSCL_NORMAL})==DD_OK);

    com(p,draw,2,{draw});
    CHECK(api(p,"user32.dll","DestroyWindow",{window})!=0);
    CHECK(api(p,"user32.dll","UnregisterClassA",{cls,0x400000})!=0);
}
}
int main(){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    try{run();std::cout<<checks<<" DirectDraw v1 exclusive/display-mode assertions passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
