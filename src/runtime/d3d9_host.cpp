#include "winrecomp/d3d9_host.hpp"
#include <cmath>
#include <algorithm>
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
Status texture_limits(IDirect3D9* api,const D3DCAPS9& c,TextureLimits& out){
    D3DDISPLAYMODE mode{};auto hr=api->GetAdapterDisplayMode(0,&mode);if(FAILED(hr))return Status(hr);
    hr=api->CheckDeviceFormat(0,D3DDEVTYPE_HAL,mode.Format,0,D3DRTYPE_TEXTURE,D3DFMT_X8R8G8B8);
    if(FAILED(hr))return Status(hr);
    constexpr DWORD ops=D3DTEXOPCAPS_DISABLE|D3DTEXOPCAPS_SELECTARG1|D3DTEXOPCAPS_SELECTARG2|D3DTEXOPCAPS_MODULATE;
    constexpr DWORD filters=D3DPTFILTERCAPS_MINFPOINT|D3DPTFILTERCAPS_MAGFPOINT|D3DPTFILTERCAPS_MINFLINEAR|D3DPTFILTERCAPS_MAGFLINEAR;
    constexpr DWORD address=D3DPTADDRESSCAPS_WRAP|D3DPTADDRESSCAPS_CLAMP;
    if(!c.MaxTextureWidth || !c.MaxTextureHeight || !c.MaxSimultaneousTextures || !c.MaxTextureBlendStages ||
       (c.TextureOpCaps&ops)!=ops || (c.TextureFilterCaps&filters)!=filters ||
       (c.TextureAddressCaps&address)!=address || !(c.TextureCaps&D3DPTEXTURECAPS_PERSPECTIVE))return Unavailable;
    out.width=std::min(c.MaxTextureWidth,DWORD(MaxSize));out.height=std::min(c.MaxTextureHeight,DWORD(MaxSize));
    out.aspect=c.MaxTextureAspectRatio;out.ops=ops;out.filters=filters;out.address=address;
    out.caps=D3DPTEXTURECAPS_POW2|D3DPTEXTURECAPS_PERSPECTIVE|(c.TextureCaps&D3DPTEXTURECAPS_SQUAREONLY);
    out.alpha=SUCCEEDED(api->CheckDeviceFormat(0,D3DDEVTYPE_HAL,mode.Format,0,D3DRTYPE_TEXTURE,D3DFMT_A8R8G8B8));
    if(out.alpha)out.caps|=D3DPTEXTURECAPS_ALPHA;
    return Ok;
}
bool texture_size(const TextureLimits& c,unsigned w,unsigned h,bool alpha){
    return w && h && w<=c.width && h<=c.height && !(w&(w-1)) && !(h&(h-1)) &&
        (!c.aspect || (std::uint64_t(w)<=std::uint64_t(h)*c.aspect && std::uint64_t(h)<=std::uint64_t(w)*c.aspect)) &&
        (!(c.caps&D3DPTEXTURECAPS_SQUAREONLY) || w==h) && (!alpha || c.alpha);
}
class NativeDevice final:public Device {
    Com<IDirect3DDevice9> device;Com<IDirect3DSurface9> target,staging;
    Com<IDirect3DTexture9> texture_image;
    DWORD thread=GetCurrentThreadId();unsigned width{},height{};bool scene{};
    Capabilities caps{};
    bool valid() const{return device.p && GetCurrentThreadId()==thread;}
public:
    Status initialize(IDirect3D9* factory,HWND window,unsigned w,unsigned h){
        if(!window || !IsWindow(window) || GetWindowThreadProcessId(window,nullptr)!=thread ||
           !w || !h || w>MaxSize || h>MaxSize)return Invalid;
        D3DCAPS9 c{};auto hr=factory->GetDeviceCaps(0,D3DDEVTYPE_HAL,&c);if(FAILED(hr))return Status(hr);
        caps=bounded(c);auto texture_hr=texture_limits(factory,c,caps.texture);if(failed(texture_hr))return texture_hr;
        if(!caps.max_primitives || !(caps.misc&D3DPMISCCAPS_CULLNONE))return Unavailable;
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
        for(auto [state,value]:{std::pair{D3DTSS_COLOROP,DWORD(D3DTOP_MODULATE)},
            {D3DTSS_COLORARG1,DWORD(D3DTA_TEXTURE)},{D3DTSS_COLORARG2,DWORD(D3DTA_CURRENT)},
            {D3DTSS_ALPHAOP,DWORD(D3DTOP_SELECTARG1)},{D3DTSS_ALPHAARG1,DWORD(D3DTA_TEXTURE)},
            {D3DTSS_ALPHAARG2,DWORD(D3DTA_CURRENT)}}){
            hr=device.p->SetTextureStageState(0,state,value);if(FAILED(hr))return Status(hr);
        }
        hr=device.p->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);if(FAILED(hr))return Status(hr);
        for(auto [key,value]:{std::pair{D3DSAMP_ADDRESSU,DWORD(D3DTADDRESS_WRAP)},
            {D3DSAMP_ADDRESSV,DWORD(D3DTADDRESS_WRAP)},{D3DSAMP_MINFILTER,DWORD(D3DTEXF_POINT)},
            {D3DSAMP_MAGFILTER,DWORD(D3DTEXF_POINT)},{D3DSAMP_MIPFILTER,DWORD(D3DTEXF_NONE)}}){
            hr=device.p->SetSamplerState(0,key,value);if(FAILED(hr))return Status(hr);
        }
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
        auto hr=device.p->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE);if(FAILED(hr))return Status(hr);
        return Status(device.p->DrawPrimitiveUP(D3DPT_TRIANGLELIST,UINT(vertices.size()/3),vertices.data(),sizeof(Vertex)));
    }
    Status texture(std::uint32_t w,std::uint32_t h,bool alpha,std::span<const std::uint8_t> bytes)override{
        if(!valid() || !texture_size(caps.texture,w,h,alpha) || bytes.size()!=std::size_t(w)*h*4)return Invalid;
        // Transactional replacement: a failed upload must leave the old binding intact.
        Com<IDirect3DTexture9> pending;
        auto hr=device.p->CreateTexture(w,h,1,0,alpha?D3DFMT_A8R8G8B8:D3DFMT_X8R8G8B8,D3DPOOL_MANAGED,&pending.p,nullptr);
        if(FAILED(hr))return Status(hr);D3DLOCKED_RECT lock{};
        hr=pending.p->LockRect(0,&lock,nullptr,0);if(FAILED(hr))return Status(hr);
        if(!lock.pBits || lock.Pitch<0 || unsigned(lock.Pitch)<w*4){pending.p->UnlockRect(0);return Invalid;}
        for(unsigned y=0;y<h;++y)std::memcpy(static_cast<std::uint8_t*>(lock.pBits)+std::size_t(y)*lock.Pitch,bytes.data()+std::size_t(y)*w*4,w*4);
        hr=pending.p->UnlockRect(0);if(FAILED(hr))return Status(hr);
        hr=device.p->SetTexture(0,pending.p);if(SUCCEEDED(hr))std::swap(texture_image.p,pending.p);
        return Status(hr);
    }
    Status unbind_texture()override{
        if(!valid())return Invalid;auto hr=device.p->SetTexture(0,nullptr);
        if(SUCCEEDED(hr)){auto old=std::exchange(texture_image.p,nullptr);if(old)old->Release();}return Status(hr);
    }
    Status set_stage(std::uint32_t key,std::uint32_t value)override{
        if(!valid())return Invalid;
        // DX7 moved sampler controls out of texture stages in DX9. MIPFILTER
        // NONE changed from 1 to 0; it must not be forwarded numerically.
        switch(key){
        case 12:{ // D3DTSS_ADDRESS sets U and V together.
            if(value!=D3DTADDRESS_WRAP && value!=D3DTADDRESS_CLAMP)return Status(E_NOTIMPL);
            DWORD old{};auto hr=device.p->GetSamplerState(0,D3DSAMP_ADDRESSU,&old);if(FAILED(hr))return Status(hr);
            hr=device.p->SetSamplerState(0,D3DSAMP_ADDRESSU,value);if(FAILED(hr))return Status(hr);
            hr=device.p->SetSamplerState(0,D3DSAMP_ADDRESSV,value);
            if(FAILED(hr))device.p->SetSamplerState(0,D3DSAMP_ADDRESSU,old);return Status(hr);
        }
        case 13:case 14:
            if(value!=D3DTADDRESS_WRAP && value!=D3DTADDRESS_CLAMP)return Status(E_NOTIMPL);
            return Status(device.p->SetSamplerState(0,key==13?D3DSAMP_ADDRESSU:D3DSAMP_ADDRESSV,value));
        case 16:case 17:
            if(value!=1 && value!=2)return Status(E_NOTIMPL);
            return Status(device.p->SetSamplerState(0,key==16?D3DSAMP_MAGFILTER:D3DSAMP_MINFILTER,value));
        case 18:if(value!=1)return Status(E_NOTIMPL);return Status(device.p->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE));
        case 1:case 4:if(value<1 || value>4)return Status(E_NOTIMPL);break;
        case 2:case 3:case 5:case 6:if(value>D3DTA_TEXTURE)return Status(E_NOTIMPL);break;
        case 11:case 24:if(value)return Status(E_NOTIMPL);break;
        default:return Status(E_NOTIMPL);
        }
        return Status(device.p->SetTextureStageState(0,D3DTEXTURESTAGESTATETYPE(key),value));
    }
    Status get_stage(std::uint32_t key,std::uint32_t& value)override{
        if(!valid())return Invalid;DWORD out{};HRESULT hr{};
        switch(key){
        case 12:case 13:case 14:case 16:case 17:case 18:{
            auto state=key==14?D3DSAMP_ADDRESSV:key==16?D3DSAMP_MAGFILTER:key==17?D3DSAMP_MINFILTER:key==18?D3DSAMP_MIPFILTER:D3DSAMP_ADDRESSU;
            hr=device.p->GetSamplerState(0,state,&out);if(SUCCEEDED(hr) && key==18)out=1;break;
        }
        case 1:case 2:case 3:case 4:case 5:case 6:case 11:case 24:
            hr=device.p->GetTextureStageState(0,D3DTEXTURESTAGESTATETYPE(key),&out);break;
        default:return Status(E_NOTIMPL);
        }
        if(SUCCEEDED(hr))value=out;return Status(hr);
    }
    Status textured_triangles(std::span<const TexturedVertex> vertices)override{
        if(!valid() || !scene || !texture_image.p || vertices.empty() || vertices.size()%3 || vertices.size()/3>caps.max_primitives)return Invalid;
        for(const auto& v:vertices)if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z)||!std::isfinite(v.rhw)||v.rhw<=0||!std::isfinite(v.u)||!std::isfinite(v.v))return Invalid;
        auto hr=device.p->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1);if(FAILED(hr))return Status(hr);
        return Status(device.p->DrawPrimitiveUP(D3DPT_TRIANGLELIST,UINT(vertices.size()/3),vertices.data(),sizeof(TexturedVertex)));
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
        if(FAILED(hr))return Status(hr);caps=bounded(value);return texture_limits(factory.p,value,caps.texture);
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
