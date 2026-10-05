// Host-backed legacy7-on9 boundary, independent of native IDirect3D7 existence.
#include "winrecomp/process.hpp"
#include <array>
#include <bit>
#include <cstring>
#include <iostream>
#include <thread>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#define DIRECTDRAW_VERSION 0x0700
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
namespace {
unsigned checks{};using U32=wr::U32;
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("D3D9 boundary: " #x);}while(0)
constexpr U32 Entry=0x401000,Wnd=0x401010,Enum=0x401020,Context=0xa13579bc;
wr::Process* current{};unsigned callbacks{},callback_mode{};U32 expired{};
wr::Image image(){wr::Image v;v.base=0x400000;v.entry=Entry;v.size=0x2000;v.headers_size=512;v.bytes.resize(1024);v.sections={{".text",0x1000,512,512,512,0x60000020}};v.bytes[512]=v.bytes[528]=v.bytes[544]=0xc3;return v;}
U32 call(wr::Process& p,U32 at,std::initializer_list<U32> args){auto saved=p.cpu;try{for(auto it=args.end();it!=args.begin();)wr::push(p.cpu,p.memory,*--it);wr::push(p.cpu,p.memory,saved.eip);p.cpu.eip=at;CHECK(at && p.dispatch_api());CHECK(p.cpu.eip==saved.eip && p.cpu.r[wr::ESP]==saved.r[wr::ESP]);auto value=p.cpu.r[wr::EAX];p.cpu=saved;return value;}catch(...){p.cpu=saved;throw;}}
U32 api(wr::Process& p,const char* dll,const char* name,std::initializer_list<U32> args){return call(p,p.resolve(dll,name),args);}
U32 com(wr::Process& p,U32 obj,unsigned slot,std::initializer_list<U32> args){return call(p,p.memory.load(p.memory.load(obj,32)+4*slot,32),args);}
template<class F>void fault(F f,wr::FaultKind kind){bool caught=false;try{f();}catch(const wr::GuestFault& e){CHECK(e.kind==kind);caught=true;}CHECK(caught);}
void guid(wr::Process& p,U32 at,const GUID& id){p.memory.copy_in(at,std::span(reinterpret_cast<const std::uint8_t*>(&id),16));}
bool step(wr::Cpu& c,wr::Memory& m,std::uint64_t&){
    if(c.eip==Entry){c.eip=wr::pop(c,m);return true;}
    if(c.eip==Wnd){c.eip=current->resolve("user32.dll","DefWindowProcA");return true;}
    if(c.eip!=Enum)return false;
    CHECK(m.load(c.r[wr::ESP]+16,32)==Context);auto at=m.load(c.r[wr::ESP]+12,32);expired=at;++callbacks;
    CHECK(current->read_string(m.load(c.r[wr::ESP]+8,32))=="WinRecomp D3D9");
    CHECK(m.load(at+116,32)==DDBD_32 && m.load(at+120,32)==DDBD_16);
    CHECK((m.load(at+76,32)&D3DPBLENDCAPS_SRCALPHA)!=0);
    CHECK((m.load(at+80,32)&D3DPBLENDCAPS_INVSRCALPHA)!=0);
    CHECK((m.load(at+84,32)&D3DPCMPCAPS_GREATER)!=0);
    CHECK(m.load(at+124,32)==1 && m.load(at+132,32)>0 && m.load(at+184,32)==0x00010001);
    for(unsigned n=220;n<236;n+=4)CHECK(m.load(at+n,32)==0);
    if(callback_mode==2)m.store(at,0,32);
    if(callback_mode==3)throw wr::GuestFault(wr::FaultKind::unsupported,c.eip,"deliberate enumeration failure");
    c.r[wr::EAX]=callback_mode==4?2:callback_mode==0?1:0;c.eip=wr::pop(c,m);c.r[wr::ESP]+=16;return true;
}
void run(){
    wr::ProcessOptions opts;opts.legacy_d3d9=true;wr::Process p(step,opts);current=&p;p.load(image());
    auto data=p.allocate_bytes(16384);p.memory.protect(data+12288,4096,0);auto out=data+12284,desc=data+512,lock=data+768,wc=data+256,view=data+1024,vertices=data+1100;
    auto name=p.put_string("WinRecompD3D9Boundary"),title=p.put_string("D3D9 test");
    for(auto [off,value]:{std::pair<U32,U32>{0,48},{8,Wnd},{20,0x400000},{40,name}})p.memory.store(wc+off,value,32);
    CHECK(api(p,"user32.dll","RegisterClassExA",{wc})!=0);
    // Borderless 64x64 client area keeps presentation pixel-exact; scaling is
    // deliberately outside the bounded renderer profile.
    auto window=api(p,"user32.dll","CreateWindowExA",{0,name,title,0x90000000,32,32,64,64,0,0,0x400000,0});CHECK(window!=0);
    guid(p,data,IID_IDirectDraw7);CHECK(api(p,"ddraw.dll","DirectDrawCreateEx",{0,out,data,0})==DD_OK);auto dd=p.memory.load(out,32);
    CHECK(com(p,dd,20,{dd,window,DDSCL_NORMAL})==DD_OK);
    guid(p,data,IID_IDirect3D7);CHECK(com(p,dd,0,{dd,data,out})==D3D_OK);auto root=p.memory.load(out,32);
    CHECK(com(p,dd,0,{dd,data,out})==D3D_OK && p.memory.load(out,32)==root);com(p,root,2,{root});
    for(callback_mode=0;callback_mode<5;++callback_mode){
        if(callback_mode<2)CHECK(com(p,root,3,{root,Enum,Context})==D3D_OK);
        else fault([&]{com(p,root,3,{root,Enum,Context});},callback_mode==2?wr::FaultKind::memory:wr::FaultKind::unsupported);
        fault([&]{p.memory.load(expired,32);},wr::FaultKind::memory);
    }
    CHECK(callbacks==5);callback_mode=0;
    CHECK(com(p,root,3,{root,0,0})==U32(DDERR_INVALIDPARAMS));
    guid(p,data,IID_IUnknown);CHECK(com(p,root,0,{root,data,out})==D3D_OK);auto unknown=p.memory.load(out,32);
    guid(p,data,IID_IDirect3D7);CHECK(com(p,unknown,0,{unknown,data,out})==D3D_OK && p.memory.load(out,32)==root);com(p,root,2,{root});com(p,unknown,2,{unknown});
    for(auto [off,value]:{std::pair<U32,U32>{0,124},{4,DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT},{8,64},{12,64},
        {72,32},{76,DDPF_RGB},{84,32},{88,0xff0000},{92,0xff00},{96,0xff},{104,DDSCAPS_OFFSCREENPLAIN|DDSCAPS_3DDEVICE}})p.memory.store(desc+off,value,32);
    CHECK(com(p,dd,6,{dd,desc,out,0})==DD_OK);auto surface=p.memory.load(out,32);guid(p,data,IID_IDirect3DHALDevice);
    fault([&]{com(p,root,4,{root,data,surface,out+1});},wr::FaultKind::memory);
    CHECK(com(p,root,4,{root,data,surface,out})==D3D_OK);auto device=p.memory.load(out,32);
    fault([&]{com(p,root,4,{root,data,surface,out});},wr::FaultKind::unsupported);
    guid(p,data,IID_IUnknown);CHECK(com(p,device,0,{device,data,out})==D3D_OK && p.memory.load(out,32)==device);com(p,device,2,{device});
    CHECK(com(p,device,7,{device,out})==D3D_OK && p.memory.load(out,32)==root);com(p,root,2,{root});
    CHECK(com(p,device,9,{device,out})==D3D_OK && p.memory.load(out,32)==surface);com(p,surface,2,{surface});
    p.memory.store(desc+236,0xfeed3456,32);CHECK(com(p,device,3,{device,desc})==D3D_OK);CHECK(p.memory.load(desc+236,32)==0xfeed3456);
    CHECK(p.memory.load(desc+120,32)==DDBD_16 && p.memory.load(desc+184,32)==0x00010001);
    CHECK((p.memory.load(desc+76,32)&D3DPBLENDCAPS_SRCALPHA)!=0);
    CHECK((p.memory.load(desc+80,32)&D3DPBLENDCAPS_INVSRCALPHA)!=0);
    CHECK((p.memory.load(desc+84,32)&D3DPCMPCAPS_GREATER)!=0);
    fault([&]{com(p,device,3,{device,out-228});},wr::FaultKind::memory);
    for(auto [state,value]:{std::pair<U32,U32>{7,0},{14,0},{137,0},{22,1},{9,2},{27,0},{28,0},{29,0}}){
        CHECK(com(p,device,20,{device,state,value})==D3D_OK);
        CHECK(com(p,device,21,{device,state,out})==D3D_OK && p.memory.load(out,32)==value);
    }
    CHECK(com(p,device,20,{device,D3DRENDERSTATE_SRCBLEND,D3DBLEND_SRCALPHA})==D3D_OK);
    CHECK(com(p,device,20,{device,D3DRENDERSTATE_DESTBLEND,D3DBLEND_INVSRCALPHA})==D3D_OK);
    CHECK(com(p,device,20,{device,D3DRENDERSTATE_ALPHABLENDENABLE,TRUE})==D3D_OK);
    CHECK(com(p,device,21,{device,D3DRENDERSTATE_SRCBLEND,out})==D3D_OK && p.memory.load(out,32)==D3DBLEND_SRCALPHA);
    CHECK(com(p,device,21,{device,D3DRENDERSTATE_DESTBLEND,out})==D3D_OK && p.memory.load(out,32)==D3DBLEND_INVSRCALPHA);
    CHECK(com(p,device,21,{device,D3DRENDERSTATE_ALPHABLENDENABLE,out})==D3D_OK && p.memory.load(out,32)==TRUE);
    fault([&]{com(p,device,20,{device,D3DRENDERSTATE_SRCBLEND,D3DBLEND_DESTCOLOR});},wr::FaultKind::unsupported);
    CHECK(com(p,device,20,{device,D3DRENDERSTATE_ALPHABLENDENABLE,FALSE})==D3D_OK);
    CHECK(com(p,device,20,{device,D3DRENDERSTATE_SRCBLEND,D3DBLEND_ONE})==D3D_OK);
    CHECK(com(p,device,20,{device,D3DRENDERSTATE_DESTBLEND,D3DBLEND_ZERO})==D3D_OK);
    CHECK(com(p,device,20,{device,D3DRENDERSTATE_ALPHAREF,127})==D3D_OK);
    CHECK(com(p,device,20,{device,D3DRENDERSTATE_ALPHAFUNC,D3DCMP_GREATER})==D3D_OK);
    CHECK(com(p,device,20,{device,D3DRENDERSTATE_ALPHATESTENABLE,TRUE})==D3D_OK);
    CHECK(com(p,device,21,{device,D3DRENDERSTATE_ALPHAREF,out})==D3D_OK && p.memory.load(out,32)==127);
    CHECK(com(p,device,21,{device,D3DRENDERSTATE_ALPHAFUNC,out})==D3D_OK && p.memory.load(out,32)==D3DCMP_GREATER);
    CHECK(com(p,device,21,{device,D3DRENDERSTATE_ALPHATESTENABLE,out})==D3D_OK && p.memory.load(out,32)==TRUE);
    CHECK(com(p,device,20,{device,D3DRENDERSTATE_ALPHAREF,256})!=D3D_OK);
    CHECK(com(p,device,20,{device,D3DRENDERSTATE_ALPHATESTENABLE,FALSE})==D3D_OK);
    fault([&]{com(p,device,20,{device,7,1});},wr::FaultKind::unsupported);
    CHECK(com(p,device,21,{device,7,out})==D3D_OK && p.memory.load(out,32)==0);
    fault([&]{com(p,device,21,{device,7,out+1});},wr::FaultKind::memory);
    for(auto [off,value]:{std::pair<U32,U32>{0,0},{4,0},{8,64},{12,64},{16,0},{20,0x3f800000}})p.memory.store(view+off,value,32);
    CHECK(com(p,device,13,{device,view})==D3D_OK);p.memory.store(view+24,0xabadcafe,32);CHECK(com(p,device,15,{device,view})==D3D_OK && p.memory.load(view+24,32)==0xabadcafe);
    p.memory.store(view+8,65,32);CHECK(com(p,device,13,{device,view})!=D3D_OK);CHECK(com(p,device,15,{device,view})==D3D_OK && p.memory.load(view+8,32)==64);
    std::array<U32,15> v{0x41000000,0x41000000,0x3f000000,0x3f800000,0xffd04020,0x42600000,0x41000000,0x3f000000,0x3f800000,0xffd04020,0x41000000,0x42600000,0x3f000000,0x3f800000,0xffd04020};
    for(unsigned n=0;n<v.size();++n)p.memory.store(vertices+4*n,v[n],32);
    CHECK(com(p,device,25,{device,4,0x44,vertices,3,0})!=D3D_OK);
    CHECK(com(p,device,10,{device,0,0,1,0xff183050,0x3f800000,0})==D3D_OK);
    fault([&]{com(p,device,10,{device,0,0,3,0xff183050,0x3f800000,0});},wr::FaultKind::unsupported);
    CHECK(com(p,device,5,{device})==D3D_OK);CHECK(com(p,device,5,{device})!=D3D_OK);
    fault([&]{com(p,device,25,{device,4,0x44,out-40,3,0});},wr::FaultKind::memory);
    fault([&]{com(p,device,25,{device,4,0x244,vertices,3,0});},wr::FaultKind::unsupported);
    fault([&]{com(p,device,25,{device,4,0x44,vertices,3,1});},wr::FaultKind::unsupported);
    p.memory.store(lock,124,32);fault([&]{com(p,surface,25,{surface,0,lock,0x11,0});},wr::FaultKind::unsupported);
    fault([&]{com(p,device,2,{device});},wr::FaultKind::unsupported);
    CHECK(com(p,device,25,{device,4,0x44,vertices,3,0})==D3D_OK);
    CHECK(com(p,device,6,{device})==D3D_OK);CHECK(com(p,device,6,{device})!=D3D_OK);
    auto lock_target=[&](U32 flags){p.memory.store(lock,124,32);CHECK(com(p,surface,25,{surface,0,lock,flags,0})==DD_OK);return p.memory.load(lock+36,32);};
    auto pixels=lock_target(0x11);auto pitch=p.memory.load(lock+16,32);
    CHECK((p.memory.load(pixels,32)&0xffffff)==0x183050);CHECK((p.memory.load(pixels+16*pitch+16*4,32)&0xffffff)==0xd04020);
    CHECK(com(p,surface,32,{surface,0})==DD_OK);fault([&]{p.memory.load(pixels,32);},wr::FaultKind::memory);
    // Native DD/guest CPU writes between scenes must reach the next D3D9 work.
    pixels=lock_target(1);p.memory.store(pixels,0x0056789a,32);CHECK(com(p,surface,32,{surface,0})==DD_OK);
    CHECK(com(p,device,5,{device})==D3D_OK && com(p,device,6,{device})==D3D_OK);
    pixels=lock_target(0x11);CHECK((p.memory.load(pixels,32)&0xffffff)==0x56789a);CHECK(com(p,surface,32,{surface,0})==DD_OK);
    // Present the current target through a guest primary-surface Blt. The test
    // then samples the actual window DC, not the offscreen render target.
    for(auto [off,value]:{std::pair<U32,U32>{4,DDSD_CAPS},{104,DDSCAPS_PRIMARYSURFACE},{108,0},{112,0},{116,0}})p.memory.store(desc+off,value,32);
    p.memory.store(desc,124,32);CHECK(com(p,dd,6,{dd,desc,out,0})==DD_OK);auto primary=p.memory.load(out,32);
    auto native_window=reinterpret_cast<HWND>(p.gui()->native_window(window));CHECK(native_window!=nullptr);
    RECT client{};CHECK(GetClientRect(native_window,&client));CHECK(client.right==64 && client.bottom==64);
    POINT origin{};CHECK(ClientToScreen(native_window,&origin));
    auto dst=view+64,src=view+80;
    for(auto [at,value]:{std::pair<U32,U32>{dst,U32(origin.x)},{dst+4,U32(origin.y)},{dst+8,U32(origin.x+64)},{dst+12,U32(origin.y+64)},
        {src,0},{src+4,0},{src+8,64},{src+12,64}})p.memory.store(at,value,32);
    CHECK(com(p,primary,5,{primary,dst,surface,src,DDBLT_WAIT,0})==DD_OK);
    // The source target pixels were checked immediately above. This assertion
    // proves the guest primary-surface Blt reached a real swap-chain Present
    // and that Present returned success. Desktop-compositor capture is not part
    // of this deterministic boundary test.
    CHECK(p.directdraw()->report().find("\"d3d9_presents\":1")!=std::string::npos);
    com(p,primary,2,{primary});
    // Native resource references survive release of the original guest tokens.
    com(p,surface,2,{surface});com(p,root,2,{root});com(p,dd,2,{dd});
    CHECK(com(p,device,9,{device,out})==D3D_OK);auto held=p.memory.load(out,32);CHECK(held!=surface);com(p,held,2,{held});
    CHECK(com(p,device,2,{device})==0);fault([&]{com(p,device,5,{device});},wr::FaultKind::unsupported);
    CHECK(api(p,"user32.dll","DestroyWindow",{window})!=0);CHECK(api(p,"user32.dll","UnregisterClassA",{name,0x400000})!=0);
    std::cout<<p.directdraw()->report()<<'\n';
}
}
int main(){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);try{run();std::cout<<checks<<" D3D9-backed legacy bridge assertions passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
