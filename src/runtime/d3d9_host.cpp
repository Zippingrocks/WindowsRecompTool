#include "winrecomp/d3d9_host.hpp"
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d9.h>
#endif
namespace wr::host9 {
#ifndef _WIN32
std::unique_ptr<Factory> make_factory(){return {};}
#else
namespace {
template<class T> struct Com {
    T* p{};
    Com()=default;Com(const Com&)=delete;Com& operator=(const Com&)=delete;
    ~Com(){if(p)p->Release();}
};
constexpr unsigned MaxSize=2048,MaxTriangles=65536;
// This initial profile deliberately advertises no textures, depth, lights,
// transform pipeline, blending or unsupported vertex formats.
Capabilities bounded(const D3DCAPS9& c){return {
    c.PrimitiveMiscCaps&(D3DPMISCCAPS_CULLNONE|D3DPMISCCAPS_CULLCW|D3DPMISCCAPS_CULLCCW),
    c.ShadeCaps&D3DPSHADECAPS_COLORGOURAUDRGB,
    (c.MaxPrimitiveCount<MaxTriangles?c.MaxPrimitiveCount:MaxTriangles),c.MaxVertexW};}
class NativeDevice final:public Device {
    Com<IDirect3DDevice9> device;Com<IDirect3DSurface9> target,staging;
    DWORD thread=GetCurrentThreadId();unsigned width{},height{};bool scene{};
    Capabilities caps{};
    bool valid() const{return device.p && GetCurrentThreadId()==thread;}
public:
    Status initialize(IDirect3D9* factory,HWND window,unsigned w,unsigned h){
        if(!window || !IsWindow(window) || GetWindowThreadProcessId(window,nullptr)!=thread ||
           !w || !h || w>MaxSize || h>MaxSize)return Invalid;
        D3DCAPS9 c{};auto hr=factory->GetDeviceCaps(0,D3DDEVTYPE_HAL,&c);if(FAILED(hr))return Status(hr);
        caps=bounded(c);if(!caps.max_primitives || !(caps.misc&D3DPMISCCAPS_CULLNONE))return Unavailable;
        D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;
        pp.BackBufferWidth=w;pp.BackBufferHeight=h;pp.BackBufferFormat=D3DFMT_UNKNOWN;
        pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        hr=factory->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&device.p);
        if(FAILED(hr))return Status(hr);
        width=w;height=h;
        hr=device.p->CreateRenderTarget(w,h,D3DFMT_X8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target.p,nullptr);
        if(FAILED(hr))return Status(hr);
        hr=device.p->CreateOffscreenPlainSurface(w,h,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,&staging.p,nullptr);
        if(FAILED(hr))return Status(hr);
        hr=device.p->SetRenderTarget(0,target.p);if(FAILED(hr))return Status(hr);
        hr=device.p->SetDepthStencilSurface(nullptr);if(FAILED(hr))return Status(hr);
        for(auto [state,value]:{std::pair{D3DRS_ZENABLE,DWORD(FALSE)},
            {D3DRS_ZWRITEENABLE,DWORD(FALSE)},{D3DRS_LIGHTING,DWORD(FALSE)},
            {D3DRS_FOGENABLE,DWORD(FALSE)},{D3DRS_ALPHABLENDENABLE,DWORD(FALSE)},
            {D3DRS_SPECULARENABLE,DWORD(FALSE)},{D3DRS_CULLMODE,DWORD(D3DCULL_CCW)},
            {D3DRS_SHADEMODE,DWORD(D3DSHADE_GOURAUD)}}){
            hr=device.p->SetRenderState(state,value);if(FAILED(hr))return Status(hr);
        }
        hr=device.p->SetTexture(0,nullptr);if(FAILED(hr))return Status(hr);
        for(auto [state,value]:{std::pair{D3DTSS_COLOROP,DWORD(D3DTOP_SELECTARG1)},
            {D3DTSS_COLORARG1,DWORD(D3DTA_DIFFUSE)},{D3DTSS_ALPHAOP,DWORD(D3DTOP_SELECTARG1)},
            {D3DTSS_ALPHAARG1,DWORD(D3DTA_DIFFUSE)}}){
            hr=device.p->SetTextureStageState(0,state,value);if(FAILED(hr))return Status(hr);
        }
        hr=device.p->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);if(FAILED(hr))return Status(hr);
        return Status(device.p->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE));
    }
    ~NativeDevice() override {if(scene && valid())device.p->EndScene();}
    bool in_scene() const override{return scene;}
    Status begin() override{if(!valid() || scene)return Invalid;auto hr=device.p->BeginScene();if(SUCCEEDED(hr))scene=true;return Status(hr);}
    Status end() override{if(!valid() || !scene)return Invalid;auto hr=device.p->EndScene();if(SUCCEEDED(hr))scene=false;return Status(hr);}
    Status clear(std::uint32_t color) override{if(!valid())return Invalid;return Status(device.p->Clear(0,nullptr,D3DCLEAR_TARGET,color,1,0));}
    Status set_viewport(const Viewport& v) override{
        if(!valid() || !v.width || !v.height || std::uint64_t(v.x)+v.width>width ||
           std::uint64_t(v.y)+v.height>height || !std::isfinite(v.min_z) || !std::isfinite(v.max_z) ||
           v.min_z<0 || v.max_z>1 || v.min_z>v.max_z)return Invalid;
        D3DVIEWPORT9 native{v.x,v.y,v.width,v.height,v.min_z,v.max_z};return Status(device.p->SetViewport(&native));
    }
    Status get_viewport(Viewport& v) override{
        if(!valid())return Invalid;D3DVIEWPORT9 out{};auto hr=device.p->GetViewport(&out);
        if(SUCCEEDED(hr))v={out.X,out.Y,out.Width,out.Height,out.MinZ,out.MaxZ};return Status(hr);
    }
    Status set_state(std::uint32_t state,std::uint32_t value) override{
        if(!valid())return Invalid;
        switch(state){
        case D3DRS_ZENABLE:case D3DRS_ZWRITEENABLE:case D3DRS_LIGHTING:
        case D3DRS_ALPHABLENDENABLE:case D3DRS_FOGENABLE:case D3DRS_SPECULARENABLE:
            if(value!=FALSE)return Status(E_NOTIMPL);break;
        case D3DRS_CULLMODE:
            if(value<D3DCULL_NONE || value>D3DCULL_CCW)return Invalid;
            if(!(caps.misc&(value==D3DCULL_NONE?D3DPMISCCAPS_CULLNONE:value==D3DCULL_CW?D3DPMISCCAPS_CULLCW:D3DPMISCCAPS_CULLCCW)))return Status(E_NOTIMPL);break;
        case D3DRS_SHADEMODE:
            if(value!=D3DSHADE_GOURAUD)return Status(E_NOTIMPL);
            if(!(caps.shade&D3DPSHADECAPS_COLORGOURAUDRGB))return Status(E_NOTIMPL);break;
        default:return Status(E_NOTIMPL);
        }
        return Status(device.p->SetRenderState(D3DRENDERSTATETYPE(state),value));
    }
    Status get_state(std::uint32_t state,std::uint32_t& value) override{
        if(!valid())return Invalid;
        switch(state){case D3DRS_ZENABLE:case D3DRS_ZWRITEENABLE:case D3DRS_LIGHTING:
        case D3DRS_ALPHABLENDENABLE:case D3DRS_FOGENABLE:case D3DRS_SPECULARENABLE:
        case D3DRS_CULLMODE:case D3DRS_SHADEMODE:break;default:return Status(E_NOTIMPL);}
        DWORD result{};auto hr=device.p->GetRenderState(D3DRENDERSTATETYPE(state),&result);
        if(SUCCEEDED(hr))value=result;return Status(hr);
    }
    Status triangles(std::span<const Vertex> vertices) override{
        if(!valid() || !scene || vertices.empty() || vertices.size()%3 || vertices.size()/3>caps.max_primitives)return Invalid;
        for(const auto& v:vertices)if(!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) ||
            !std::isfinite(v.rhw) || v.rhw<=0)return Invalid;
        return Status(device.p->DrawPrimitiveUP(D3DPT_TRIANGLELIST,UINT(vertices.size()/3),vertices.data(),sizeof(Vertex)));
    }
    Status upload(std::span<const std::uint8_t> bytes) override{
        if(!valid() || scene || bytes.size()!=std::size_t(width)*height*4)return Invalid;
        D3DLOCKED_RECT lock{};auto hr=staging.p->LockRect(&lock,nullptr,0);if(FAILED(hr))return Status(hr);
        if(lock.Pitch<0 || unsigned(lock.Pitch)<width*4 || !lock.pBits){staging.p->UnlockRect();return Invalid;}
        for(unsigned row=0;row<height;++row)std::memcpy(static_cast<std::uint8_t*>(lock.pBits)+std::size_t(row)*lock.Pitch,bytes.data()+std::size_t(row)*width*4,width*4);
        hr=staging.p->UnlockRect();if(FAILED(hr))return Status(hr);
        return Status(device.p->UpdateSurface(staging.p,nullptr,target.p,nullptr));
    }
    Status readback(std::vector<std::uint8_t>& bytes) override{
        if(!valid() || scene)return Invalid;
        // Allocate before locking so allocation failure cannot leak a native lock.
        std::vector<std::uint8_t> out(std::size_t(width)*height*4);
        auto hr=device.p->GetRenderTargetData(target.p,staging.p);if(FAILED(hr))return Status(hr);
        D3DLOCKED_RECT lock{};hr=staging.p->LockRect(&lock,nullptr,D3DLOCK_READONLY);if(FAILED(hr))return Status(hr);
        if(lock.Pitch<0 || unsigned(lock.Pitch)<width*4 || !lock.pBits){staging.p->UnlockRect();return Invalid;}
        for(unsigned row=0;row<height;++row)std::memcpy(out.data()+std::size_t(row)*width*4,static_cast<const std::uint8_t*>(lock.pBits)+std::size_t(row)*lock.Pitch,width*4);
        hr=staging.p->UnlockRect();if(SUCCEEDED(hr))bytes=std::move(out);return Status(hr);
    }
};
class NativeFactory final:public Factory {
    Com<IDirect3D9> factory;DWORD thread=GetCurrentThreadId();
public:
    NativeFactory(){factory.p=Direct3DCreate9(D3D_SDK_VERSION);}
    Status capabilities(Capabilities& caps) override{
        if(GetCurrentThreadId()!=thread)return Invalid;if(!factory.p)return Unavailable;
        D3DCAPS9 value{};auto hr=factory.p->GetDeviceCaps(0,D3DDEVTYPE_HAL,&value);if(FAILED(hr))return Status(hr);
        D3DDISPLAYMODE mode{};hr=factory.p->GetAdapterDisplayMode(0,&mode);if(FAILED(hr))return Status(hr);
        hr=factory.p->CheckDeviceFormat(0,D3DDEVTYPE_HAL,mode.Format,D3DUSAGE_RENDERTARGET,D3DRTYPE_SURFACE,D3DFMT_X8R8G8B8);
        if(FAILED(hr))return Status(hr);caps=bounded(value);return Ok;
    }
    Status create(std::uintptr_t window,std::uint32_t width,std::uint32_t height,std::unique_ptr<Device>& out) override{
        if(GetCurrentThreadId()!=thread)return Invalid;if(!factory.p)return Unavailable;
        auto pending=std::make_unique<NativeDevice>();const auto hr=pending->initialize(factory.p,reinterpret_cast<HWND>(window),width,height);
        if(!failed(hr))out=std::move(pending);return hr;
    }
};
}
std::unique_ptr<Factory> make_factory(){return std::make_unique<NativeFactory>();}
#endif
}
