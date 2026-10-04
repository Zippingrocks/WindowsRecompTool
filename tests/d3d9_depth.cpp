// Synthetic native-Windows boundary tests, not an E3 scene or CPU fallback.
#include "winrecomp/d3d9_host.hpp"
#define main winrecomp_previous_boundary_main
#include "d3d9_boundary.cpp"
#undef main
#include <limits>
namespace {
constexpr U32 ZCallback=0x401030;
unsigned z_calls{},z_mode{};U32 z_expired{};
bool depth_step(wr::Cpu& c,wr::Memory& m,std::uint64_t& budget){
    if(c.eip!=ZCallback)return step(c,m,budget);
    ++z_calls;const auto at=m.load(c.r[wr::ESP]+4,32);z_expired=at;
    CHECK(m.load(c.r[wr::ESP]+8,32)==Context);
    const std::array<U32,8> expected{32,DDPF_ZBUFFER,0,16,0,0xffff,0,0};
    for(unsigned n=0;n<8;++n)CHECK(m.load(at+4*n,32)==expected[n]);
    if(z_mode==2)m.store(at,0,32);
    if(z_mode==3)throw wr::GuestFault(wr::FaultKind::unsupported,c.eip,"depth callback fault");
    c.r[wr::EAX]=z_mode==4?2:z_mode==0?1:0;c.eip=wr::pop(c,m);c.r[wr::ESP]+=8;return true;
}
void depth_tests(){
    wr::ProcessOptions options;options.legacy_d3d9=true;wr::Process p(depth_step,options);current=&p;
    auto program=image();program.bytes[560]=0xc3;p.load(std::move(program));
    const auto base=p.allocate_bytes(16384),out=base+4092,wc=base+256,desc=base+512,zdesc=base+768;
    const auto lk=base+1024,verts=base+1280,query=base+1536,view=base+1792;
    p.memory.protect(base+4096,4096,0);
    auto name=p.put_string("WinRecompDepthBoundary");
    for(auto [off,value]:{std::pair<U32,U32>{0,48},{8,Wnd},{20,0x400000},{40,name}})p.memory.store(wc+off,value,32);
    CHECK(api(p,"user32.dll","RegisterClassExA",{wc}));
    auto window=api(p,"user32.dll","CreateWindowExA",{0,name,name,0x00cf0000,20,20,160,160,0,0,0x400000,0});CHECK(window);
    auto root_dd=[&]{guid(p,base,IID_IDirectDraw7);CHECK(api(p,"ddraw.dll","DirectDrawCreateEx",{0,out,base,0})==0);auto dd=p.memory.load(out,32);CHECK(com(p,dd,20,{dd,window,DDSCL_NORMAL})==0);return dd;};
    auto dd=root_dd();guid(p,base,IID_IDirect3D7);CHECK(com(p,dd,0,{dd,base,out})==0);auto d3d=p.memory.load(out,32);
    guid(p,base,IID_IDirect3DHALDevice);
    CHECK(com(p,d3d,6,{d3d,base,0,Context})==U32(DDERR_INVALIDPARAMS));
    for(z_mode=0;z_mode<5;++z_mode){
        if(z_mode<2)CHECK(com(p,d3d,6,{d3d,base,ZCallback,Context})==0);
        else fault([&]{com(p,d3d,6,{d3d,base,ZCallback,Context});},z_mode==2?wr::FaultKind::memory:wr::FaultKind::unsupported);
        fault([&]{p.memory.load(z_expired,32);},wr::FaultKind::memory);
    }
    CHECK(z_calls==5);z_mode=0;
    auto describe=[&](U32 at,bool depth,unsigned size=64){
        for(U32 off=0;off<124;off+=4)p.memory.store(at+off,0,32);
        for(auto [off,value]:{std::pair<U32,U32>{0,124},{4,0x1007},{8,size},{12,size},{72,32},
            {76,depth?U32(DDPF_ZBUFFER):U32(DDPF_RGB)},{84,depth?16u:32u},
            {88,depth?0u:0xff0000u},{92,depth?0xffffu:0xff00u},{96,depth?0u:0xffu},
            {104,depth?U32(DDSCAPS_ZBUFFER|DDSCAPS_VIDEOMEMORY):U32(DDSCAPS_OFFSCREENPLAIN|DDSCAPS_3DDEVICE)}})p.memory.store(at+off,value,32);
    };
    auto make_surface=[&](U32 parent,bool depth,unsigned size=64){describe(depth?zdesc:desc,depth,size);CHECK(com(p,parent,6,{parent,depth?zdesc:desc,out,0})==0);return p.memory.load(out,32);};
    auto target=make_surface(dd,false),z=make_surface(dd,true);
    // Structure validation and output bounds precede allocation or mutation.
    describe(zdesc,true);p.memory.store(out,0xabcd1234,32);
    fault([&]{com(p,dd,6,{dd,zdesc,out+1,0});},wr::FaultKind::memory);
    CHECK(p.memory.load(out,32)==0xabcd1234);
    p.memory.store(zdesc+84,24,32);fault([&]{com(p,dd,6,{dd,zdesc,out,0});},wr::FaultKind::unsupported);
    describe(zdesc,true);p.memory.store(zdesc+12,2049,32);fault([&]{com(p,dd,6,{dd,zdesc,out,0});},wr::FaultKind::unsupported);
    describe(zdesc,true);p.memory.store(zdesc+76,DDPF_ZBUFFER|DDPF_STENCILBUFFER,32);fault([&]{com(p,dd,6,{dd,zdesc,out,0});},wr::FaultKind::unsupported);
    guid(p,base,IID_IUnknown);CHECK(com(p,z,0,{z,base,out})==0 && p.memory.load(out,32)==z);com(p,z,2,{z});
    guid(p,base,IID_IDirect3D7);CHECK(com(p,z,0,{z,base,out})==U32(E_NOINTERFACE) && p.memory.load(out,32)==0);
    p.memory.store(query,124,32);p.memory.store(query+124,0xcafefeed,32);CHECK(com(p,z,22,{z,query})==0);
    CHECK(p.memory.load(query+4,32)==0x1007 && p.memory.load(query+84,32)==16 && p.memory.load(query+92,32)==0xffff);
    CHECK(p.memory.load(query+36,32)==0 && p.memory.load(query+124,32)==0xcafefeed);
    p.memory.store(query,32,32);p.memory.store(query+32,0xcafefeed,32);CHECK(com(p,z,21,{z,query})==0);
    CHECK(p.memory.load(query+4,32)==DDPF_ZBUFFER && p.memory.load(query+32,32)==0xcafefeed);
    fault([&]{com(p,z,21,{z,out});},wr::FaultKind::memory);
    CHECK(com(p,z,36,{z,out})==0 && p.memory.load(out,32)==dd);com(p,dd,2,{dd});
    p.memory.store(query,DDSCAPS_ZBUFFER,32);for(U32 off=4;off<16;off+=4)p.memory.store(query+off,0,32);
    CHECK(com(p,target,12,{target,query,out})==U32(DDERR_NOTFOUND) && p.memory.load(out,32)==0);
    auto small=make_surface(dd,true,32);CHECK(com(p,target,3,{target,small})==U32(DDERR_CANNOTATTACHSURFACE));com(p,small,2,{small});
    auto other_dd=root_dd(),foreign=make_surface(other_dd,true);CHECK(com(p,target,3,{target,foreign})==U32(DDERR_CANNOTATTACHSURFACE));com(p,foreign,2,{foreign});com(p,other_dd,2,{other_dd});
    CHECK(com(p,target,3,{target,z})==0);
    CHECK(com(p,target,3,{target,z})==U32(DDERR_SURFACEALREADYATTACHED));
    auto second_target=make_surface(dd,false);CHECK(com(p,second_target,3,{second_target,z})==U32(DDERR_CANNOTATTACHSURFACE));
    fault([&]{com(p,target,12,{target,query,out+1});},wr::FaultKind::memory);
    // An attachment retains the resource independently of the original token.
    CHECK(com(p,z,2,{z})==0);fault([&]{com(p,z,14,{z,query});},wr::FaultKind::unsupported);
    CHECK(com(p,target,12,{target,query,out})==0);auto held=p.memory.load(out,32);CHECK(held!=z);z=held;
    guid(p,base,IID_IDirect3DHALDevice);CHECK(com(p,d3d,4,{d3d,base,target,out})==0);auto device=p.memory.load(out,32);
    for(auto [state,value]:{std::pair<U32,U32>{7,1},{14,1},{23,4}}){CHECK(com(p,device,21,{device,state,out})==0 && p.memory.load(out,32)==value);}
    CHECK(com(p,device,20,{device,22,1})==0);
    auto state=[&](U32 key,U32 value){CHECK(com(p,device,20,{device,key,value})==0);};
    auto clear=[&](U32 flags=3,float value=1){return com(p,device,10,{device,0,0,flags,0xff183050,std::bit_cast<U32>(value),0});};
    auto begin=[&]{CHECK(com(p,device,5,{device})==0);};auto end=[&]{CHECK(com(p,device,6,{device})==0);};
    auto draw=[&](float zvalue,U32 color){
        const std::array<wr::host9::Vertex,3> v{{{8,8,zvalue,1,color},{56,8,zvalue,1,color},{8,56,zvalue,1,color}}};
        p.memory.copy_in(verts,std::span(reinterpret_cast<const std::uint8_t*>(v.data()),sizeof(v)));
        CHECK(com(p,device,25,{device,4,0x44,verts,3,0})==0);
    };
    auto pixel=[&](U32 x=16,U32 y=16){p.memory.store(lk,124,32);CHECK(com(p,target,25,{target,0,lk,0x11,0})==0);auto bits=p.memory.load(lk+36,32),pitch=p.memory.load(lk+16,32);auto result=p.memory.load(bits+y*pitch+4*x,32)&0xffffff;CHECK(com(p,target,32,{target,0})==0);return result;};
    CHECK(clear()==0);begin();draw(.25f,0xffff0000);draw(.75f,0xff0000ff);end();CHECK(pixel()==0xff0000);
    // All eight comparison functions are checked against exact binary fractions.
    for(U32 fn=1;fn<=8;++fn)for(float incoming:{.25f,.5f,.75f}){
        CHECK(clear(3,.5f)==0);state(23,fn);state(14,0);begin();draw(incoming,0xff00ff00);end();
        const bool pass=fn==1?false:fn==2?incoming<.5f:fn==3?incoming==.5f:fn==4?incoming<=.5f:fn==5?incoming>.5f:fn==6?incoming!=.5f:fn==7?incoming>=.5f:true;
        CHECK(pixel()==(pass?0x00ff00u:0x183050u));
    }
    state(23,4);state(14,0);CHECK(clear()==0);begin();draw(.25f,0xffff0000);draw(.75f,0xff0000ff);end();CHECK(pixel()==0xff);
    state(14,1);state(7,0);CHECK(clear()==0);begin();draw(.25f,0xffff0000);draw(.75f,0xff0000ff);end();CHECK(pixel()==0xff);
    state(7,1);CHECK(clear()==0);begin();draw(.25f,0xffff0000);end();
    CHECK(clear(1)==0);begin();draw(.75f,0xff0000ff);end();CHECK(pixel()==0x183050); // Z survives scene/clear boundaries.
    CHECK(com(p,target,8,{target,0,z})==0);
    begin();fault([&]{com(p,target,3,{target,z});},wr::FaultKind::unsupported);end();
    CHECK(com(p,target,3,{target,z})==0);
    begin();draw(.75f,0xff0000ff);fault([&]{com(p,target,8,{target,0,z});},wr::FaultKind::unsupported);end();CHECK(pixel()==0x183050); // Z survives rebind.
    CHECK(clear(2)==0);begin();draw(.75f,0xff0000ff);end();CHECK(pixel()==0xff);
    // Rejected state/clear operations leave existing color and depth intact.
    CHECK(clear()==0);begin();draw(.25f,0xffff0000);end();
    for(float bad:{-1.f,2.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()})CHECK(clear(3,bad)!=0);
    for(U32 bad:{0u,9u,0xffffffffu})CHECK(com(p,device,20,{device,23,bad})!=0);
    fault([&]{com(p,device,20,{device,7,2});},wr::FaultKind::unsupported);
    fault([&]{clear(7);},wr::FaultKind::unsupported);
    begin();draw(.75f,0xff0000ff);end();CHECK(pixel()==0xff0000);
    // Clear affects only the selected viewport; inactive z is ignored for color.
    auto viewport=[&](U32 x,U32 y,U32 w,U32 h){for(auto [off,val]:{std::pair<U32,U32>{0,x},{4,y},{8,w},{12,h},{16,0},{20,0x3f800000}})p.memory.store(view+off,val,32);CHECK(com(p,device,13,{device,view})==0);};
    CHECK(clear(1,std::numeric_limits<float>::quiet_NaN())==0);viewport(8,8,16,16);CHECK(clear(2)==0);viewport(0,0,64,64);
    begin();draw(.75f,0xff0000ff);end();CHECK(pixel()==0xff && pixel(30,12)==0x183050);
    fault([&]{com(p,z,25,{z,0,lk,0x11,0});},wr::FaultKind::unsupported); // no pretend CPU-visible Z data.
    // A D16 buffer cannot silently migrate to another native D3D9 device.
    CHECK(com(p,target,8,{target,0,z})==0);guid(p,base,IID_IDirect3DHALDevice);
    CHECK(com(p,d3d,4,{d3d,base,second_target,out})==0);auto second_device=p.memory.load(out,32);
    CHECK(com(p,second_target,3,{second_target,z})!=0);
    CHECK(com(p,target,3,{target,z})==0);com(p,second_device,2,{second_device});com(p,second_target,2,{second_target});
    com(p,target,2,{target});CHECK(com(p,device,9,{device,out})==0);target=p.memory.load(out,32);
    p.memory.store(query,DDSCAPS_ZBUFFER,32);for(U32 off=4;off<16;off+=4)p.memory.store(query+off,0,32);
    CHECK(com(p,target,12,{target,query,out})==0 && p.memory.load(out,32)==z);com(p,z,2,{z});
    CHECK(com(p,target,8,{target,1,z})==U32(DDERR_INVALIDPARAMS));CHECK(com(p,target,8,{target,0,z})==0);
    CHECK(com(p,target,8,{target,0,z})==U32(DDERR_SURFACENOTATTACHED));
    com(p,z,2,{z});com(p,target,2,{target});com(p,device,2,{device});com(p,d3d,2,{d3d});com(p,dd,2,{dd});
    CHECK(api(p,"user32.dll","DestroyWindow",{window}));CHECK(api(p,"user32.dll","UnregisterClassA",{name,0x400000}));
    std::cout<<p.directdraw()->report()<<'\n';
}
}
int main(){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);try{depth_tests();std::cout<<checks<<" D16 depth, occlusion, comparison, clear, attachment and lifetime assertions passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
