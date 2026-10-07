// Native Windows legacy DirectSound boundary for the E3-observed audio startup.
// No game bytes. Guest DSBUFFERDESC/WAVEFORMATEX layouts are explicitly translated.
#include "winrecomp/process.hpp"
#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dsound.h>

namespace {
using U32=wr::U32;
unsigned checks{};
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("DirectSound assertion: " #x);}while(0)
constexpr U32 Entry=0x401000,Wnd=0x401020;
wr::Process* current{};
constexpr GUID IID_KsObserved{0x31efac30,0x515c,0x11d0,{0xa9,0xaa,0x00,0xaa,0x00,0x61,0xbe,0x93}};

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
        CHECK(target&&p.dispatch_api());CHECK(p.cpu.eip==saved.eip&&p.cpu.r[wr::ESP]==saved.r[wr::ESP]);
        auto value=p.cpu.r[wr::EAX];p.cpu=saved;return value;
    }catch(...){p.cpu=saved;throw;}
}
U32 api(wr::Process& p,const char* dll,const char* name,std::initializer_list<U32> args){return call(p,p.resolve(dll,name),args);}
U32 com(wr::Process& p,U32 object,unsigned slot,std::initializer_list<U32> args){return call(p,p.memory.load(p.memory.load(object,32)+slot*4,32),args);}
template<class F>void unsupported(F fn){bool caught=false;try{fn();}catch(const wr::GuestFault& e){caught=true;CHECK(e.kind==wr::FaultKind::unsupported);}CHECK(caught);}
void put_guid(wr::Process& p,U32 at,const GUID& id){p.memory.copy_in(at,std::span(reinterpret_cast<const std::uint8_t*>(&id),16));}
void put_pcm(wr::Process& p,U32 at,U32 rate,U32 channels){
    p.memory.store(at,WAVE_FORMAT_PCM,16);p.memory.store(at+2,channels,16);p.memory.store(at+4,rate,32);
    p.memory.store(at+8,rate*channels*2,32);p.memory.store(at+12,channels*2,16);p.memory.store(at+14,16,16);
}
void put_desc(wr::Process& p,U32 at,U32 flags,U32 bytes,U32 format){
    for(U32 n=0;n<36;n+=4)p.memory.store(at+n,0,32);
    p.memory.store(at,36,32);p.memory.store(at+4,flags,32);p.memory.store(at+8,bytes,32);p.memory.store(at+16,format,32);
}
void run(){
    wr::Process p(step);current=&p;p.load(image());
    const auto data=p.allocate_bytes(8192),wc=data+64,out=data+512,guid=data+640,desc=data+768,format=data+896,caps=data+1024;
    auto cls=p.put_string("WinRecompDirectSoundBoundary");
    for(auto [off,value]:{std::pair<U32,U32>{0,48},{8,Wnd},{20,0x400000},{40,cls}})p.memory.store(wc+off,value,32);
    CHECK(api(p,"user32.dll","RegisterClassExA",{wc})!=0);
    auto window=api(p,"user32.dll","CreateWindowExA",{0,cls,cls,0x90000000,20,20,320,240,0,0,0x400000,0});CHECK(window);
    auto native_window=reinterpret_cast<HWND>(p.gui()->native_window(window));ShowWindow(native_window,SW_SHOW);SetActiveWindow(native_window);

    unsupported([&]{api(p,"dsound.dll","#1",{0,out,1});});
    const auto created=HRESULT(api(p,"dsound.dll","#1",{0,out,0}));
    if(created==DSERR_NODRIVER){
        const auto report=p.report();CHECK(report.find("\"directsound\":{\"backend\":\"native-legacy\"")!=std::string::npos);
        CHECK(api(p,"user32.dll","DestroyWindow",{window})!=0);CHECK(api(p,"user32.dll","UnregisterClassA",{cls,0x400000})!=0);
        std::cout<<checks<<" DirectSound assertions passed (host reports DSERR_NODRIVER)\n";return;
    }
    CHECK(SUCCEEDED(created));const auto sound=p.memory.load(out,32);CHECK(sound);

    const auto coop=HRESULT(com(p,sound,6,{sound,window,DSSCL_EXCLUSIVE}));
    CHECK(SUCCEEDED(coop));
    p.memory.store(caps,96,32);CHECK(SUCCEEDED(HRESULT(com(p,sound,4,{sound,caps}))));CHECK(p.memory.load(caps,32)==96);

    constexpr U32 Primary=DSBCAPS_PRIMARYBUFFER|DSBCAPS_CTRL3D;
    put_desc(p,desc,Primary,0,0);CHECK(SUCCEEDED(HRESULT(com(p,sound,3,{sound,desc,out,0}))));
    const auto primary=p.memory.load(out,32);CHECK(primary);
    put_pcm(p,format,22050,2);CHECK(SUCCEEDED(HRESULT(com(p,primary,14,{primary,format}))));

    put_guid(p,guid,IID_IDirectSound3DListener);CHECK(com(p,primary,0,{primary,guid,out})==U32(S_OK));
    const auto listener=p.memory.load(out,32);CHECK(listener);
    const U32 one=std::bit_cast<U32>(1.0f);
    CHECK(SUCCEEDED(HRESULT(com(p,listener,11,{listener,one,DS3D_IMMEDIATE}))));
    CHECK(SUCCEEDED(HRESULT(com(p,listener,15,{listener,one,DS3D_IMMEDIATE}))));

    constexpr U32 Secondary=DSBCAPS_STATIC|DSBCAPS_CTRLFREQUENCY|DSBCAPS_CTRLVOLUME|DSBCAPS_GETCURRENTPOSITION2|DSBCAPS_CTRL3D;
    put_pcm(p,format,22050,1);put_desc(p,desc,Secondary,4096,format);
    CHECK(SUCCEEDED(HRESULT(com(p,sound,3,{sound,desc,out,0}))));const auto secondary=p.memory.load(out,32);CHECK(secondary);
    put_guid(p,guid,IID_IDirectSound3DBuffer);CHECK(com(p,secondary,0,{secondary,guid,out})==U32(S_OK));
    const auto buffer3d=p.memory.load(out,32);CHECK(buffer3d);

    // E3 probes EAX 2.0 through IKsPropertySet. The bounded bridge must say no
    // rather than advertising an unimplemented extension.
    put_guid(p,guid,IID_KsObserved);CHECK(com(p,buffer3d,0,{buffer3d,guid,out})==U32(E_NOINTERFACE));CHECK(p.memory.load(out,32)==0);

    const U32 zero=std::bit_cast<U32>(0.0f),ten=std::bit_cast<U32>(10.0f);
    CHECK(SUCCEEDED(HRESULT(com(p,buffer3d,19,{buffer3d,zero,zero,zero,DS3D_DEFERRED}))));
    CHECK(SUCCEEDED(HRESULT(com(p,buffer3d,20,{buffer3d,zero,zero,zero,DS3D_DEFERRED}))));
    CHECK(SUCCEEDED(HRESULT(com(p,buffer3d,16,{buffer3d,ten,DS3D_DEFERRED}))));
    CHECK(SUCCEEDED(HRESULT(com(p,buffer3d,17,{buffer3d,one,DS3D_DEFERRED}))));
    CHECK(SUCCEEDED(HRESULT(com(p,buffer3d,13,{buffer3d,DS3D_DEFAULTCONEANGLE,DS3D_DEFAULTCONEANGLE,DS3D_DEFERRED}))));
    CHECK(SUCCEEDED(HRESULT(com(p,buffer3d,15,{buffer3d,DS3D_DEFAULTCONEOUTSIDEVOLUME,DS3D_DEFERRED}))));
    CHECK(SUCCEEDED(HRESULT(com(p,secondary,15,{secondary,U32(-100)}))));
    CHECK(SUCCEEDED(HRESULT(com(p,secondary,17,{secondary,22050}))));
    CHECK(SUCCEEDED(HRESULT(com(p,listener,17,{listener}))));

    const auto report=p.report();
    CHECK(report.find("\"directsound\":{\"backend\":\"native-legacy\"")!=std::string::npos);
    CHECK(report.find("\"primary_buffers\":1")!=std::string::npos);
    CHECK(report.find("\"secondary_buffers\":1")!=std::string::npos);
    CHECK(report.find("\"listeners\":1")!=std::string::npos);
    CHECK(report.find("\"buffers3d\":1")!=std::string::npos);

    com(p,buffer3d,2,{buffer3d});com(p,secondary,2,{secondary});com(p,listener,2,{listener});com(p,primary,2,{primary});
    CHECK(SUCCEEDED(HRESULT(com(p,sound,6,{sound,window,DSSCL_NORMAL}))));com(p,sound,2,{sound});
    CHECK(api(p,"user32.dll","DestroyWindow",{window})!=0);CHECK(api(p,"user32.dll","UnregisterClassA",{cls,0x400000})!=0);
}
}
int main(){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);try{run();std::cout<<checks<<" legacy DirectSound boundary assertions passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
