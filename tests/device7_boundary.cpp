// Real render-device construction and texture-format enumeration. Not gameplay.
#include "winrecomp/process.hpp"
#include <array>
#include <cstring>
#include <exception>
#include <iostream>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#define DIRECTDRAW_VERSION 0x0700
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
namespace {
unsigned checks{};
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("device7 boundary: " #x);}while(0)
using U32=wr::U32;using Format=std::array<U32,8>;
constexpr U32 Entry=0x401000,Callback=0x401010,Context=0xa197356b;
wr::Image image(){wr::Image v;v.base=0x400000;v.entry=Entry;v.size=0x2000;v.headers_size=512;v.bytes.resize(1024);v.sections={{".text",0x1000,512,512,512,0x60000020}};v.bytes[512]=v.bytes[528]=0xc3;return v;}
U32 call(wr::Process& p,U32 at,std::initializer_list<U32> args){auto saved=p.cpu;try{for(auto it=args.end();it!=args.begin();)wr::push(p.cpu,p.memory,*--it);wr::push(p.cpu,p.memory,saved.eip);p.cpu.eip=at;CHECK(at && p.dispatch_api());CHECK(p.cpu.eip==saved.eip && p.cpu.r[wr::ESP]==saved.r[wr::ESP]);return p.cpu.r[wr::EAX];}catch(...){p.cpu=saved;throw;}}
U32 com(wr::Process& p,U32 obj,unsigned slot,std::initializer_list<U32> args){return call(p,p.memory.load(p.memory.load(obj,32)+4*slot,32),args);}
template<class F>void fault(F f,wr::FaultKind k){bool caught=false;try{f();}catch(const wr::GuestFault& e){CHECK(e.kind==k);caught=true;}CHECK(caught);}
void guid(wr::Process& p,U32 at,const GUID& id){p.memory.copy_in(at,std::span(reinterpret_cast<const std::uint8_t*>(&id),16));}
struct Native {
    IDirectDraw7* dd{};IDirect3D7* d3d{};IDirectDrawSurface7* surface{};IDirect3DDevice7* device{};
    ~Native(){if(device)device->Release();if(surface)surface->Release();if(d3d)d3d->Release();if(dd)dd->Release();}
};
HRESULT CALLBACK select_device(char*,char*,D3DDEVICEDESC7* d,void* p){*static_cast<GUID*>(p)=d->deviceGUID;return D3DENUMRET_CANCEL;}
struct Original {std::vector<DDPIXELFORMAT> formats;HRESULT reply=D3DENUMRET_OK;};
HRESULT CALLBACK native_format(DDPIXELFORMAT* f,void* p){auto& n=*static_cast<Original*>(p);n.formats.push_back(*f);return n.reply;}
struct State {wr::Process* p{};U32 device{},expired{},reply=1;std::vector<Format> formats;bool fail{},write{},nested{},inside{},release{},exit{};}state;
bool step(wr::Cpu& c,wr::Memory& m,std::uint64_t&){if(c.eip==Entry){c.eip=wr::pop(c,m);return true;}if(c.eip!=Callback)return false;
    CHECK(m.load(c.r[wr::ESP]+8,32)==Context);auto at=m.load(c.r[wr::ESP]+4,32);state.expired=at;
    Format row{};for(unsigned n=0;n<8;++n)row[n]=m.load(at+4*n,32);CHECK(row[0]==32);state.formats.push_back(row);
    if(state.fail)throw wr::GuestFault(wr::FaultKind::unsupported,c.eip,"intentional texture callback failure");
    if(state.write)m.store(at,0,32);
    if(state.nested && !state.inside){state.inside=true;CHECK(com(*state.p,state.device,4,{state.device,Callback,Context})==D3D_OK);state.inside=false;for(unsigned n=0;n<8;++n)CHECK(row[n]==m.load(at+4*n,32));state.expired=at;}
    if(state.release){state.release=false;com(*state.p,state.device,2,{state.device});}
    if(state.exit)state.p->exit(19);
    c.r[wr::EAX]=state.reply;c.eip=wr::pop(c,m);c.r[wr::ESP]+=8;return true;
}
void compare(const Format& got,const DDPIXELFORMAT& f){
    CHECK(got[0]==32 && got[1]==f.dwFlags);CHECK(got[2]==((f.dwFlags&DDPF_FOURCC)?f.dwFourCC:0));
    constexpr DWORD palette=DDPF_PALETTEINDEXED1|DDPF_PALETTEINDEXED2|DDPF_PALETTEINDEXED4|DDPF_PALETTEINDEXED8|DDPF_PALETTEINDEXEDTO8;
    const DWORD type=f.dwFlags&(DDPF_RGB|DDPF_ALPHA|DDPF_LUMINANCE|DDPF_BUMPDUDV|palette);
    CHECK(got[3]==(type?f.dwRGBBitCount:0));
    if(f.dwFlags&DDPF_RGB){CHECK(got[4]==f.dwRBitMask && got[5]==f.dwGBitMask && got[6]==f.dwBBitMask);}
    else if(f.dwFlags&DDPF_BUMPDUDV){CHECK(got[4]==f.dwBumpDuBitMask && got[5]==f.dwBumpDvBitMask);CHECK(got[6]==((f.dwFlags&DDPF_BUMPLUMINANCE)?f.dwBumpLuminanceBitMask:0));}
    else if(f.dwFlags&DDPF_LUMINANCE){CHECK(got[4]==f.dwLuminanceBitMask && got[5]==0 && got[6]==0);}
    else CHECK(got[4]==0 && got[5]==0 && got[6]==0);
    CHECK(got[7]==((f.dwFlags&DDPF_ALPHAPIXELS)?f.dwRGBAlphaBitMask:0));
}
void run(){
    Native native;CHECK(DirectDrawCreateEx(nullptr,reinterpret_cast<void**>(&native.dd),IID_IDirectDraw7,nullptr)==DD_OK);CHECK(native.dd->QueryInterface(IID_IDirect3D7,reinterpret_cast<void**>(&native.d3d))==D3D_OK);CHECK(native.dd->SetCooperativeLevel(nullptr,DDSCL_NORMAL)==DD_OK);
    GUID id{};CHECK(native.d3d->EnumDevices(select_device,&id)==D3D_OK);CHECK(!IsEqualGUID(id,GUID{}));
    DDSURFACEDESC2 surface{};surface.dwSize=sizeof(surface);surface.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT;surface.dwWidth=64;surface.dwHeight=64;surface.ddsCaps.dwCaps=DDSCAPS_OFFSCREENPLAIN|DDSCAPS_3DDEVICE;
    CHECK(native.dd->CreateSurface(&surface,&native.surface,nullptr)==DD_OK);CHECK(native.d3d->CreateDevice(id,native.surface,&native.device)==D3D_OK);
    wr::Process p(step);state={};state.p=&p;p.load(image());auto data=p.allocate_bytes(8192);p.memory.protect(data+4096,4096,0);auto out=data+4092,desc=data+512;
    guid(p,data,IID_IDirectDraw7);CHECK(call(p,p.resolve("ddraw.dll","DirectDrawCreateEx"),{0,out,data,0})==DD_OK);auto dd=p.memory.load(out,32);CHECK(com(p,dd,20,{dd,0,DDSCL_NORMAL})==DD_OK);
    guid(p,data,IID_IDirect3D7);CHECK(com(p,dd,0,{dd,data,out})==D3D_OK);auto d3d=p.memory.load(out,32);
    for(auto [off,value]:{std::pair<U32,U32>{0,124},{4,surface.dwFlags},{8,64},{12,64},{104,surface.ddsCaps.dwCaps}})p.memory.store(desc+off,value,32);
    CHECK(com(p,dd,6,{dd,desc,out,0})==DD_OK);auto target=p.memory.load(out,32);guid(p,data,id);
    fault([&]{com(p,d3d,4,{d3d,data,target,out+1});},wr::FaultKind::memory);
    fault([&]{com(p,d3d,4,{d3d,0,target,out});},wr::FaultKind::memory);
    fault([&]{com(p,d3d,4,{d3d,data,dd,out});},wr::FaultKind::unsupported);
    CHECK(com(p,d3d,4,{d3d,data,target,out})==D3D_OK);auto device=p.memory.load(out,32);state.device=device;CHECK(p.directdraw()->report().find("\"d3d_devices_created\":1")!=std::string::npos);
    fault([&]{p.memory.store(device,0,32);},wr::FaultKind::memory);
    guid(p,data,IID_IDirect3DDevice7);CHECK(com(p,device,0,{device,data,out})==D3D_OK && p.memory.load(out,32)==device);com(p,device,2,{device});
    CHECK(com(p,device,7,{device,out})==D3D_OK && p.memory.load(out,32)==d3d);com(p,d3d,2,{d3d});
    CHECK(com(p,device,9,{device,out})==D3D_OK && p.memory.load(out,32)==target);com(p,target,2,{target});
    D3DDEVICEDESC7 caps{};CHECK(native.device->GetCaps(&caps)==D3D_OK);p.memory.store(desc+236,0xfeed3456,32);CHECK(com(p,device,3,{device,desc})==D3D_OK);
    std::array<std::uint8_t,220> got{};p.memory.copy_out(desc,got);CHECK(std::memcmp(got.data(),&caps,220)==0);for(unsigned n=220;n<236;n+=4)CHECK(p.memory.load(desc+n,32)==0);CHECK(p.memory.load(desc+236,32)==0xfeed3456);
    fault([&]{com(p,device,3,{device,out-228});},wr::FaultKind::memory);
    auto enumerate=[&]{return com(p,device,4,{device,Callback,Context});};Original expected;CHECK(native.device->EnumTextureFormats(native_format,&expected)==D3D_OK);CHECK(!expected.formats.empty());
    CHECK(enumerate()==D3D_OK);CHECK(state.formats.size()==expected.formats.size());for(unsigned n=0;n<expected.formats.size();++n)compare(state.formats[n],expected.formats[n]);fault([&]{p.memory.load(state.expired,32);},wr::FaultKind::memory);
    auto count=state.formats.size();state.formats.clear();state.reply=0;expected.formats.clear();expected.reply=0;CHECK(native.device->EnumTextureFormats(native_format,&expected)==D3D_OK);CHECK(enumerate()==D3D_OK);CHECK(state.formats.size()==expected.formats.size());compare(state.formats.front(),expected.formats.front());
    for(unsigned mode=0;mode<3;++mode){state.fail=mode==0;state.write=mode==1;state.reply=mode==2?2:1;fault([&]{enumerate();},mode==1?wr::FaultKind::memory:wr::FaultKind::unsupported);fault([&]{p.memory.load(state.expired,32);},wr::FaultKind::memory);}
    state.fail=state.write=false;state.reply=0;state.nested=true;state.formats.clear();CHECK(enumerate()==D3D_OK && state.formats.size()==2);state.nested=false;
    CHECK(com(p,device,4,{device,0,0})==U32(native.device->EnumTextureFormats(nullptr,nullptr)));
    fault([&]{com(p,device,4,{device,data,Context});},wr::FaultKind::memory);
    // Rendering is still unsupported by this increment, not a successful no-op.
    fault([&]{com(p,device,5,{device});},wr::FaultKind::unsupported);
    // Device holds its real target and Direct3D references independently.
    com(p,target,2,{target});com(p,d3d,2,{d3d});com(p,dd,2,{dd});CHECK(com(p,device,9,{device,out})==D3D_OK);auto held=p.memory.load(out,32);CHECK(held!=target);com(p,held,2,{held});
    state.release=true;state.formats.clear();CHECK(enumerate()==D3D_OK && state.formats.size()==1);fault([&]{enumerate();},wr::FaultKind::unsupported);
    std::cout<<"Real native texture formats="<<count<<"; pointer-free data and ownership verified\n";
}
}
int main(){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);try{run();std::cout<<checks<<" native device7 construction/format assertions passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
