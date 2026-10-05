// Native Windows alpha-test/cutout acceptance.
// Verifies discarded texels do not write D16 depth. Synthetic, not E3.
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
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("D3D9 alpha test: " #x);}while(0)
LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcA(h,m,w,l);}
std::vector<std::uint8_t> cutout(){
    std::vector<std::uint8_t> out(64);
    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x){
        const auto i=(y*4+x)*4;const bool opaque=x>=2;
        out[i+0]=0;out[i+1]=opaque?255:0;out[i+2]=opaque?0:255;out[i+3]=opaque?255:0;
    }
    return out;
}
std::vector<std::uint8_t> solid(std::uint8_t r,std::uint8_t g,std::uint8_t b){
    std::vector<std::uint8_t> out(64);
    for(std::size_t i=0;i<16;++i){out[i*4]=b;out[i*4+1]=g;out[i*4+2]=r;out[i*4+3]=255;}
    return out;
}
std::array<wr::host9::TexturedVertex,6> quad(float z){
    using V=wr::host9::TexturedVertex;
    return {{V{8,8,z,1,0xffffffffu,.125f,.125f},V{56,8,z,1,0xffffffffu,.875f,.125f},V{8,56,z,1,0xffffffffu,.125f,.875f},
             V{8,56,z,1,0xffffffffu,.125f,.875f},V{56,8,z,1,0xffffffffu,.875f,.125f},V{56,56,z,1,0xffffffffu,.875f,.875f}}};
}
std::array<wr::host9::TexturedVertex,4> indexed_quad(float z){
    using V=wr::host9::TexturedVertex;
    return {{V{8,8,z,1,0xffffffffu,.125f,.125f},V{56,8,z,1,0xffffffffu,.875f,.125f},
             V{8,56,z,1,0xffffffffu,.125f,.875f},V{56,56,z,1,0xffffffffu,.875f,.875f}}};
}
std::uint32_t rgb(const std::vector<std::uint8_t>& bytes,unsigned x,unsigned y){
    const auto i=(std::size_t(y)*64+x)*4;
    return (std::uint32_t(bytes[i+2])<<16)|(std::uint32_t(bytes[i+1])<<8)|bytes[i];
}
}
int main(){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    HINSTANCE instance=GetModuleHandleA(nullptr);const char* cls="WinRecompAlphaTest";
    WNDCLASSA wc{};wc.lpfnWndProc=proc;wc.hInstance=instance;wc.lpszClassName=cls;
    if(!RegisterClassA(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return 2;
    HWND window=CreateWindowExA(0,cls,"alpha-test",WS_POPUP|WS_VISIBLE,60,60,64,64,nullptr,nullptr,instance,nullptr);
    if(!window)return 3;
    try{
        auto factory=wr::host9::make_factory();CHECK(bool(factory));
        wr::host9::Capabilities caps{};CHECK(factory->capabilities(caps)==wr::host9::Ok);
        CHECK(caps.depth16);CHECK(caps.alpha_compare&D3DPCMPCAPS_GREATER);
        std::unique_ptr<wr::host9::Device> device;
        CHECK(factory->create(reinterpret_cast<std::uintptr_t>(window),64,64,device)==wr::host9::Ok);
        std::shared_ptr<wr::host9::Depth> depth;CHECK(device->create_depth(depth)==wr::host9::Ok);CHECK(device->bind_depth(depth)==wr::host9::Ok);
        CHECK(device->set_state(D3DRS_ZENABLE,TRUE)==wr::host9::Ok);
        CHECK(device->set_state(D3DRS_ZWRITEENABLE,TRUE)==wr::host9::Ok);
        CHECK(device->set_state(D3DRS_ZFUNC,D3DCMP_LESSEQUAL)==wr::host9::Ok);
        CHECK(device->set_state(D3DRS_CULLMODE,D3DCULL_NONE)==wr::host9::Ok);
        CHECK(device->set_state(D3DRS_ALPHAREF,127)==wr::host9::Ok);
        CHECK(device->set_state(D3DRS_ALPHAFUNC,D3DCMP_GREATER)==wr::host9::Ok);
        CHECK(device->set_state(D3DRS_ALPHATESTENABLE,TRUE)==wr::host9::Ok);
        std::uint32_t state{};
        CHECK(device->get_state(D3DRS_ALPHAREF,state)==wr::host9::Ok && state==127);
        CHECK(device->get_state(D3DRS_ALPHAFUNC,state)==wr::host9::Ok && state==D3DCMP_GREATER);
        CHECK(device->get_state(D3DRS_ALPHATESTENABLE,state)==wr::host9::Ok && state==TRUE);

        auto mask=cutout(),blue=solid(0,0,255);
        auto front=quad(.25f),back=quad(.75f);
        CHECK(device->clear_buffers(D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff000000u,1.f)==wr::host9::Ok);
        CHECK(device->begin()==wr::host9::Ok);
        CHECK(device->texture(4,4,true,mask)==wr::host9::Ok);CHECK(device->textured_triangles(front)==wr::host9::Ok);
        CHECK(device->texture(4,4,true,blue)==wr::host9::Ok);CHECK(device->textured_triangles(back)==wr::host9::Ok);
        CHECK(device->end()==wr::host9::Ok);
        std::vector<std::uint8_t> pixels;CHECK(device->readback(pixels)==wr::host9::Ok);
        CHECK(rgb(pixels,16,32)==0x0000ffu); // transparent front texel discarded; back remains visible.
        CHECK(rgb(pixels,48,32)==0x00ff00u); // opaque front texel wrote color and depth.
        CHECK(rgb(pixels,0,0)==0);

        // Repeat through indexed textured geometry.
        auto ifront=indexed_quad(.25f),iback=indexed_quad(.75f);
        const std::array<std::uint16_t,6> indices{0,1,2,2,1,3};
        CHECK(device->clear_buffers(D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff000000u,1.f)==wr::host9::Ok);
        CHECK(device->begin()==wr::host9::Ok);
        CHECK(device->texture(4,4,true,mask)==wr::host9::Ok);CHECK(device->indexed_textured_triangles(ifront,indices)==wr::host9::Ok);
        CHECK(device->texture(4,4,true,blue)==wr::host9::Ok);CHECK(device->indexed_textured_triangles(iback,indices)==wr::host9::Ok);
        CHECK(device->end()==wr::host9::Ok);CHECK(device->readback(pixels)==wr::host9::Ok);
        CHECK(rgb(pixels,16,32)==0x0000ffu);CHECK(rgb(pixels,48,32)==0x00ff00u);
        CHECK(device->present()==wr::host9::Ok);

        CHECK(wr::host9::failed(device->set_state(D3DRS_ALPHAREF,256)));
        CHECK(wr::host9::failed(device->set_state(D3DRS_ALPHAFUNC,0)));
        CHECK(wr::host9::failed(device->set_state(D3DRS_ALPHAFUNC,9)));
        CHECK(wr::host9::failed(device->set_state(D3DRS_ALPHATESTENABLE,2)));
        CHECK(device->set_state(D3DRS_ALPHATESTENABLE,FALSE)==wr::host9::Ok);

        DestroyWindow(window);window=nullptr;UnregisterClassA(cls,instance);
        std::cout<<checks<<" alpha-test cutout/depth/index/presentation assertions passed\n";return 0;
    }catch(const std::exception& e){
        std::cerr<<e.what()<<'\n';if(window)DestroyWindow(window);UnregisterClassA(cls,instance);return 1;
    }
}
