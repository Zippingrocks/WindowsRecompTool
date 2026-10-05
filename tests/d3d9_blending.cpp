// Native Windows fixed-function alpha blending acceptance.
// Synthetic component test; not an E3 frame.
#include "winrecomp/d3d9_host.hpp"
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d9.h>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
unsigned checks{};
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("D3D9 blending: " #x);}while(0)
LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcA(h,m,w,l);}
std::vector<std::uint8_t> solid(std::uint8_t r,std::uint8_t g,std::uint8_t b,std::uint8_t a){
    std::vector<std::uint8_t> out(64);
    for(std::size_t i=0;i<16;++i){out[i*4]=b;out[i*4+1]=g;out[i*4+2]=r;out[i*4+3]=a;}
    return out;
}
std::array<wr::host9::TexturedVertex,3> tri(float z){
    return {{{8,8,z,1,0xffffffffu,.25f,.25f},{56,8,z,1,0xffffffffu,.25f,.25f},{8,56,z,1,0xffffffffu,.25f,.25f}}};
}
std::uint32_t rgb(const std::vector<std::uint8_t>& bytes,unsigned x=16,unsigned y=16){
    auto i=(std::size_t(y)*64+x)*4;
    return (std::uint32_t(bytes[i+2])<<16)|(std::uint32_t(bytes[i+1])<<8)|bytes[i];
}
bool close8(unsigned a,unsigned b){return a+1>=b && b+1>=a;}
void expect_mix(std::uint32_t c){
    CHECK(close8((c>>16)&255,128));
    CHECK(close8((c>>8)&255,127));
    CHECK((c&255)<=1);
}
}
int main(){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    HINSTANCE instance=GetModuleHandleA(nullptr);const char* cls="WinRecompBlendBoundary";
    WNDCLASSA wc{};wc.lpfnWndProc=proc;wc.hInstance=instance;wc.lpszClassName=cls;
    if(!RegisterClassA(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return 2;
    HWND window=CreateWindowExA(0,cls,"blend",WS_POPUP|WS_VISIBLE,50,50,64,64,nullptr,nullptr,instance,nullptr);
    if(!window)return 3;
    try{
        auto factory=wr::host9::make_factory();CHECK(bool(factory));
        wr::host9::Capabilities caps{};CHECK(factory->capabilities(caps)==wr::host9::Ok);CHECK(caps.depth16);
        CHECK(caps.src_blend&D3DPBLENDCAPS_SRCALPHA);CHECK(caps.dst_blend&D3DPBLENDCAPS_INVSRCALPHA);
        std::unique_ptr<wr::host9::Device> device;
        CHECK(factory->create(reinterpret_cast<std::uintptr_t>(window),64,64,device)==wr::host9::Ok);
        std::shared_ptr<wr::host9::Depth> depth;CHECK(device->create_depth(depth)==wr::host9::Ok);CHECK(device->bind_depth(depth)==wr::host9::Ok);
        CHECK(device->set_state(D3DRS_ZENABLE,TRUE)==wr::host9::Ok);
        CHECK(device->set_state(D3DRS_ZWRITEENABLE,TRUE)==wr::host9::Ok);
        CHECK(device->set_state(D3DRS_ZFUNC,D3DCMP_LESSEQUAL)==wr::host9::Ok);
        CHECK(device->set_state(D3DRS_CULLMODE,D3DCULL_NONE)==wr::host9::Ok);
        CHECK(device->set_state(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA)==wr::host9::Ok);
        CHECK(device->set_state(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA)==wr::host9::Ok);
        CHECK(device->set_state(D3DRS_ALPHABLENDENABLE,TRUE)==wr::host9::Ok);
        std::uint32_t state{};
        CHECK(device->get_state(D3DRS_SRCBLEND,state)==wr::host9::Ok && state==D3DBLEND_SRCALPHA);
        CHECK(device->get_state(D3DRS_DESTBLEND,state)==wr::host9::Ok && state==D3DBLEND_INVSRCALPHA);
        CHECK(device->get_state(D3DRS_ALPHABLENDENABLE,state)==wr::host9::Ok && state==TRUE);

        auto green=solid(0,255,0,255),red50=solid(255,0,0,128),blue=solid(0,0,255,255);
        auto far_tri=tri(.75f),near_tri=tri(.25f),behind_tri=tri(.90f);
        CHECK(device->clear_buffers(D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff000000u,1.f)==wr::host9::Ok);
        CHECK(device->begin()==wr::host9::Ok);
        CHECK(device->texture(4,4,true,green)==wr::host9::Ok);CHECK(device->textured_triangles(far_tri)==wr::host9::Ok);
        CHECK(device->texture(4,4,true,red50)==wr::host9::Ok);CHECK(device->textured_triangles(near_tri)==wr::host9::Ok);
        CHECK(device->texture(4,4,true,blue)==wr::host9::Ok);CHECK(device->textured_triangles(behind_tri)==wr::host9::Ok);
        CHECK(device->end()==wr::host9::Ok);
        std::vector<std::uint8_t> pixels;CHECK(device->readback(pixels)==wr::host9::Ok);expect_mix(rgb(pixels));CHECK(rgb(pixels,0,0)==0);

        // Same blend/depth behavior through the WORD-indexed textured path.
        CHECK(device->clear_buffers(D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff000000u,1.f)==wr::host9::Ok);
        const std::array<std::uint16_t,3> indices{0,1,2};
        CHECK(device->begin()==wr::host9::Ok);
        CHECK(device->texture(4,4,true,green)==wr::host9::Ok);CHECK(device->indexed_textured_triangles(far_tri,indices)==wr::host9::Ok);
        CHECK(device->texture(4,4,true,red50)==wr::host9::Ok);CHECK(device->indexed_textured_triangles(near_tri,indices)==wr::host9::Ok);
        CHECK(device->texture(4,4,true,blue)==wr::host9::Ok);CHECK(device->indexed_textured_triangles(behind_tri,indices)==wr::host9::Ok);
        CHECK(device->end()==wr::host9::Ok);CHECK(device->readback(pixels)==wr::host9::Ok);expect_mix(rgb(pixels));
        CHECK(device->present()==wr::host9::Ok);

        // Unsupported factors and invalid booleans must remain failures.
        CHECK(wr::host9::failed(device->set_state(D3DRS_SRCBLEND,D3DBLEND_DESTCOLOR)));
        CHECK(wr::host9::failed(device->set_state(D3DRS_DESTBLEND,D3DBLEND_SRCCOLOR)));
        CHECK(wr::host9::failed(device->set_state(D3DRS_ALPHABLENDENABLE,2)));
        CHECK(device->set_state(D3DRS_ALPHABLENDENABLE,FALSE)==wr::host9::Ok);
        CHECK(device->set_state(D3DRS_SRCBLEND,D3DBLEND_ONE)==wr::host9::Ok);
        CHECK(device->set_state(D3DRS_DESTBLEND,D3DBLEND_ZERO)==wr::host9::Ok);

        DestroyWindow(window);window=nullptr;UnregisterClassA(cls,instance);
        std::cout<<checks<<" alpha blend/depth/index/presentation assertions passed\n";return 0;
    }catch(const std::exception& e){
        std::cerr<<e.what()<<'\n';if(window)DestroyWindow(window);UnregisterClassA(cls,instance);return 1;
    }
}
