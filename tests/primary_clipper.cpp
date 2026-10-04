// Native windowed primary/render surfaces and clipper handle/reference marshalling.
// No pixels are written to the desktop: this is allocation/lifecycle verification.
#include "winrecomp/process.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <string>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#define DIRECTDRAW_VERSION 0x0700
#include <windows.h>
#include <ddraw.h>
namespace {
unsigned checks{};
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("primary/clipper: " #x);}while(0)
constexpr wr::U32 Entry=0x401000,Callback=0x401010;
thread_local wr::Process* active{};
wr::Image image(){wr::Image v;v.base=0x400000;v.entry=Entry;v.size=0x2000;v.headers_size=512;v.bytes.resize(1024);v.sections={{".text",0x1000,512,512,512,0x60000020}};v.bytes[512]=v.bytes[528]=0xc3;return v;}
bool step(wr::Cpu& c,wr::Memory& m,std::uint64_t&){if(c.eip==Entry){c.eip=wr::pop(c,m);return true;}if(c.eip!=Callback)return false;c.eip=active->resolve("user32.dll","DefWindowProcA");return true;}
wr::U32 call(wr::Process& p,wr::U32 at,std::initializer_list<wr::U32> args){auto saved=p.cpu;try{for(auto it=args.end();it!=args.begin();)wr::push(p.cpu,p.memory,*--it);wr::push(p.cpu,p.memory,saved.eip);p.cpu.eip=at;CHECK(at && p.dispatch_api());CHECK(p.cpu.eip==saved.eip && p.cpu.r[wr::ESP]==saved.r[wr::ESP]);return p.cpu.r[wr::EAX];}catch(...){p.cpu=saved;throw;}}
wr::U32 api(wr::Process& p,const char* dll,const char* name,std::initializer_list<wr::U32> a){return call(p,p.resolve(dll,name),a);}
wr::U32 com(wr::Process& p,wr::U32 o,unsigned slot,std::initializer_list<wr::U32> a){return call(p,p.memory.load(p.memory.load(o,32)+4*slot,32),a);}
template<class F>void fault(F f,wr::FaultKind k){bool caught=false;try{f();}catch(const wr::GuestFault& e){CHECK(e.kind==k);caught=true;}CHECK(caught);}
struct Native {IDirectDraw7* dd{};IDirectDrawClipper* clip{};~Native(){if(clip)clip->Release();if(dd)dd->Release();}};
void put(wr::Process& p,wr::U32 at,unsigned off,wr::U32 value){p.memory.store(at+off,value,32);}
void describe(wr::Process& p,wr::U32 at,DWORD flags,DWORD caps){std::array<std::uint8_t,124> zeros{};p.memory.copy_in(at,zeros);put(p,at,0,124);put(p,at,4,flags);put(p,at,104,caps);}
void check_description(wr::Process& p,wr::U32 at,const DDSURFACEDESC2& d){
    CHECK(p.memory.load(at,32)==124 && p.memory.load(at+4,32)==d.dwFlags);
    CHECK(p.memory.load(at+8,32)==d.dwHeight && p.memory.load(at+12,32)==d.dwWidth);
    if(d.dwFlags&DDSD_PITCH)CHECK(p.memory.load(at+16,32)==wr::U32(d.lPitch));
    CHECK(p.memory.load(at+32,32)==0 && p.memory.load(at+36,32)==0); // reserved and native pointer
    if(d.dwFlags&DDSD_PIXELFORMAT){DDPIXELFORMAT f{};p.memory.copy_out(at+72,std::span(reinterpret_cast<std::uint8_t*>(&f),32));CHECK(std::memcmp(&f,&d.ddpfPixelFormat,32)==0);}
    if(d.dwFlags&DDSD_CAPS){DDSCAPS2 c{};p.memory.copy_out(at+104,std::span(reinterpret_cast<std::uint8_t*>(&c),16));CHECK(std::memcmp(&c,&d.ddsCaps,16)==0);}
    CHECK(p.memory.load(at+124,32)==0xfaceb00c);
}
void run(){
    wr::Process p(step);active=&p;p.load(image());auto data=p.allocate_bytes(8192);p.memory.protect(data+4096,4096,0);
    auto wc=data,desc=data+1024,out=data+4092;
    auto name=p.put_string("WinRecompClipperBoundary"),title=p.put_string("Private primary/clipper test");
    const std::array<wr::U32,12> fields{48,0,Callback,0,0,p.image_base,0,0,0,0,name,0};for(unsigned i=0;i<fields.size();++i)put(p,wc,4*i,fields[i]);
    CHECK(api(p,"user32.dll","RegisterClassExA",{wc}));auto window=api(p,"user32.dll","CreateWindowExA",{0,name,title,WS_OVERLAPPEDWINDOW,10,10,160,120,0,0,p.image_base,0});CHECK(window);
    auto hwnd=reinterpret_cast<HWND>(p.gui()->native_window(window));CHECK(IsWindow(hwnd));
    p.memory.copy_in(data+256,std::span(reinterpret_cast<const std::uint8_t*>(&IID_IDirectDraw7),16));
    CHECK(api(p,"ddraw.dll","DirectDrawCreateEx",{0,out,data+256,0})==DD_OK);auto dd=p.memory.load(out,32);CHECK(com(p,dd,20,{dd,window,DDSCL_NORMAL})==DD_OK);
    Native host;CHECK(DirectDrawCreateEx(nullptr,reinterpret_cast<void**>(&host.dd),IID_IDirectDraw7,nullptr)==DD_OK);CHECK(host.dd->SetCooperativeLevel(hwnd,DDSCL_NORMAL)==DD_OK);
    DDSURFACEDESC2 wanted{};wanted.dwSize=sizeof(wanted);wanted.dwFlags=DDSD_CAPS;wanted.ddsCaps.dwCaps=DDSCAPS_PRIMARYSURFACE;
    IDirectDrawSurface7* oracle{};CHECK(host.dd->CreateSurface(&wanted,&oracle,nullptr)==DD_OK);DDSURFACEDESC2 expected{};expected.dwSize=sizeof(expected);CHECK(oracle->GetSurfaceDesc(&expected)==DD_OK);oracle->Release();
    describe(p,desc,DDSD_CAPS,DDSCAPS_PRIMARYSURFACE);
    fault([&]{com(p,dd,6,{dd,desc,out+1,0});},wr::FaultKind::memory); // validate before native creation
    CHECK(com(p,dd,6,{dd,desc,out,0})==DD_OK);auto primary=p.memory.load(out,32);put(p,desc,124,0xfaceb00c);
    CHECK(com(p,primary,22,{primary,desc})==DD_OK);check_description(p,desc,expected);
    // Some hosts reuse the windowed primary. Compare their real HRESULT instead
    // of prescribing DDERR_PRIMARYSURFACEALREADYEXISTS on every implementation.
    IDirectDrawSurface7 *first_native{},*second_native{};CHECK(host.dd->CreateSurface(&wanted,&first_native,nullptr)==DD_OK);
    auto second_result=host.dd->CreateSurface(&wanted,&second_native,nullptr);if(second_native)second_native->Release();first_native->Release();
    describe(p,desc,DDSD_CAPS,DDSCAPS_PRIMARYSURFACE);put(p,data,280,0xfeed1234);auto twice=com(p,dd,6,{dd,desc,data+280,0});CHECK(twice==wr::U32(second_result));
    if(SUCCEEDED(HRESULT(twice))){auto extra=p.memory.load(data+280,32);CHECK(extra);com(p,extra,2,{extra});}else CHECK(p.memory.load(data+280,32)==0xfeed1234);
    // Native reference policy and a guest HWND token, never a truncated HWND.
    CHECK(host.dd->CreateClipper(0,&host.clip,nullptr)==DD_OK);
    fault([&]{com(p,dd,4,{dd,0,out+1,0});},wr::FaultKind::memory);
    CHECK(com(p,dd,4,{dd,0,out,0})==DD_OK);auto clip=p.memory.load(out,32);
    HWND empty{};auto hr=host.clip->GetHWnd(&empty);CHECK(com(p,clip,4,{clip,out})==wr::U32(hr));if(SUCCEEDED(hr))CHECK(p.memory.load(out,32)==0);
    fault([&]{com(p,clip,4,{clip,out+1});},wr::FaultKind::memory);
    CHECK(host.clip->SetHWnd(0,hwnd)==DD_OK);CHECK(com(p,clip,8,{clip,0,window})==DD_OK);CHECK(com(p,clip,4,{clip,out})==DD_OK && p.memory.load(out,32)==window);
    fault([&]{com(p,clip,8,{clip,0,dd});},wr::FaultKind::unsupported);
    auto invalid=host.clip->SetHWnd(0xffffffffu,hwnd);CHECK(com(p,clip,8,{clip,0xffffffffu,window})==wr::U32(invalid));
    p.memory.copy_in(data+288,std::span(reinterpret_cast<const std::uint8_t*>(&IID_IDirectDrawClipper),16));CHECK(com(p,clip,0,{clip,data+288,out})==DD_OK);CHECK(p.memory.load(out,32)==clip);com(p,clip,2,{clip});
    fault([&]{com(p,primary,28,{primary,dd});},wr::FaultKind::unsupported);
    CHECK(com(p,primary,28,{primary,clip})==DD_OK);CHECK(com(p,primary,15,{primary,out})==DD_OK && p.memory.load(out,32)==clip);com(p,clip,2,{clip});
    CHECK(com(p,primary,28,{primary,clip})==DD_OK); // repeated attachment follows native reference rules
    com(p,clip,2,{clip});fault([&]{com(p,clip,1,{clip});},wr::FaultKind::unsupported);
    CHECK(com(p,primary,15,{primary,out})==DD_OK);auto retained=p.memory.load(out,32);CHECK(retained!=clip);
    CHECK(com(p,retained,4,{retained,out})==DD_OK && p.memory.load(out,32)==window);
    CHECK(com(p,primary,28,{primary,0})==DD_OK);com(p,retained,2,{retained});com(p,primary,2,{primary});
    // A small native-default render target has exactly the requested caps; no
    // fake pixel format is supplied and no actual rendering is claimed here.
    wanted={};wanted.dwSize=sizeof(wanted);wanted.dwFlags=DDSD_WIDTH|DDSD_HEIGHT|DDSD_CAPS;wanted.dwWidth=320;wanted.dwHeight=240;wanted.ddsCaps.dwCaps=DDSCAPS_OFFSCREENPLAIN|DDSCAPS_3DDEVICE;
    CHECK(host.dd->CreateSurface(&wanted,&oracle,nullptr)==DD_OK);expected={};expected.dwSize=sizeof(expected);CHECK(oracle->GetSurfaceDesc(&expected)==DD_OK);oracle->Release();
    describe(p,desc,wanted.dwFlags,wanted.ddsCaps.dwCaps);put(p,desc,8,240);put(p,desc,12,320);put(p,desc,124,0xfaceb00c);
    CHECK(com(p,dd,6,{dd,desc,out,0})==DD_OK);auto target=p.memory.load(out,32);CHECK(com(p,target,22,{target,desc})==DD_OK);check_description(p,desc,expected);com(p,target,2,{target});
    for(unsigned option=0;option<5;++option){describe(p,desc,wanted.dwFlags,wanted.ddsCaps.dwCaps);put(p,desc,8,240);put(p,desc,12,320);
        if(option==0)put(p,desc,104,DDSCAPS_TEXTURE);if(option==1)put(p,desc,12,0xffffffffu);if(option==2)put(p,desc,108,1);if(option==3)put(p,desc,36,0x1234);if(option==4)put(p,desc,4,DDSD_CAPS);
        fault([&]{com(p,dd,6,{dd,desc,out,0});},wr::FaultKind::unsupported);
    }
    com(p,dd,2,{dd});CHECK(api(p,"user32.dll","DestroyWindow",{window}));
}
}
int main(){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);try{run();std::cout<<checks<<" native primary/render-surface and clipper assertions passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
