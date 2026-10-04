// Native Windows composite renderer acceptance: texture + D16 + indexed geometry + Present.
// Synthetic component test only; this is not an E3 frame.
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
#define CHECK(x) do { ++checks; if(!(x)) throw std::runtime_error("combined D3D9: " #x); } while(0)
LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcA(h,m,w,l); }
std::vector<std::uint8_t> solid(std::uint8_t r,std::uint8_t g,std::uint8_t b) {
    std::vector<std::uint8_t> out(64);
    for(std::size_t i=0;i<16;++i){out[i*4]=b;out[i*4+1]=g;out[i*4+2]=r;out[i*4+3]=0xff;}
    return out;
}
std::uint32_t rgb_at(const std::vector<std::uint8_t>& bytes,unsigned x,unsigned y) {
    const auto i=(std::size_t(y)*64+x)*4;
    return (std::uint32_t(bytes[i+2])<<16)|(std::uint32_t(bytes[i+1])<<8)|bytes[i];
}
std::array<wr::host9::TexturedVertex,3> tri(float z) {
    return {{{8,8,z,1,0xffffffffu,.25f,.25f},{56,8,z,1,0xffffffffu,.25f,.25f},{8,56,z,1,0xffffffffu,.25f,.25f}}};
}
}
int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    HINSTANCE instance=GetModuleHandleA(nullptr);const char* cls="WinRecompCombinedRenderer";
    WNDCLASSA wc{};wc.lpfnWndProc=proc;wc.hInstance=instance;wc.lpszClassName=cls;
    if(!RegisterClassA(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return 2;
    HWND window=CreateWindowExA(0,cls,"combined",WS_POPUP|WS_VISIBLE,40,40,64,64,nullptr,nullptr,instance,nullptr);
    if(!window)return 3;
    try {
        auto factory=wr::host9::make_factory();CHECK(bool(factory));
        wr::host9::Capabilities caps{};CHECK(!wr::host9::failed(factory->capabilities(caps)));CHECK(caps.depth16);
        std::unique_ptr<wr::host9::Device> device;
        CHECK(!wr::host9::failed(factory->create(reinterpret_cast<std::uintptr_t>(window),64,64,device)));CHECK(bool(device));
        std::shared_ptr<wr::host9::Depth> depth;CHECK(device->create_depth(depth)==wr::host9::Ok);CHECK(bool(depth));
        CHECK(device->bind_depth(depth)==wr::host9::Ok);
        CHECK(device->set_state(D3DRS_ZENABLE,TRUE)==wr::host9::Ok);
        CHECK(device->set_state(D3DRS_ZWRITEENABLE,TRUE)==wr::host9::Ok);
        CHECK(device->set_state(D3DRS_ZFUNC,D3DCMP_LESSEQUAL)==wr::host9::Ok);
        CHECK(device->set_state(D3DRS_CULLMODE,D3DCULL_NONE)==wr::host9::Ok);
        CHECK(device->set_viewport({0,0,64,64,0,1})==wr::host9::Ok);
        auto green=solid(0,255,0),red=solid(255,0,0),blue=solid(0,0,255);
        CHECK(device->clear_buffers(D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff183050u,1.f)==wr::host9::Ok);
        CHECK(device->texture(4,4,true,green)==wr::host9::Ok);CHECK(device->begin()==wr::host9::Ok);
        auto far=tri(.75f),near=tri(.25f);CHECK(device->textured_triangles(far)==wr::host9::Ok);
        CHECK(device->texture(4,4,true,red)==wr::host9::Ok);CHECK(device->textured_triangles(near)==wr::host9::Ok);
        CHECK(device->end()==wr::host9::Ok);
        std::vector<std::uint8_t> pixels;CHECK(device->readback(pixels)==wr::host9::Ok);CHECK(pixels.size()==64u*64u*4u);
        CHECK(rgb_at(pixels,16,16)==0xff0000u);CHECK(rgb_at(pixels,0,0)==0x183050u);
        CHECK(device->clear_buffers(D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff183050u,1.f)==wr::host9::Ok);
        const std::array<std::uint16_t,3> indices{0,1,2};CHECK(device->texture(4,4,true,green)==wr::host9::Ok);
        CHECK(device->begin()==wr::host9::Ok);CHECK(device->indexed_textured_triangles(far,indices)==wr::host9::Ok);
        CHECK(device->texture(4,4,true,blue)==wr::host9::Ok);CHECK(device->indexed_textured_triangles(near,indices)==wr::host9::Ok);
        CHECK(wr::host9::failed(device->present()));CHECK(device->end()==wr::host9::Ok);
        CHECK(device->readback(pixels)==wr::host9::Ok);CHECK(rgb_at(pixels,16,16)==0x0000ffu);CHECK(rgb_at(pixels,0,0)==0x183050u);
        CHECK(device->present()==wr::host9::Ok);
        CHECK(device->bind_depth({})==wr::host9::Ok);CHECK(wr::host9::failed(device->set_state(D3DRS_ZENABLE,TRUE)));
        CHECK(device->set_state(D3DRS_ZENABLE,FALSE)==wr::host9::Ok);CHECK(device->unbind_texture()==wr::host9::Ok);
        DestroyWindow(window);window=nullptr;UnregisterClassA(cls,instance);
        std::cout<<checks<<" combined texture/depth/index/presentation assertions passed\n";return 0;
    } catch(const std::exception& e) {
        std::cerr<<e.what()<<'\n';if(window)DestroyWindow(window);UnregisterClassA(cls,instance);return 1;
    }
}