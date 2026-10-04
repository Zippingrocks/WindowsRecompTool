#include "winrecomp/d3d9_host.hpp"
#define main winrecomp_existing_boundary_main
#include "d3d9_boundary.cpp"
#undef main
#include <limits>
namespace {
void indexed(){
    wr::ProcessOptions options;options.legacy_d3d9=true;wr::Process p(step,options);current=&p;p.load(image());
    const auto base=p.allocate_bytes(16384),out=base+256,wc=base+512,desc=base+1024,lk=base+1280;
    const auto indices=base+4096-12,verts=base+8192;
    p.memory.protect(base+4096,4096,0);p.memory.protect(base+12288,4096,0);
    auto name=p.put_string("WinRecompIndexedBoundary");
    for(auto [off,value]:{std::pair<U32,U32>{0,48},{8,Wnd},{20,0x400000},{40,name}})p.memory.store(wc+off,value,32);
    CHECK(api(p,"user32.dll","RegisterClassExA",{wc}));
    auto window=api(p,"user32.dll","CreateWindowExA",{0,name,name,0x00cf0000,20,20,160,160,0,0,0x400000,0});CHECK(window);
    guid(p,base,IID_IDirectDraw7);CHECK(api(p,"ddraw.dll","DirectDrawCreateEx",{0,out,base,0})==0);auto dd=p.memory.load(out,32);
    CHECK(com(p,dd,20,{dd,window,DDSCL_NORMAL})==0);guid(p,base,IID_IDirect3D7);
    CHECK(com(p,dd,0,{dd,base,out})==0);auto root=p.memory.load(out,32);
    auto surface=[&](bool texture){
        for(U32 n=0;n<124;n+=4)p.memory.store(desc+n,0,32);
        for(auto [off,value]:{std::pair<U32,U32>{0,124},{4,0x1007},{8,texture?8u:64u},{12,texture?8u:64u},
            {72,32},{76,DDPF_RGB},{84,32},{88,0xff0000},{92,0xff00},{96,0xff},
            {104,texture?U32(DDSCAPS_TEXTURE|DDSCAPS_SYSTEMMEMORY):U32(DDSCAPS_OFFSCREENPLAIN|DDSCAPS_3DDEVICE)}})p.memory.store(desc+off,value,32);
        CHECK(com(p,dd,6,{dd,desc,out,0})==0);return p.memory.load(out,32);
    };
    auto target=surface(false);guid(p,base,IID_IDirect3DHALDevice);
    CHECK(com(p,root,4,{root,base,target,out})==0);auto device=p.memory.load(out,32);
    CHECK(com(p,device,20,{device,22,1})==0);
    auto put_indices=[&](std::initializer_list<std::uint16_t> words){U32 n=0;for(auto i:words)p.memory.store(indices+2*n++,i,16);};
    std::array<wr::host9::Vertex,8> vertex{};for(auto& v:vertex)v={-100,-100,.5f,1,0xffffffff};
    vertex[1]={8,8,.5f,1,0xff4080c0};vertex[3]={56,8,.5f,1,0xff4080c0};
    vertex[5]={8,56,.5f,1,0xff4080c0};vertex[7]={56,56,.5f,1,0xff4080c0};
    auto write_vertices=[&]{p.memory.copy_in(verts,std::span(reinterpret_cast<const std::uint8_t*>(vertex.data()),sizeof(vertex)));};
    write_vertices();put_indices({1,3,5,3,7,5});
    auto draw=[&](U32 count=8,U32 n=6,U32 pointer=0,U32 ip=0,U32 fvf=0x44){return com(p,device,26,{device,4,fvf,pointer?pointer:verts,count,ip?ip:indices,n,0});};
    auto pixels=[&]{
        p.memory.store(lk,124,32);CHECK(com(p,target,25,{target,0,lk,0x11,0})==0);
        auto ptr=p.memory.load(lk+36,32),pitch=p.memory.load(lk+16,32);std::vector<U32> result;
        for(U32 y=0;y<64;++y)for(U32 x=0;x<64;++x)result.push_back(p.memory.load(ptr+y*pitch+x*4,32)&0xffffff);
        CHECK(com(p,target,32,{target,0})==0);return result;
    };
    CHECK(draw()!=0);CHECK(com(p,device,10,{device,0,0,1,0xff183050,0x3f800000,0})==0);
    CHECK(com(p,device,5,{device})==0);
    CHECK(draw()==0);
    // Rejected inputs do not change a completed frame, leak guest writes or turn
    // an error into a successful draw. Count gates run before buffer accesses.
    CHECK(draw(0)!=0);CHECK(draw(65536)!=0);CHECK(draw(8,0)!=0);CHECK(draw(8,5)!=0);CHECK(draw(8,65538)!=0);
    fault([&]{draw(8,9);},wr::FaultKind::memory);
    fault([&]{draw(8,6,base+12280);},wr::FaultKind::memory);
    fault([&]{draw(8,6,0,0xfffffffcu);},wr::FaultKind::memory);
    fault([&]{com(p,device,26,{device,5,0x44,verts,8,indices,6,0});},wr::FaultKind::unsupported);
    fault([&]{com(p,device,26,{device,4,0x244,verts,8,indices,6,0});},wr::FaultKind::unsupported);
    fault([&]{com(p,device,26,{device,4,0x44,verts,8,indices,6,1});},wr::FaultKind::unsupported);
    put_indices({1,3,8,3,7,5});CHECK(draw()!=0);put_indices({1,3,65535,3,7,5});CHECK(draw()!=0);
    put_indices({1,3,5,3,7,5});vertex[1].rhw=0;write_vertices();CHECK(draw()!=0);vertex[1].rhw=1;
    vertex[1].x=std::numeric_limits<float>::quiet_NaN();write_vertices();CHECK(draw()!=0);vertex[1].x=8;
    vertex[0].x=std::numeric_limits<float>::quiet_NaN();write_vertices();CHECK(draw()==0); // unused payload
    put_indices({1,1,1});CHECK(draw(8,3)==0); // degenerate primitive is legal, no visible pixels
    CHECK(com(p,device,6,{device})==0);auto baseline=pixels();
    CHECK(baseline[0]==0x183050 && baseline[16*64+16]==0x4080c0 && baseline[50*64+50]==0x4080c0);
    CHECK(std::count(baseline.begin(),baseline.end(),0x4080c0)==48*48);
    // Upper WORD values must not be sign-extended or decoded as DWORD indices.
    auto big=p.allocate_bytes(65535*20);
    p.memory.copy_in(big+65532*20,std::span(reinterpret_cast<const std::uint8_t*>(&vertex[1]),20));
    p.memory.copy_in(big+65533*20,std::span(reinterpret_cast<const std::uint8_t*>(&vertex[3]),20));
    p.memory.copy_in(big+65534*20,std::span(reinterpret_cast<const std::uint8_t*>(&vertex[5]),20));
    put_indices({65532,65533,65534});CHECK(com(p,device,5,{device})==0);CHECK(draw(65535,3,big)==0);CHECK(com(p,device,6,{device})==0);
    CHECK(pixels()==baseline);
    // Texture edits after binding, indexed UVs, bounds and retained references.
    auto texture=surface(true);
    auto fill=[&](U32 color){p.memory.store(lk,124,32);CHECK(com(p,texture,25,{texture,0,lk,1,0})==0);
        auto ptr=p.memory.load(lk+36,32),pitch=p.memory.load(lk+16,32);
        for(U32 y=0;y<8;++y)for(U32 x=0;x<8;++x)p.memory.store(ptr+y*pitch+x*4,color,32);
        CHECK(com(p,texture,32,{texture,0})==0);};
    fill(0xff123456);CHECK(com(p,device,35,{device,0,texture})==0);
    for(auto [key,value]:{std::pair<U32,U32>{1,2},{2,2},{13,3},{14,3},{16,1},{17,1},{18,1}})CHECK(com(p,device,37,{device,0,key,value})==0);
    std::array<wr::host9::TexturedVertex,8> tv{};
    for(unsigned i=0;i<8;++i){auto& a=vertex[i];tv[i]={a.x,a.y,a.z,a.rhw,a.diffuse,.25f,.25f};}
    auto write_tv=[&]{p.memory.copy_in(verts,std::span(reinterpret_cast<const std::uint8_t*>(tv.data()),sizeof(tv)));};
    write_tv();put_indices({1,3,5,3,7,5});CHECK(com(p,device,5,{device})==0);
    CHECK(draw(8,6,0,0,0x144)==0);
    // Invalid UVs must be caught before the texture binding is changed.
    tv[1].u=std::numeric_limits<float>::infinity();write_tv();CHECK(draw(8,6,0,0,0x144)!=0);tv[1].u=.25f;write_tv();
    p.memory.store(lk,124,32);CHECK(com(p,texture,25,{texture,0,lk,1,0})==0);
    fault([&]{draw(8,6,0,0,0x144);},wr::FaultKind::unsupported);CHECK(com(p,texture,32,{texture,0})==0);
    fill(0xffabcdef);CHECK(draw(8,6,0,0,0x144)==0);
    com(p,texture,2,{texture});CHECK(draw(8,6,0,0,0x144)==0);
    CHECK(com(p,device,6,{device})==0);auto textured=pixels();CHECK(textured[16*64+16]==0xabcdef && textured[50*64+50]==0xabcdef);
    CHECK(com(p,device,35,{device,0,0})==0);
    CHECK(com(p,device,5,{device})==0);CHECK(draw(8,6,0,0,0x144)!=0);CHECK(com(p,device,6,{device})==0);
    CHECK(pixels()==textured);CHECK(draw()!=0);
    com(p,device,2,{device});fault([&]{draw();},wr::FaultKind::unsupported);
    com(p,target,2,{target});com(p,root,2,{root});com(p,dd,2,{dd});
    CHECK(api(p,"user32.dll","DestroyWindow",{window}));CHECK(api(p,"user32.dll","UnregisterClassA",{name,0x400000}));
    std::cout<<p.directdraw()->report()<<'\n';
}
}
int main(){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);try{indexed();std::cout<<checks<<" indexed draw, WORD bounds, pixel and lifetime assertions passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
