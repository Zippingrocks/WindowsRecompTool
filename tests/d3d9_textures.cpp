#include "winrecomp/d3d9_host.hpp"
// Texture lifecycle, state translation and pixel regressions through guest COM.
// Reuse the existing boundary's authored image and checked invocation helpers.
#define main winrecomp_untextured_boundary_main
#include "d3d9_boundary.cpp"
#undef main
#include <limits>
namespace {
constexpr U32 TextureEnum=0x401030;
unsigned format_visits{},format_mode{};U32 format_expired{};
bool texture_step(wr::Cpu& c,wr::Memory& m,std::uint64_t& budget){
    if(c.eip!=TextureEnum)return step(c,m,budget);
    auto at=m.load(c.r[wr::ESP]+4,32);format_expired=at;++format_visits;
    CHECK(m.load(c.r[wr::ESP]+8,32)==Context);
    CHECK(m.load(at,32)==32 && m.load(at+12,32)==32);
    CHECK(m.load(at+16,32)==0xff0000 && m.load(at+20,32)==0xff00 && m.load(at+24,32)==0xff);
    CHECK(m.load(at+4,32)==DDPF_RGB || m.load(at+4,32)==(DDPF_RGB|DDPF_ALPHAPIXELS));
    if(format_mode==2)m.store(at,1,32);
    if(format_mode==3)throw wr::GuestFault(wr::FaultKind::unsupported,c.eip,"texture callback fault");
    c.r[wr::EAX]=format_mode==4?2:format_mode==1?0:1;c.eip=wr::pop(c,m);c.r[wr::ESP]+=8;return true;
}
void textures(){
    wr::ProcessOptions opts;opts.legacy_d3d9=true;wr::Process p(texture_step,opts);current=&p;
    auto im=image();im.bytes[560]=0xc3;p.load(im);
    auto base=p.allocate_bytes(16384),out=base+8192,desc=base+512,lk=base+768,verts=base+1024,wc=base+256;
    auto name=p.put_string("WinRecompTextureBoundary");
    for(auto [off,value]:{std::pair<U32,U32>{0,48},{8,Wnd},{20,0x400000},{40,name}})p.memory.store(wc+off,value,32);
    CHECK(api(p,"user32.dll","RegisterClassExA",{wc}));
    auto window=api(p,"user32.dll","CreateWindowExA",{0,name,name,0x00cf0000,20,20,160,160,0,0,0x400000,0});CHECK(window);
    guid(p,base,IID_IDirectDraw7);CHECK(api(p,"ddraw.dll","DirectDrawCreateEx",{0,out,base,0})==DD_OK);auto dd=p.memory.load(out,32);
    CHECK(com(p,dd,20,{dd,window,DDSCL_NORMAL})==DD_OK);
    guid(p,base,IID_IDirect3D7);CHECK(com(p,dd,0,{dd,base,out})==0);auto root=p.memory.load(out,32);
    auto surface=[&](U32 caps,U32 width,U32 height,bool alpha=false){
        for(U32 n=0;n<124;n+=4)p.memory.store(desc+n,0,32);
        for(auto [off,value]:{std::pair<U32,U32>{0,124},{4,0x1007},{8,height},{12,width},{72,32},{76,U32(DDPF_RGB|(alpha?DDPF_ALPHAPIXELS:0))},
            {84,32},{88,0xff0000},{92,0xff00},{96,0xff},{100,alpha?0xff000000u:0u},{104,caps}})p.memory.store(desc+off,value,32);
        CHECK(com(p,dd,6,{dd,desc,out,0})==0);return p.memory.load(out,32);
    };
    auto target=surface(DDSCAPS_OFFSCREENPLAIN|DDSCAPS_3DDEVICE,64,64);
    guid(p,base,IID_IDirect3DHALDevice);CHECK(com(p,root,4,{root,base,target,out})==0);auto device=p.memory.load(out,32);
    auto fill=[&](U32 s,auto color){
        p.memory.store(lk,124,32);CHECK(com(p,s,25,{s,0,lk,DDLOCK_WAIT,0})==0);
        auto ptr=p.memory.load(lk+36,32),pitch=p.memory.load(lk+16,32),w=p.memory.load(lk+12,32),h=p.memory.load(lk+8,32);
        for(U32 y=0;y<h;++y)for(U32 x=0;x<w;++x)p.memory.store(ptr+y*pitch+x*4,color(x,y),32);
        CHECK(com(p,s,32,{s,0})==0);
    };
    for(format_mode=0;format_mode<5;++format_mode){
        format_visits=0;
        if(format_mode<2)CHECK(com(p,device,4,{device,TextureEnum,Context})==0);
        else fault([&]{com(p,device,4,{device,TextureEnum,Context});},format_mode==2?wr::FaultKind::memory:wr::FaultKind::unsupported);
        CHECK(format_visits==(format_mode?1u:2u));
        fault([&]{p.memory.load(format_expired,32);},wr::FaultKind::memory);
    }
    auto texture=surface(DDSCAPS_TEXTURE|DDSCAPS_SYSTEMMEMORY,8,8,true);
    fill(texture,[](U32 x,U32 y){return y<4?(x<4?0xffff0000u:0xff00ff00u):(x<4?0xff0000ffu:0xffffffffu);});
    CHECK(com(p,device,34,{device,0,out})==0 && !p.memory.load(out,32));
    CHECK(com(p,device,35,{device,0,texture})==0);
    CHECK(com(p,device,34,{device,0,out})==0 && p.memory.load(out,32)==texture);com(p,texture,2,{texture});
    for(auto [key,value]:{std::pair<U32,U32>{1,2},{2,2},{3,0},{4,2},{5,2},{6,0},{11,0},{12,3},{13,3},{14,3},{16,1},{17,1},{18,1},{24,0}}){
        CHECK(com(p,device,37,{device,0,key,value})==0);
        p.memory.store(out+4,0xfeedcafe,32);CHECK(com(p,device,36,{device,0,key,out})==0);
        CHECK(p.memory.load(out,32)==value && p.memory.load(out+4,32)==0xfeedcafe);
    }
    for(auto [key,value]:{std::pair<U32,U32>{13,4},{16,5},{18,2},{11,1},{24,2},{1,7},{2,0x100},{99,0}})
        fault([&]{com(p,device,37,{device,0,key,value});},wr::FaultKind::unsupported);
    fault([&]{com(p,device,35,{device,1,texture});},wr::FaultKind::unsupported);
    fault([&]{com(p,device,35,{device,0,target});},wr::FaultKind::unsupported);
    fault([&]{com(p,device,34,{device,0,0});},wr::FaultKind::memory);
    CHECK(com(p,device,34,{device,0,out})==0 && p.memory.load(out,32)==texture);com(p,texture,2,{texture});
    CHECK(com(p,device,20,{device,D3DRENDERSTATE_CULLMODE,D3DCULL_NONE})==0);
    auto draw=[&](float u,float v,U32 color=0xffffffffu){
        const std::array<wr::host9::TexturedVertex,3> vertices{{{8,8,.5f,1,color,u,v},{56,8,.5f,1,color,u,v},{8,56,.5f,1,color,u,v}}};
        p.memory.copy_in(verts,std::span(reinterpret_cast<const std::uint8_t*>(vertices.data()),sizeof(vertices)));
        CHECK(com(p,device,10,{device,0,0,1,0xff183050,0x3f800000,0})==0);
        CHECK(com(p,device,5,{device})==0);CHECK(com(p,device,25,{device,4,0x144,verts,3,0})==0);CHECK(com(p,device,6,{device})==0);
        p.memory.store(lk,124,32);CHECK(com(p,target,25,{target,0,lk,DDLOCK_WAIT|DDLOCK_READONLY,0})==0);
        auto ptr=p.memory.load(lk+36,32),pitch=p.memory.load(lk+16,32);
        auto pixel=p.memory.load(ptr+16*pitch+16*4,32)&0xffffff;CHECK((p.memory.load(ptr,32)&0xffffff)==0x183050);
        CHECK(com(p,target,32,{target,0})==0);return pixel;
    };
    CHECK(draw(.125f,.125f)==0xff0000);CHECK(draw(.625f,.125f)==0x00ff00);
    CHECK(draw(.125f,.625f)==0x0000ff);CHECK(draw(.625f,.625f)==0xffffff);
    CHECK(draw(-.5f,.125f)==0xff0000); // clamp
    CHECK(com(p,device,37,{device,0,12,1})==0);CHECK(draw(1.625f,.125f)==0x00ff00); // wrap
    CHECK(com(p,device,37,{device,0,12,3})==0);
    CHECK(com(p,device,37,{device,0,16,2})==0);CHECK(com(p,device,37,{device,0,17,2})==0);
    auto mixed=draw(.5f,.125f);CHECK(((mixed>>16)&255)>=126 && ((mixed>>16)&255)<=129 && ((mixed>>8)&255)>=126 && ((mixed>>8)&255)<=129 && !(mixed&255));
    CHECK(com(p,device,37,{device,0,16,1})==0);CHECK(com(p,device,37,{device,0,17,1})==0);
    CHECK(com(p,device,37,{device,0,1,4})==0);CHECK(com(p,device,37,{device,0,3,0})==0);
    auto modulated=draw(.125f,.125f,0xff800000);CHECK(((modulated>>16)&255)>=127 && ((modulated>>16)&255)<=128 && !(modulated&65535));
    CHECK(com(p,device,37,{device,0,1,2})==0);
    // A bound surface is retained, and CPU edits require neither rebinding nor
    // a newly created device. D3D9 receives a fresh snapshot for each draw.
    fill(texture,[](U32,U32){return 0xff345678u;});CHECK(draw(.125f,.125f)==0x345678);
    p.memory.store(lk,124,32);CHECK(com(p,texture,25,{texture,0,lk,DDLOCK_WAIT,0})==0);
    CHECK(com(p,device,5,{device})==0);fault([&]{com(p,device,25,{device,4,0x144,verts,3,0});},wr::FaultKind::unsupported);
    CHECK(com(p,texture,32,{texture,0})==0);CHECK(com(p,device,25,{device,4,0x144,verts,3,0})==0);CHECK(com(p,device,6,{device})==0);
    com(p,texture,2,{texture});fault([&]{com(p,texture,24,{texture});},wr::FaultKind::unsupported);
    CHECK(draw(.125f,.125f)==0x345678);
    CHECK(com(p,device,34,{device,0,out})==0);auto held=p.memory.load(out,32);CHECK(held && held!=texture);
    CHECK(com(p,device,35,{device,0,0})==0);CHECK(com(p,device,34,{device,0,out})==0 && p.memory.load(out,32)==0);
    com(p,held,2,{held});
    CHECK(com(p,device,5,{device})==0);CHECK(com(p,device,25,{device,4,0x144,verts,3,0})!=0);CHECK(com(p,device,6,{device})==0);
    com(p,device,2,{device});com(p,target,2,{target});com(p,root,2,{root});com(p,dd,2,{dd});
    CHECK(api(p,"user32.dll","DestroyWindow",{window}));CHECK(api(p,"user32.dll","UnregisterClassA",{name,0x400000}));
    std::cout<<p.directdraw()->report()<<'\n';
}
}
int main(){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);try{textures();std::cout<<checks<<" texture state, pixel and lifetime assertions passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
