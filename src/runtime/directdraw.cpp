#include "winrecomp/directdraw.hpp"
#include "winrecomp/process.hpp"
#include "winrecomp/d3d9_host.hpp"
#include "winrecomp/indexed_geometry.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <exception>
#include <map>
#include <sstream>
#include <iomanip>
#include <thread>
#include <vector>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#define DIRECTDRAW_VERSION 0x0700
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <cstddef>
#include <type_traits>
#endif
namespace wr {
#ifndef _WIN32
namespace {class UnavailableDraw final:public DirectDrawBackend {
public:void shutdown() noexcept override{}std::string report() const override{return "{\"backend\":\"unavailable\"}";}
};}
std::unique_ptr<DirectDrawBackend> install_directdraw(Process& p){if(p.options.legacy_d3d9)throw std::runtime_error("D3D9 renderer requires Windows");return std::make_unique<UnavailableDraw>();}
#else
namespace {
using Args=std::span<const U32>;
using Function=std::function<U32(Args)>;
enum class Interface {unknown,draw1,draw7,surface7,d3d7,clipper,device7,compat3d7,compatdevice7,depth7};
constexpr std::size_t DescSize=124,MaxSurfaceBytes=16u*1024*1024;
constexpr U32 MaxObjects=256;
struct Allocation {
    Process& p;U32 address{};
    Allocation(Process& q,std::size_t n):p(q),address(q.allocate_bytes(n)){}
    ~Allocation(){if(address)try{p.memory.release(address);}catch(...){}}
    U32 keep(){return std::exchange(address,0u);}
};
struct NativeOwner {
    IUnknown* value{};
    ~NativeOwner(){if(value)value->Release();}
    IUnknown* keep(){return std::exchange(value,nullptr);}
};
struct SurfaceLock {
    DDSURFACEDESC2 description{};U32 guest{};std::size_t size{},row_bytes{};bool readonly{};
};
// Guest texture identity is independent of the host DirectDraw storage caps.
// Windows x64 may reject native DDSCAPS_TEXTURE even for system memory; the
// actual texture lives in D3D9 and a native plain surface provides CPU storage.
struct TextureModel { DDSCAPS2 caps{}; };
struct CompatDevice;
struct SurfaceModel;
struct DepthModel {
    std::shared_ptr<IDirectDraw7> root;
    DDSURFACEDESC2 description{};
    std::shared_ptr<host9::Depth> storage;
    std::weak_ptr<SurfaceModel> attached_to;
};
struct SurfaceModel {
    std::shared_ptr<DepthModel> depth;
    std::weak_ptr<CompatDevice> device;
    bool constructing{};
    bool primary{};
};
struct CompatDevice {
    std::unique_ptr<host9::Device> gpu;
    IDirectDraw7* root{};IDirectDrawSurface7* target{};
    std::shared_ptr<IDirectDrawSurface7> texture;
    std::shared_ptr<TextureModel> texture_model;
    std::shared_ptr<SurfaceModel> surface_model;
    U32 width{},height{};
    ~CompatDevice(){gpu.reset();texture.reset();if(target)target->Release();if(root)root->Release();}
};
struct Object {
    Interface kind{};IUnknown* native{};U32 guest_refs{};
    std::unique_ptr<SurfaceLock> locked;
    std::shared_ptr<CompatDevice> compat;
    std::shared_ptr<TextureModel> texture_model;
    std::shared_ptr<SurfaceModel> surface_model;
    std::shared_ptr<DepthModel> depth_model;
};
class Draw final:public DirectDrawBackend {
    Process& p;
    DWORD thread{GetCurrentThreadId()};bool stopped{};
    std::map<U32,Object> objects;
    std::map<Interface,U32> vtables;
    std::unique_ptr<host9::Factory> renderer9;
    std::map<std::uintptr_t,HWND> cooperative_windows;
    // Retain controlling identity references while using their addresses as keys.
    std::map<std::uintptr_t,std::shared_ptr<IUnknown>> root_identities;
    std::uint64_t renderer9_devices{},renderer9_draws{},renderer9_indexed_draws{},renderer9_clears{},renderer9_readbacks{},renderer9_presents{};
    std::uint64_t renderer9_depth_surfaces{},renderer9_depth_binds{};
    std::map<std::uintptr_t,U32> monitors;
    // D3D enumeration names remain usable after EnumDevices returns. These
    // process-owned read-only strings are not temporary callback payloads.
    std::map<std::string,U32> device_strings;
    std::map<std::uintptr_t,std::pair<HWND,U32>> clipper_windows;
    std::uint64_t d3d_wrappers{},device_callbacks{},zformat_callbacks{},mode7_callbacks{},render_devices{},texture_callbacks{};
    std::uint64_t created{},retired{},enumerated{},mode_callbacks{},locks{},unlocks{},blits{};
    [[noreturn]]void stop(const std::string& why){throw GuestFault(FaultKind::unsupported,p.cpu.eip,"DirectDraw: "+why);}
    void enter(){if(stopped || GetCurrentThreadId()!=thread)stop("foreign-thread or retired backend");}
    GUID guid(U32 address){GUID value{};static_assert(sizeof(value)==16);p.memory.copy_out(address,std::span(reinterpret_cast<std::uint8_t*>(&value),16));return value;}
    Object& object(U32 address,Interface kind){auto it=objects.find(address);if(it==objects.end() || (!it->second.native && !it->second.compat && !it->second.depth_model) || !it->second.guest_refs || it->second.kind!=kind)stop("stale or wrong-interface guest COM object");return it->second;}
    void api(const char* name,unsigned count,Function function){
        p.register_api("ddraw.dll",name,count,[this,count,function=std::move(function)](Process& q){enter();std::array<U32,8> args{};for(unsigned n=0;n<count;++n)args[n]=q.argument(n);return function(std::span(args).first(count));});
    }
    U32 wrap(IUnknown* native,Interface kind,std::shared_ptr<TextureModel> texture_model={},std::shared_ptr<SurfaceModel> surface_model={}){
        NativeOwner owner{native};
        for(auto& [address,value]:objects)if(value.native==native && value.kind==kind){if(value.guest_refs==0xffffffffu)stop("reference count overflow");++value.guest_refs;owner.keep();return address;}
        if(objects.size()>=MaxObjects)stop("COM object budget exhausted");
        const auto table=vtable(kind);Allocation memory(p,4);p.memory.store(memory.address,table,32);p.memory.protect(memory.address,4096,Memory::Read);
        const auto at=memory.address;objects.emplace(at,Object{kind,native,1,{},{},std::move(texture_model),std::move(surface_model),{}});memory.keep();owner.keep();++created;if(kind==Interface::d3d7)++d3d_wrappers;return at;
    }
    U32 query(Object& value,Args args){
        const auto iid=guid(args[1]);p.memory.check(args[2],4,Memory::Write);
        if(value.kind==Interface::depth7){
            if(IsEqualIID(iid,IID_IUnknown) || IsEqualIID(iid,IID_IDirectDrawSurface7)){
                if(value.guest_refs==0xffffffffu)stop("reference count overflow");
                ++value.guest_refs;p.memory.store(args[2],args[0],32);return U32(S_OK);
            }
            p.memory.store(args[2],0,32);return U32(E_NOINTERFACE);
        }
        if(value.kind==Interface::compatdevice7){
            if(IsEqualIID(iid,IID_IUnknown) || IsEqualIID(iid,IID_IDirect3DDevice7)){
                if(value.guest_refs==0xffffffffu)stop("reference count overflow");
                ++value.guest_refs;p.memory.store(args[2],args[0],32);return U32(S_OK);
            }
            p.memory.store(args[2],0,32);return U32(E_NOINTERFACE);
        }
        if(p.options.legacy_d3d9 && IsEqualIID(iid,IID_IDirect3D7) &&
           (value.kind==Interface::draw1 || value.kind==Interface::draw7 || value.kind==Interface::unknown || value.kind==Interface::compat3d7)){
            host9::Capabilities caps{};const auto available=factory9().capabilities(caps);
            if(host9::failed(available)){p.memory.store(args[2],0,32);return available;}
            IDirectDraw7* root{};auto hr=value.native->QueryInterface(IID_IDirectDraw7,reinterpret_cast<void**>(&root));
            if(FAILED(hr)){p.memory.store(args[2],0,32);return U32(hr);}
            // The adapter's controlling IUnknown remains the actual Draw root.
            p.memory.store(args[2],wrap(root,Interface::compat3d7),32);return U32(S_OK);
        }
        Interface kind=Interface::unknown;bool supported=true;
        if(IsEqualIID(iid,IID_IUnknown))kind=Interface::unknown;
        else if(IsEqualIID(iid,IID_IDirectDraw))kind=Interface::draw1;
        else if(IsEqualIID(iid,IID_IDirectDraw7))kind=Interface::draw7;
        else if(IsEqualIID(iid,IID_IDirectDrawSurface7))kind=Interface::surface7;
        else if(IsEqualIID(iid,IID_IDirect3D7))kind=Interface::d3d7;
        else if(IsEqualIID(iid,IID_IDirectDrawClipper))kind=Interface::clipper;
        else if(IsEqualIID(iid,IID_IDirect3DDevice7))kind=Interface::device7;
        else supported=false;
        void* result=nullptr;const auto hr=value.native->QueryInterface(iid,&result);
        if(FAILED(hr)){p.memory.store(args[2],0,32);return U32(hr);}
        if(!result)stop("native QueryInterface returned success without an object");
        if(!supported){
            static_cast<IUnknown*>(result)->Release();std::ostringstream id;
            id<<std::hex<<std::setfill('0')<<std::setw(8)<<iid.Data1<<'-'<<std::setw(4)<<iid.Data2<<'-'<<std::setw(4)<<iid.Data3<<'-';
            for(unsigned n=0;n<8;++n){if(n==2)id<<'-';id<<std::setw(2)<<unsigned(iid.Data4[n]);}
            stop("native QueryInterface supports IID "+id.str()+" without a guest wrapper");
        }
        p.memory.store(args[2],wrap(static_cast<IUnknown*>(result),kind,value.texture_model,value.surface_model),32);return U32(hr);
    }
    void destroy_lock(Object& value) noexcept {
        if(!value.locked)return;
        static_cast<IDirectDrawSurface7*>(value.native)->Unlock(nullptr);
        try{p.memory.release(value.locked->guest);}catch(...){}
        value.locked.reset();
    }
    U32 release(Object& value){
        if(value.depth_model){const auto left=--value.guest_refs;if(!left){value.depth_model.reset();++retired;}return left;}
        if(value.kind==Interface::compatdevice7){if(value.compat->gpu->in_scene())stop("release during active D3D9 scene");const auto left=--value.guest_refs;if(!left){value.compat.reset();++retired;}return left;}
        if(value.locked)stop("release of a locked surface");
        const auto refs=value.native->Release();if(!--value.guest_refs){value.native=nullptr;value.texture_model.reset();value.surface_model.reset();++retired;}return refs;
    }
    U32 vtable(Interface);
    struct Enumeration {Draw* self;U32 callback,context;std::exception_ptr failure;unsigned visits{};};
    static BOOL CALLBACK enumerate(GUID* id,LPSTR description,LPSTR name,LPVOID context,HMONITOR monitor) noexcept {
        auto& call=*static_cast<Enumeration*>(context);auto& self=*call.self;
        try {
            self.enter();if(call.failure)return FALSE;
            if(++call.visits>256)self.stop("adapter enumeration budget");
            Allocation scratch(self.p,16384);U32 used=0;
            auto bytes=[&](const void* data,std::size_t n){if(n>16384-used)self.stop("adapter callback payload too large");auto at=scratch.address+used;used+=(U32(n)+3)&~3u;self.p.memory.copy_in(at,std::span(static_cast<const std::uint8_t*>(data),n));return at;};
            auto string=[&](const char* text)->U32{if(!text)return 0;std::size_t n=0;while(n<4096 && text[n])++n;if(n==4096)self.stop("unterminated native adapter name");return bytes(text,n+1);};
            U32 mon=0;if(monitor){const auto key=reinterpret_cast<std::uintptr_t>(monitor);auto it=self.monitors.find(key);if(it==self.monitors.end()){if(self.monitors.size()>=256)self.stop("monitor token budget");mon=0xba000000u+U32(self.monitors.size())*16;self.monitors.emplace(key,mon);}else mon=it->second;}
            const std::array<U32,5> args{id?bytes(id,16):0,string(description),string(name),call.context,mon};
            ++self.enumerated;const auto result=self.p.callback(call.callback,args);return self.p.exited()?FALSE:result!=0;
        }catch(...){call.failure=std::current_exception();return FALSE;}
    }
    // IDirectDraw (v1) uses DDSURFACEDESC, not DDSURFACEDESC2. The native
    // 64-bit pointer shifts following fields; never memcpy the native layout.
    static constexpr U32 LegacyDescSize=108;
    DDSURFACEDESC mode_filter(U32 at){
        p.memory.check(at,LegacyDescSize,Memory::Read);
        if(p.memory.load(at,32)!=LegacyDescSize)stop("mode filter requires the 108-byte x86 DDSURFACEDESC");
        DDSURFACEDESC d{};d.dwSize=sizeof(d);d.dwFlags=p.memory.load(at+4,32);
        constexpr U32 allowed=DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT|DDSD_REFRESHRATE;
        if(d.dwFlags&~allowed)stop("unmodelled legacy display-mode filter fields");
        if(p.memory.load(at+36,32))stop("display-mode filter must not contain a surface pointer");
        if(d.dwFlags&DDSD_WIDTH)d.dwWidth=p.memory.load(at+12,32);
        if(d.dwFlags&DDSD_HEIGHT)d.dwHeight=p.memory.load(at+8,32);
        if(d.dwFlags&DDSD_REFRESHRATE)d.dwRefreshRate=p.memory.load(at+24,32);
        if(d.dwFlags&DDSD_PIXELFORMAT){
            p.memory.copy_out(at+72,std::span(reinterpret_cast<std::uint8_t*>(&d.ddpfPixelFormat),32));
            if(d.ddpfPixelFormat.dwSize!=32)stop("mode filter pixel format must have size 32");
        }
        return d;
    }
    void write_mode(U32 at,const DDSURFACEDESC& d){
        if(d.dwSize!=sizeof(d) || d.lpSurface || (d.dwFlags&DDSD_LPSURFACE))
            stop("unhandled native display-mode descriptor or pixel pointer");
        static_assert(sizeof(DDPIXELFORMAT)==32 && sizeof(DDCOLORKEY)==8 && sizeof(DDSCAPS)==4);
        std::array<U32,27> out{};out[0]=LegacyDescSize;out[1]=d.dwFlags;
        // Only fields marked valid may be consumed. Ignore undefined native
        // padding, pointer storage and reserved values rather than exposing them.
        if(d.dwFlags&DDSD_HEIGHT)out[2]=d.dwHeight;
        if(d.dwFlags&DDSD_WIDTH)out[3]=d.dwWidth;
        if(d.dwFlags&(DDSD_PITCH|DDSD_LINEARSIZE))out[4]=U32(d.lPitch);
        if(d.dwFlags&DDSD_BACKBUFFERCOUNT)out[5]=d.dwBackBufferCount;
        if(d.dwFlags&(DDSD_MIPMAPCOUNT|DDSD_ZBUFFERBITDEPTH|DDSD_REFRESHRATE))out[6]=d.dwRefreshRate;
        if(d.dwFlags&DDSD_ALPHABITDEPTH)out[7]=d.dwAlphaBitDepth;
        if(d.dwFlags&DDSD_CKDESTOVERLAY)std::memcpy(out.data()+10,&d.ddckCKDestOverlay,8);
        if(d.dwFlags&DDSD_CKDESTBLT)std::memcpy(out.data()+12,&d.ddckCKDestBlt,8);
        if(d.dwFlags&DDSD_CKSRCOVERLAY)std::memcpy(out.data()+14,&d.ddckCKSrcOverlay,8);
        if(d.dwFlags&DDSD_CKSRCBLT)std::memcpy(out.data()+16,&d.ddckCKSrcBlt,8);
        if(d.dwFlags&DDSD_PIXELFORMAT)std::memcpy(out.data()+18,&d.ddpfPixelFormat,32);
        if(d.dwFlags&DDSD_CAPS)out[26]=d.ddsCaps.dwCaps;
        for(unsigned n=0;n<out.size();++n)p.memory.store(at+4*n,out[n],32);
    }
    struct ModeEnumeration {Draw* self;U32 callback,context;std::exception_ptr failure;unsigned visits{};};
    static HRESULT CALLBACK enumerate_mode(DDSURFACEDESC* description,void* context) noexcept {
        auto& call=*static_cast<ModeEnumeration*>(context);auto& self=*call.self;
        try {
            self.enter();if(call.failure || self.p.exited())return DDENUMRET_CANCEL;
            if(!description)self.stop("null display-mode callback descriptor");
            if(++call.visits>4096)self.stop("display-mode callback budget exceeded");
            Allocation scratch(self.p,LegacyDescSize);self.write_mode(scratch.address,*description);
            self.p.memory.protect(scratch.address,4096,Memory::Read);
            const std::array<U32,2> args{scratch.address,call.context};++self.mode_callbacks;
            const auto result=self.p.callback(call.callback,args);
            if(self.p.exited())return DDENUMRET_CANCEL;
            if(result!=DDENUMRET_OK && result!=DDENUMRET_CANCEL)self.stop("invalid display-mode callback result");
            return HRESULT(result);
        }catch(...){if(!call.failure)call.failure=std::current_exception();return DDENUMRET_CANCEL;}
    }
    U32 enum_modes(Args a){
        auto native=static_cast<IDirectDraw*>(object(a[0],Interface::draw1).native);
        p.memory.check(a[4],1,Memory::Execute);
        DDSURFACEDESC filter{};if(a[2])filter=mode_filter(a[2]);
        // Guest callbacks may release their original reference. Keep the native
        // enumeration receiver alive until the DLL call has unwound normally.
        native->AddRef();NativeOwner lifetime{native};
        ModeEnumeration call{this,a[4],a[3],{},0};
        const auto hr=native->EnumDisplayModes(a[1],a[2]?&filter:nullptr,&call,enumerate_mode);
        if(call.failure)std::rethrow_exception(call.failure);
        return U32(hr);
    }
    DDSURFACEDESC2 mode_filter7(U32 at){
        p.memory.check(at,124,Memory::Read);
        if(p.memory.load(at,32)!=124)stop("mode filter requires the 124-byte x86 DDSURFACEDESC2");
        DDSURFACEDESC2 d{};d.dwSize=sizeof(d);d.dwFlags=p.memory.load(at+4,32);
        constexpr U32 allowed=DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT|DDSD_REFRESHRATE;
        if(d.dwFlags&~allowed)stop("unmodelled DD7 display-mode filter fields");
        if(p.memory.load(at+36,32))stop("DD7 display-mode filter contains a surface pointer");
        if(d.dwFlags&DDSD_WIDTH)d.dwWidth=p.memory.load(at+12,32);
        if(d.dwFlags&DDSD_HEIGHT)d.dwHeight=p.memory.load(at+8,32);
        if(d.dwFlags&DDSD_REFRESHRATE)d.dwRefreshRate=p.memory.load(at+24,32);
        if(d.dwFlags&DDSD_PIXELFORMAT){
            p.memory.copy_out(at+72,std::span(reinterpret_cast<std::uint8_t*>(&d.ddpfPixelFormat),32));
            if(d.ddpfPixelFormat.dwSize!=32)stop("DD7 mode filter pixel-format size");
        }
        return d;
    }
    void write_mode7(U32 at,const DDSURFACEDESC2& d){
        constexpr DWORD allowed=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT|DDSD_REFRESHRATE|DDSD_PITCH|DDSD_LINEARSIZE|DDSD_BACKBUFFERCOUNT|DDSD_ALPHABITDEPTH;
        if(d.dwSize!=sizeof(d) || d.lpSurface || (d.dwFlags&~allowed))stop("unhandled DD7 native display-mode descriptor");
        if((d.dwFlags&DDSD_PIXELFORMAT) && d.ddpfPixelFormat.dwSize!=32)stop("DD7 native mode pixel-format size");
        std::array<U32,31> out{};out[0]=124;out[1]=d.dwFlags;
        if(d.dwFlags&DDSD_HEIGHT)out[2]=d.dwHeight;
        if(d.dwFlags&DDSD_WIDTH)out[3]=d.dwWidth;
        if(d.dwFlags&(DDSD_PITCH|DDSD_LINEARSIZE))out[4]=U32(d.lPitch);
        if(d.dwFlags&DDSD_BACKBUFFERCOUNT)out[5]=d.dwBackBufferCount;
        if(d.dwFlags&DDSD_REFRESHRATE)out[6]=d.dwRefreshRate;
        if(d.dwFlags&DDSD_ALPHABITDEPTH)out[7]=d.dwAlphaBitDepth;
        if(d.dwFlags&DDSD_PIXELFORMAT)std::memcpy(out.data()+18,&d.ddpfPixelFormat,32);
        if(d.dwFlags&DDSD_CAPS)std::memcpy(out.data()+26,&d.ddsCaps,16);
        for(unsigned n=0;n<out.size();++n)p.memory.store(at+4*n,out[n],32);
    }
    static HRESULT CALLBACK enumerate_mode7(DDSURFACEDESC2* description,void* context) noexcept {
        auto& call=*static_cast<ModeEnumeration*>(context);auto& self=*call.self;
        try {
            self.enter();if(call.failure || self.p.exited())return DDENUMRET_CANCEL;
            if(!description)self.stop("null DD7 display-mode descriptor");
            if(++call.visits>4096)self.stop("DD7 display-mode callback budget exceeded");
            Allocation scratch(self.p,124);self.write_mode7(scratch.address,*description);
            self.p.memory.protect(scratch.address,4096,Memory::Read);
            const std::array<U32,2> args{scratch.address,call.context};++self.mode7_callbacks;
            const auto result=self.p.callback(call.callback,args);
            if(self.p.exited())return DDENUMRET_CANCEL;
            if(result!=DDENUMRET_OK && result!=DDENUMRET_CANCEL)self.stop("invalid DD7 display-mode callback result");
            return HRESULT(result);
        }catch(...){if(!call.failure)call.failure=std::current_exception();return DDENUMRET_CANCEL;}
    }
    U32 enum_modes7(Args a){
        auto native=static_cast<IDirectDraw7*>(object(a[0],Interface::draw7).native);
        p.memory.check(a[4],1,Memory::Execute);
        DDSURFACEDESC2 filter{};if(a[2])filter=mode_filter7(a[2]);
        native->AddRef();NativeOwner lifetime{native};
        ModeEnumeration call{this,a[4],a[3],{},0};
        const auto hr=native->EnumDisplayModes(a[1],a[2]?&filter:nullptr,&call,enumerate_mode7);
        if(call.failure)std::rethrow_exception(call.failure);
        return U32(hr);
    }
    // All public D3DDEVICEDESC7 fields are fixed-width values, not pointers.
    // The first 220 bytes have the same DX7 x86/x64 layout. Reserved fields
    // stay zero; no host stack padding or pointers are exposed to the guest.
    static constexpr U32 DeviceDescSize=236;
    void write_device(U32 at,const D3DDEVICEDESC7& d){
        static_assert(std::is_trivially_copyable_v<D3DDEVICEDESC7>);
        static_assert(sizeof(D3DDEVICEDESC7)==DeviceDescSize && sizeof(D3DPRIMCAPS)==56);
        static_assert(offsetof(D3DDEVICEDESC7,dpcLineCaps)==4);
        static_assert(offsetof(D3DDEVICEDESC7,dpcTriCaps)==60);
        static_assert(offsetof(D3DDEVICEDESC7,dwDeviceRenderBitDepth)==116);
        static_assert(offsetof(D3DDEVICEDESC7,dwMinTextureWidth)==124);
        static_assert(offsetof(D3DDEVICEDESC7,dvGuardBandLeft)==152);
        static_assert(offsetof(D3DDEVICEDESC7,wMaxTextureBlendStages)==184);
        static_assert(offsetof(D3DDEVICEDESC7,dwMaxActiveLights)==188);
        static_assert(offsetof(D3DDEVICEDESC7,deviceGUID)==196);
        static_assert(offsetof(D3DDEVICEDESC7,dwVertexProcessingCaps)==216);
        static_assert(offsetof(D3DDEVICEDESC7,dwReserved1)==220);
        if(d.dpcLineCaps.dwSize!=56 || d.dpcTriCaps.dwSize!=56)
            stop("unhandled native D3DPRIMCAPS layout");
        std::array<std::uint8_t,DeviceDescSize> bytes{};
        std::memcpy(bytes.data(),&d,220);
        p.memory.copy_in(at,bytes);
    }
    U32 device_string(const char* text){
        if(!text)stop("null Direct3D enumeration string");
        std::size_t n=0;while(n<4096 && text[n])++n;
        if(n==4096)stop("unterminated Direct3D enumeration string");
        std::string key(text,n);
        if(auto it=device_strings.find(key);it!=device_strings.end())return it->second;
        if(device_strings.size()>=256)stop("Direct3D enumeration string budget");
        Allocation memory(p,n+1);
        p.memory.copy_in(memory.address,std::span(reinterpret_cast<const std::uint8_t*>(text),n+1));
        p.memory.protect(memory.address,4096,Memory::Read);
        auto at=memory.address;device_strings.emplace(std::move(key),at);memory.keep();return at;
    }
    struct DeviceEnumeration {Draw* self;U32 callback,context;std::exception_ptr failure;unsigned visits{};};
    static HRESULT CALLBACK enumerate_device(char* description,char* name,D3DDEVICEDESC7* desc,void* context) noexcept {
        auto& call=*static_cast<DeviceEnumeration*>(context);auto& self=*call.self;
        try {
            self.enter();if(call.failure || self.p.exited())return D3DENUMRET_CANCEL;
            if(!desc)self.stop("null Direct3D device description");
            if(++call.visits>256)self.stop("Direct3D device callback budget exceeded");
            Allocation scratch(self.p,DeviceDescSize);self.write_device(scratch.address,*desc);
            self.p.memory.protect(scratch.address,4096,Memory::Read);
            const std::array<U32,4> args{self.device_string(description),self.device_string(name),scratch.address,call.context};
            ++self.device_callbacks;const auto result=self.p.callback(call.callback,args);
            if(self.p.exited())return D3DENUMRET_CANCEL;
            if(result!=D3DENUMRET_OK && result!=D3DENUMRET_CANCEL)self.stop("invalid Direct3D device callback result");
            return HRESULT(result);
        }catch(...){if(!call.failure)call.failure=std::current_exception();return D3DENUMRET_CANCEL;}
    }
    U32 enum_devices(Args a){
        auto native=static_cast<IDirect3D7*>(object(a[0],Interface::d3d7).native);
        if(a[1])p.memory.check(a[1],1,Memory::Execute);
        // Preserve a native reference even when the guest callback releases its
        // interface, nests enumeration or queries the controlling IUnknown.
        native->AddRef();NativeOwner lifetime{native};
        DeviceEnumeration call{this,a[1],a[2],{},0};
        const auto hr=native->EnumDevices(a[1]?enumerate_device:nullptr,&call);
        if(call.failure)std::rethrow_exception(call.failure);
        return U32(hr);
    }
    struct ZFormatEnumeration {Draw* self;U32 callback,context;std::exception_ptr failure;unsigned visits{};};
    static HRESULT CALLBACK enumerate_zformat(DDPIXELFORMAT* desc,void* context) noexcept {
        auto& call=*static_cast<ZFormatEnumeration*>(context);auto& self=*call.self;
        try {
            self.enter();if(call.failure || self.p.exited())return D3DENUMRET_CANCEL;
            if(!desc || desc->dwSize!=32)self.stop("unhandled native depth pixel format");
            if(++call.visits>256)self.stop("Direct3D depth format callback budget exceeded");
            constexpr DWORD allowed=DDPF_ZBUFFER|DDPF_STENCILBUFFER;
            if(!(desc->dwFlags&DDPF_ZBUFFER) || (desc->dwFlags&~allowed))self.stop("unhandled depth pixel-format union");
            // Serialize only active union fields; FourCC and unused RGB members
            // are not meaningful for a depth/stencil enumeration.
            const std::array<U32,8> fields{32,desc->dwFlags,0,desc->dwZBufferBitDepth,
                (desc->dwFlags&DDPF_STENCILBUFFER)?desc->dwStencilBitDepth:0,
                desc->dwZBitMask,(desc->dwFlags&DDPF_STENCILBUFFER)?desc->dwStencilBitMask:0,0};
            Allocation scratch(self.p,32);
            for(unsigned n=0;n<fields.size();++n)self.p.memory.store(scratch.address+4*n,fields[n],32);
            self.p.memory.protect(scratch.address,4096,Memory::Read);
            const std::array<U32,2> args{scratch.address,call.context};++self.zformat_callbacks;
            const auto result=self.p.callback(call.callback,args);
            if(self.p.exited())return D3DENUMRET_CANCEL;
            if(result!=D3DENUMRET_OK && result!=D3DENUMRET_CANCEL)self.stop("invalid Direct3D depth callback result");
            return HRESULT(result);
        }catch(...){if(!call.failure)call.failure=std::current_exception();return D3DENUMRET_CANCEL;}
    }
    U32 enum_zformats(Args a){
        auto native=static_cast<IDirect3D7*>(object(a[0],Interface::d3d7).native);
        auto id=guid(a[1]);if(a[2])p.memory.check(a[2],1,Memory::Execute);
        native->AddRef();NativeOwner lifetime{native};
        ZFormatEnumeration call{this,a[2],a[3],{},0};
        const auto hr=native->EnumZBufferFormats(id,a[2]?enumerate_zformat:nullptr,&call);
        if(call.failure)std::rethrow_exception(call.failure);
        return U32(hr);
    }
    U32 create_device(Args a){
        auto native=static_cast<IDirect3D7*>(object(a[0],Interface::d3d7).native);
        const auto id=guid(a[1]);p.memory.check(a[3],4,Memory::Write);
        IDirectDrawSurface7* target=nullptr;
        if(a[2]){auto& value=object(a[2],Interface::surface7);if(value.locked)stop("CreateDevice with a locked render target");target=static_cast<IDirectDrawSurface7*>(value.native);}
        IDirect3DDevice7* result{};const auto hr=native->CreateDevice(id,target,&result);
        if(SUCCEEDED(hr)){if(!result)stop("native CreateDevice returned no device");p.memory.store(a[3],wrap(result,Interface::device7),32);++render_devices;}
        return U32(hr);
    }
    void write_texture_format(U32 at,const DDPIXELFORMAT& f){
        static_assert(sizeof(f)==32);
        constexpr DWORD palette=DDPF_PALETTEINDEXED1|DDPF_PALETTEINDEXED2|DDPF_PALETTEINDEXED4|DDPF_PALETTEINDEXED8|DDPF_PALETTEINDEXEDTO8;
        constexpr DWORD allowed=DDPF_RGB|DDPF_ALPHAPIXELS|DDPF_ALPHAPREMULT|DDPF_ALPHA|DDPF_LUMINANCE|DDPF_BUMPDUDV|DDPF_BUMPLUMINANCE|DDPF_FOURCC|DDPF_COMPRESSED|palette;
        if(f.dwSize!=32 || (f.dwFlags&~allowed))stop("unhandled native texture pixel-format flags");
        std::array<U32,8> bytes{32,f.dwFlags,0,0,0,0,0,0};
        if(f.dwFlags&DDPF_FOURCC)bytes[2]=f.dwFourCC;
        if(f.dwFlags&(DDPF_RGB|DDPF_ALPHA|DDPF_LUMINANCE|DDPF_BUMPDUDV|palette))bytes[3]=f.dwRGBBitCount;
        if(f.dwFlags&DDPF_RGB){bytes[4]=f.dwRBitMask;bytes[5]=f.dwGBitMask;bytes[6]=f.dwBBitMask;}
        if(f.dwFlags&DDPF_LUMINANCE)bytes[4]=f.dwLuminanceBitMask;
        if(f.dwFlags&DDPF_BUMPDUDV){bytes[4]=f.dwBumpDuBitMask;bytes[5]=f.dwBumpDvBitMask;if(f.dwFlags&DDPF_BUMPLUMINANCE)bytes[6]=f.dwBumpLuminanceBitMask;}
        if(f.dwFlags&DDPF_ALPHAPIXELS)bytes[7]=f.dwRGBAlphaBitMask;
        // Unused union members are not native pointers, but may be undefined.
        // They remain zero rather than exposing incidental host storage.
        for(unsigned i=0;i<bytes.size();++i)p.memory.store(at+4*i,bytes[i],32);
    }
    static HRESULT CALLBACK enumerate_texture(DDPIXELFORMAT* format,void* state) noexcept {
        auto& call=*static_cast<ModeEnumeration*>(state);auto& self=*call.self;
        try{
            self.enter();if(call.failure || self.p.exited())return D3DENUMRET_CANCEL;
            if(!format)self.stop("null native texture format");
            if(++call.visits>1024)self.stop("texture format callback budget");
            Allocation scratch(self.p,32);self.write_texture_format(scratch.address,*format);self.p.memory.protect(scratch.address,4096,Memory::Read);
            const std::array<U32,2> args{scratch.address,call.context};++self.texture_callbacks;
            const auto result=self.p.callback(call.callback,args);
            if(self.p.exited())return D3DENUMRET_CANCEL;
            if(result!=D3DENUMRET_CANCEL && result!=D3DENUMRET_OK)self.stop("invalid texture format callback result");
            return HRESULT(result);
        }catch(...){if(!call.failure)call.failure=std::current_exception();return D3DENUMRET_CANCEL;}
    }
    U32 enum_texture_formats(Args a){
        auto native=static_cast<IDirect3DDevice7*>(object(a[0],Interface::device7).native);
        if(!a[1])return U32(native->EnumTextureFormats(nullptr,nullptr));
        p.memory.check(a[1],1,Memory::Execute);native->AddRef();NativeOwner owner{native};
        ModeEnumeration call{this,a[1],a[2],{},0};const auto hr=native->EnumTextureFormats(enumerate_texture,&call);
        if(call.failure)std::rethrow_exception(call.failure);return U32(hr);
    }
    U32 draw_caps(Args a){
        auto native=static_cast<IDirectDraw7*>(object(a[0],Interface::draw7).native);
        static_assert(sizeof(DDCAPS)==380 && sizeof(DDSCAPS2)==16);
        static_assert(offsetof(DDCAPS,dwVidMemTotal)==60 && offsetof(DDCAPS,ddsCaps)==364);
        DDCAPS driver{},emulation{};driver.dwSize=emulation.dwSize=380;
        for(auto at:{a[1],a[2]})if(at){
            p.memory.check(at,380,Memory::Read|Memory::Write);
            if(p.memory.load(at,32)!=380)stop("GetCaps requires the 380-byte DX7 DDCAPS layout");
        }
        if(a[1] && a[2] && std::uint64_t(a[1])<std::uint64_t(a[2])+380 && std::uint64_t(a[2])<std::uint64_t(a[1])+380)
            stop("overlapping GetCaps outputs outside this profile");
        const auto hr=native->GetCaps(a[1]?&driver:nullptr,a[2]?&emulation:nullptr);
        if(SUCCEEDED(hr)){
            // The DWORD-only structure has no host pointers or padding. Clear
            // reserved words rather than exposing implementation-private data.
            driver.dwReserved1=driver.dwReserved2=driver.dwReserved3=0;
            emulation.dwReserved1=emulation.dwReserved2=emulation.dwReserved3=0;
            if(a[1])p.memory.copy_in(a[1],std::span(reinterpret_cast<const std::uint8_t*>(&driver),380));
            if(a[2])p.memory.copy_in(a[2],std::span(reinterpret_cast<const std::uint8_t*>(&emulation),380));
        }
        return U32(hr);
    }
    U32 device_identifier(Args a){
        auto native=static_cast<IDirectDraw7*>(object(a[0],Interface::draw7).native);
        constexpr U32 GuestSize=1068;
        p.memory.check(a[1],GuestSize,Memory::Write);
        DDDEVICEIDENTIFIER2 value{};
        const auto hr=native->GetDeviceIdentifier(&value,a[2]);
        if(FAILED(hr))return U32(hr);
        static_assert(MAX_DDDEVICEID_STRING==512);
        static_assert(offsetof(DDDEVICEIDENTIFIER2,liDriverVersion)==1024);
        static_assert(offsetof(DDDEVICEIDENTIFIER2,dwVendorId)==1032);
        static_assert(offsetof(DDDEVICEIDENTIFIER2,guidDeviceIdentifier)==1048);
        static_assert(offsetof(DDDEVICEIDENTIFIER2,dwWHQLLevel)==1064);
        std::array<std::uint8_t,GuestSize> bytes{};
        auto text=[&](std::size_t at,const char* v){
            std::size_t n=0;while(n<512 && v[n])++n;
            if(n==512)stop("unterminated device identifier string");
            std::memcpy(bytes.data()+at,v,n);
        };
        text(0,value.szDriver);text(512,value.szDescription);
        // No native sizeof: on Win64 this type has tail padding after WHQL.
        const std::array<U32,6> words{value.liDriverVersion.LowPart,U32(value.liDriverVersion.HighPart),
            value.dwVendorId,value.dwDeviceId,value.dwSubSysId,value.dwRevision};
        for(unsigned n=0;n<words.size();++n)for(unsigned k=0;k<4;++k)bytes[1024+4*n+k]=std::uint8_t(words[n]>>(8*k));
        std::memcpy(bytes.data()+1048,&value.guidDeviceIdentifier,16);
        for(unsigned k=0;k<4;++k)bytes[1064+k]=std::uint8_t(value.dwWHQLLevel>>(8*k));
        p.memory.copy_in(a[1],bytes);return U32(hr);
    }
    DDSURFACEDESC2 read_description(U32 at){
        p.memory.check(at,DescSize,Memory::Read);
        if(p.memory.load(at,32)!=DescSize)stop("DDSURFACEDESC2 must have the 124-byte x86 layout");
        DDSURFACEDESC2 d{};d.dwSize=sizeof(d);d.dwFlags=p.memory.load(at+4,32);
        constexpr DWORD allowed=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT;
        if((d.dwFlags&~allowed) || !(d.dwFlags&DDSD_CAPS))stop("unmodelled surface descriptor flags");
        if(p.memory.load(at+36,32))stop("caller-owned native surface memory is not supported");
        static_assert(sizeof(DDPIXELFORMAT)==32 && sizeof(DDSCAPS2)==16);
        p.memory.copy_out(at+104,std::span(reinterpret_cast<std::uint8_t*>(&d.ddsCaps),16));
        if(d.ddsCaps.dwCaps2 || d.ddsCaps.dwCaps3 || d.ddsCaps.dwCaps4)stop("extended surface caps are not supported");
        // A primary surface inherits the host display dimensions/format. Never
        // manufacture an offscreen surface or invent these values for the guest.
        if(d.dwFlags==DDSD_CAPS && d.ddsCaps.dwCaps==DDSCAPS_PRIMARYSURFACE)return d;
        if((d.dwFlags&(DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT))!=(DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT))
            stop("offscreen surface requires explicit dimensions");
        const DWORD caps=d.ddsCaps.dwCaps;
        const bool plain=caps==(DDSCAPS_OFFSCREENPLAIN|DDSCAPS_SYSTEMMEMORY);
        const bool render=(caps&(DDSCAPS_OFFSCREENPLAIN|DDSCAPS_3DDEVICE))==(DDSCAPS_OFFSCREENPLAIN|DDSCAPS_3DDEVICE)
            && !(caps&~DWORD(DDSCAPS_OFFSCREENPLAIN|DDSCAPS_3DDEVICE|DDSCAPS_SYSTEMMEMORY|DDSCAPS_VIDEOMEMORY))
            && (caps&(DDSCAPS_SYSTEMMEMORY|DDSCAPS_VIDEOMEMORY))!=(DDSCAPS_SYSTEMMEMORY|DDSCAPS_VIDEOMEMORY);
        const bool texture=p.options.legacy_d3d9 && caps==(DDSCAPS_TEXTURE|DDSCAPS_SYSTEMMEMORY);
        const bool depth=p.options.legacy_d3d9 && (caps==DDSCAPS_ZBUFFER || caps==(DDSCAPS_ZBUFFER|DDSCAPS_VIDEOMEMORY));
        if(depth){
            if(!(d.dwFlags&DDSD_PIXELFORMAT))stop("depth surface requires explicit D16 format");
            p.memory.copy_out(at+72,std::span(reinterpret_cast<std::uint8_t*>(&d.ddpfPixelFormat),32));
            const auto& f=d.ddpfPixelFormat;
            if(f.dwSize!=32 || f.dwFlags!=DDPF_ZBUFFER || f.dwFourCC || f.dwZBufferBitDepth!=16 ||
               f.dwStencilBitDepth || f.dwZBitMask!=0xffff || f.dwStencilBitMask || f.dwRGBZBitMask)
                stop("only non-stencil D16 depth surfaces are supported");
            d.dwHeight=p.memory.load(at+8,32);d.dwWidth=p.memory.load(at+12,32);
            if(!d.dwWidth || !d.dwHeight || d.dwWidth>2048 || d.dwHeight>2048)stop("depth surface size outside profile");
            return d;
        }
        if(!plain && !render && !texture)stop("unmodelled offscreen surface capability combination");
        if(!(d.dwFlags&DDSD_PIXELFORMAT) && !render)stop("plain offscreen surface requires an explicit pixel format");
        if(d.dwFlags&DDSD_PIXELFORMAT){
            p.memory.copy_out(at+72,std::span(reinterpret_cast<std::uint8_t*>(&d.ddpfPixelFormat),32));
            const auto& f=d.ddpfPixelFormat;
            if(f.dwSize!=32 || (f.dwFlags!=DDPF_RGB && !(texture && f.dwFlags==(DDPF_RGB|DDPF_ALPHAPIXELS))) || f.dwFourCC || f.dwRGBBitCount!=32 || f.dwRBitMask!=0xff0000 || f.dwGBitMask!=0xff00 || f.dwBBitMask!=0xff || f.dwRGBAlphaBitMask!=((f.dwFlags&DDPF_ALPHAPIXELS)?0xff000000u:0u))
                stop("only an explicit X8R8G8B8 or a native default render format is supported");
        }
        d.dwHeight=p.memory.load(at+8,32);d.dwWidth=p.memory.load(at+12,32);
        if(texture && ((d.dwWidth&(d.dwWidth-1)) || (d.dwHeight&(d.dwHeight-1)) || d.dwWidth>2048 || d.dwHeight>2048))stop("single-level power-of-two texture dimensions required");
        if(!d.dwWidth || !d.dwHeight || std::uint64_t(d.dwWidth)*d.dwHeight>MaxSurfaceBytes/4)stop("surface allocation budget");
        return d;
    }
    U32 create_surface(Args a){
        auto description=read_description(a[1]);p.memory.check(a[2],4,Memory::Write);
        if(a[3])stop("aggregated surfaces not supported");
        if(p.options.legacy_d3d9 && (description.ddsCaps.dwCaps&DDSCAPS_ZBUFFER))return create_depth9(a,description);
        std::shared_ptr<TextureModel> model;
        auto surface_model=p.options.legacy_d3d9?std::make_shared<SurfaceModel>():nullptr;
        if(surface_model)surface_model->primary=(description.ddsCaps.dwCaps&DDSCAPS_PRIMARYSURFACE)!=0;
        if(p.options.legacy_d3d9 && (description.ddsCaps.dwCaps&DDSCAPS_TEXTURE)){
            model=std::make_shared<TextureModel>();model->caps=description.ddsCaps;
            // Allocate real CPU storage, not a host legacy texture interface.
            // Pixel format and dimensions remain exactly those requested.
            description.ddsCaps.dwCaps=DDSCAPS_OFFSCREENPLAIN|DDSCAPS_SYSTEMMEMORY;
        }
        IDirectDrawSurface7* surface{};
        auto hr=static_cast<IDirectDraw7*>(object(a[0],Interface::draw7).native)->CreateSurface(&description,&surface,nullptr);
        if(SUCCEEDED(hr)){
            if(!surface)stop("native CreateSurface returned success without storage");
            p.memory.store(a[2],wrap(surface,Interface::surface7,std::move(model),std::move(surface_model)),32);
        }
        return U32(hr);
    }
    void guest_surface_description(const Object& value,DDSURFACEDESC2& d){
        if(value.texture_model){d.ddsCaps=value.texture_model->caps;d.dwFlags|=DDSD_CAPS;}
    }
    void write_description(U32 at,const DDSURFACEDESC2& d,U32 pixels=0){
        p.memory.check(at,DescSize,Memory::Write);
        constexpr DWORD allowed=DDSD_CAPS|DDSD_HEIGHT|DDSD_WIDTH|DDSD_PITCH|DDSD_LINEARSIZE|DDSD_BACKBUFFERCOUNT|DDSD_MIPMAPCOUNT|DDSD_REFRESHRATE|DDSD_ALPHABITDEPTH|DDSD_LPSURFACE|DDSD_CKDESTOVERLAY|DDSD_CKDESTBLT|DDSD_CKSRCOVERLAY|DDSD_CKSRCBLT|DDSD_PIXELFORMAT|DDSD_TEXTURESTAGE;
        if(d.dwSize!=sizeof(d) || (d.dwFlags&~allowed))stop("unmodelled native surface descriptor fields");
        if((d.dwFlags&DDSD_PIXELFORMAT) && d.ddpfPixelFormat.dwSize!=32)stop("native surface pixel format size");
        std::array<U32,31> out{};out[0]=DescSize;out[1]=d.dwFlags;
        if(d.dwFlags&DDSD_HEIGHT)out[2]=d.dwHeight;
        if(d.dwFlags&DDSD_WIDTH)out[3]=d.dwWidth;
        if(d.dwFlags&(DDSD_PITCH|DDSD_LINEARSIZE))out[4]=U32(d.lPitch);
        if(d.dwFlags&DDSD_BACKBUFFERCOUNT)out[5]=d.dwBackBufferCount;
        if(d.dwFlags&(DDSD_MIPMAPCOUNT|DDSD_REFRESHRATE))out[6]=d.dwMipMapCount;
        if(d.dwFlags&DDSD_ALPHABITDEPTH)out[7]=d.dwAlphaBitDepth;
        // Lock's native address is replaced with the staging allocation even if
        // the host omits DDSD_LPSURFACE in its returned flags. Never copy it.
        out[9]=pixels;
        if(d.dwFlags&DDSD_CKDESTOVERLAY)std::memcpy(out.data()+10,&d.ddckCKDestOverlay,8);
        if(d.dwFlags&DDSD_CKDESTBLT)std::memcpy(out.data()+12,&d.ddckCKDestBlt,8);
        if(d.dwFlags&DDSD_CKSRCOVERLAY)std::memcpy(out.data()+14,&d.ddckCKSrcOverlay,8);
        if(d.dwFlags&DDSD_CKSRCBLT)std::memcpy(out.data()+16,&d.ddckCKSrcBlt,8);
        if(d.dwFlags&DDSD_PIXELFORMAT)std::memcpy(out.data()+18,&d.ddpfPixelFormat,32);
        if(d.dwFlags&DDSD_CAPS)std::memcpy(out.data()+26,&d.ddsCaps,16);
        if(d.dwFlags&DDSD_TEXTURESTAGE)out[30]=d.dwTextureStage;
        for(unsigned n=0;n<out.size();++n)p.memory.store(at+4*n,out[n],32);
    }
    HWND guest_window(U32 token){
        if(!token)return nullptr;
        try{return reinterpret_cast<HWND>(p.gui()->native_window(token));}
        catch(const GuestFault&){throw;}
        catch(const std::exception& error){stop(std::string("guest window lookup: ")+error.what());}
    }
    U32 create_clipper(Args a){
        auto native=static_cast<IDirectDraw7*>(object(a[0],Interface::draw7).native);
        p.memory.check(a[2],4,Memory::Write);if(a[3])stop("COM clipper aggregation is unsupported");
        IDirectDrawClipper* value{};const auto hr=native->CreateClipper(a[1],&value,nullptr);
        if(SUCCEEDED(hr)){
            if(!value)stop("native CreateClipper succeeded without an object");
            // Reset association on creation: native addresses may be reused.
            NativeOwner owner{value};clipper_windows[reinterpret_cast<std::uintptr_t>(value)]={nullptr,0};
            auto address=wrap(owner.keep(),Interface::clipper);p.memory.store(a[2],address,32);
        }
        return U32(hr);
    }
    U32 set_clipper_window(Args a){
        auto native=static_cast<IDirectDrawClipper*>(object(a[0],Interface::clipper).native);
        const auto window=guest_window(a[2]);
        const auto hr=native->SetHWnd(a[1],window);
        if(SUCCEEDED(hr))clipper_windows[reinterpret_cast<std::uintptr_t>(native)]={window,a[2]};
        return U32(hr);
    }
    U32 get_clipper_window(Args a){
        auto native=static_cast<IDirectDrawClipper*>(object(a[0],Interface::clipper).native);
        p.memory.check(a[1],4,Memory::Write);HWND window{};const auto hr=native->GetHWnd(&window);
        if(SUCCEEDED(hr)){
            U32 token=0;
            if(window){auto it=clipper_windows.find(reinterpret_cast<std::uintptr_t>(native));
                if(it==clipper_windows.end() || it->second.first!=window)stop("foreign clipper window handle");
                token=it->second.second;}
            p.memory.store(a[1],token,32);
        }
        return U32(hr);
    }
    U32 present9(Object& destination,Args a){
        if(!p.options.legacy_d3d9 || !destination.surface_model || !destination.surface_model->primary)
            stop("present requires a D3D9-profile primary surface");
        if(destination.locked || !a[2])stop("present with locked destination or null source");
        if(a[4]&~U32(DDBLT_WAIT))stop("present supports only DDBLT_WAIT");
        if(a[5])stop("present does not accept DDBLTFX");
        auto& source=object(a[2],Interface::surface7);
        if(source.locked || !source.surface_model)stop("present source is unavailable or locked");
        auto device=source.surface_model->device.lock();
        if(!device || device->target!=source.native || device->gpu->in_scene())stop("present source is not an idle D3D9 render target");
        // Confirm the two legacy surfaces belong to the same DirectDraw root.
        void* parent{};auto hr=static_cast<IDirectDrawSurface7*>(destination.native)->GetDDInterface(&parent);
        if(FAILED(hr))return U32(hr);if(!parent)stop("primary surface has no DirectDraw owner");
        NativeOwner parent_owner{static_cast<IUnknown*>(parent)};
        if(identity9(parent_owner.value)!=identity9(device->root))stop("present crosses DirectDraw roots");
        const auto key=identity9(device->root);auto cooperative=cooperative_windows.find(key);
        if(cooperative==cooperative_windows.end() || !cooperative->second)stop("present has no cooperative window");
        auto window=cooperative->second;RECT client{};if(!GetClientRect(window,&client))return U32(HRESULT_FROM_WIN32(GetLastError()));
        if(client.left!=0 || client.top!=0 || client.right!=LONG(device->width) || client.bottom!=LONG(device->height))
            stop("present requires an exact-size client area; scaling is outside this profile");
        POINT origin{client.left,client.top};
        if(!ClientToScreen(window,&origin))return U32(HRESULT_FROM_WIN32(GetLastError()));
        const RECT expected_dest{origin.x,origin.y,origin.x+LONG(device->width),origin.y+LONG(device->height)};
        const RECT expected_src{0,0,LONG(device->width),LONG(device->height)};
        auto read_rect=[&](U32 at,const RECT& expected,const char* label){
            if(!at)stop(std::string("present requires explicit ")+label+" rectangle");
            p.memory.check(at,16,Memory::Read);RECT value{};p.memory.copy_out(at,std::span(reinterpret_cast<std::uint8_t*>(&value),sizeof(value)));
            if(std::memcmp(&value,&expected,sizeof(value))!=0)stop(std::string("present ")+label+" rectangle outside exact windowed profile");
        };
        read_rect(a[1],expected_dest,"destination");read_rect(a[3],expected_src,"source");
        // CPU writes to the legacy render target must be visible before display.
        hr=transfer9(*device,false);if(host9::failed(U32(hr)))return U32(hr);
        const auto shown=device->gpu->present();if(!host9::failed(shown)){++renderer9_presents;++blits;}return shown;
    }
    U32 lock(Object& value,Args a){
        if(a[1] || a[4])stop("surface Lock supports the whole surface and no event handle");
        if(a[3]&~U32(DDLOCK_WAIT|DDLOCK_READONLY|DDLOCK_WRITEONLY|DDLOCK_NOSYSLOCK|DDLOCK_DONOTWAIT))stop("unimplemented Lock flags");
        if(value.locked)stop("surface already locked");
        p.memory.check(a[2],DescSize,Memory::Read|Memory::Write);if(p.memory.load(a[2],32)!=DescSize)stop("Lock description size");
        auto s=static_cast<IDirectDrawSurface7*>(value.native);auto state=std::make_unique<SurfaceLock>();auto& d=state->description;d.dwSize=sizeof(d);
        const auto hr=s->Lock(nullptr,&d,a[3],nullptr);if(FAILED(hr))return U32(hr);
        try {
            const std::int64_t pitch=d.lPitch;const auto stride=std::uint64_t(pitch<0?-pitch:pitch);const auto row=std::uint64_t(d.dwWidth)*4;
            const auto bytes=stride*std::uint64_t(d.dwHeight?d.dwHeight-1:0)+row;
            if(!d.lpSurface || !d.dwHeight || !d.dwWidth || d.ddpfPixelFormat.dwRGBBitCount!=32 || !(d.ddpfPixelFormat.dwFlags&DDPF_RGB) || stride<row || bytes>MaxSurfaceBytes)stop("unhandled native lock shape");
            Allocation staging(p,std::size_t(bytes));state->size=std::size_t(bytes);state->row_bytes=std::size_t(row);state->readonly=(a[3]&DDLOCK_READONLY)!=0;
            const auto top=staging.address+(pitch<0?U32(stride*(d.dwHeight-1)):0);
            for(U32 y=0;y<d.dwHeight;++y){const auto offset=std::int64_t(y)*pitch;auto* native=static_cast<const std::uint8_t*>(d.lpSurface)+offset;p.memory.copy_in(U32(std::int64_t(top)+offset),std::span(native,std::size_t(row)));}
            if(state->readonly)p.memory.protect(staging.address,(bytes+4095)&~4095ull,Memory::Read);
            auto guest=d;guest_surface_description(value,guest);write_description(a[2],guest,top);state->guest=staging.keep();value.locked=std::move(state);++locks;return U32(hr);
        }catch(...){s->Unlock(nullptr);throw;}
    }
    U32 unlock(Object& value,Args a){
        if(a[1])stop("surface Unlock rectangle not supported");
        if(!value.locked)return U32(static_cast<IDirectDrawSurface7*>(value.native)->Unlock(nullptr));
        auto& state=*value.locked;const auto& d=state.description;const std::int64_t pitch=d.lPitch;
        const auto top=state.guest+(pitch<0?U32(-pitch*(d.dwHeight-1)):0);
        if(!state.readonly){
            // Validate and stage everything before touching a locked native surface.
            std::vector<std::uint8_t> bytes(state.row_bytes*d.dwHeight);
            for(U32 y=0;y<d.dwHeight;++y)p.memory.copy_out(U32(std::int64_t(top)+std::int64_t(y)*pitch),std::span(bytes).subspan(y*state.row_bytes,state.row_bytes));
            for(U32 y=0;y<d.dwHeight;++y)std::memcpy(static_cast<std::uint8_t*>(d.lpSurface)+std::int64_t(y)*pitch,bytes.data()+y*state.row_bytes,state.row_bytes);
        }
        auto result=static_cast<IDirectDrawSurface7*>(value.native)->Unlock(nullptr);if(SUCCEEDED(result)){p.memory.release(state.guest);value.locked.reset();++unlocks;}return U32(result);
    }
    #include "directdraw_depth.hpp"
    #include "directdraw_d3d9.hpp"
    void install(){
        api("DirectDrawEnumerateExA",3,[this](Args a){p.memory.check(a[0],1,Memory::Execute);Enumeration call{this,a[0],a[1],{},0};const auto hr=DirectDrawEnumerateExA(enumerate,&call,a[2]);if(call.failure)std::rethrow_exception(call.failure);return U32(hr);});
        api("DirectDrawCreateEx",4,[this](Args a){
            if(p.options.legacy_d3d9 && a[0])stop("D3D9 profile supports only the default adapter");p.memory.check(a[1],4,Memory::Write);const auto iid=guid(a[2]);if(!IsEqualIID(iid,IID_IDirectDraw7))return U32(DDERR_INVALIDPARAMS);if(a[3])stop("COM aggregation is not supported");
            GUID id{};GUID* ptr=nullptr;if(a[0] && a[0]<=2)ptr=reinterpret_cast<GUID*>(std::uintptr_t(a[0]));else if(a[0]){id=guid(a[0]);ptr=&id;}
            IDirectDraw7* object=nullptr;const auto hr=DirectDrawCreateEx(ptr,reinterpret_cast<void**>(&object),IID_IDirectDraw7,nullptr);if(SUCCEEDED(hr))p.memory.store(a[1],wrap(object,Interface::draw7),32);return U32(hr);
        });
        api("DirectDrawCreate",3,[this](Args a){if(p.options.legacy_d3d9 && a[0])stop("D3D9 profile supports only the default adapter");p.memory.check(a[1],4,Memory::Write);if(a[2])stop("COM aggregation is not supported");GUID id{};GUID* ptr=nullptr;if(a[0] && a[0]<=2)ptr=reinterpret_cast<GUID*>(std::uintptr_t(a[0]));else if(a[0]){id=guid(a[0]);ptr=&id;}IDirectDraw* object=nullptr;const auto hr=DirectDrawCreate(ptr,&object,nullptr);if(SUCCEEDED(hr))p.memory.store(a[1],wrap(object,Interface::draw1),32);return U32(hr);});
    }
public:
    explicit Draw(Process& q):p(q){install();}
    ~Draw() override{shutdown();}
    void shutdown() noexcept override{
        if(stopped)return;stopped=true;
        for(auto& [address,value]:objects){(void)address;if(value.compat){value.compat.reset();value.guest_refs=0;}}
        for(auto& [address,value]:objects){(void)address;value.surface_model.reset();value.depth_model.reset();}
        for(auto& [address,value]:objects){(void)address;if(value.native)destroy_lock(value);}
        for(auto kind:{Interface::device7,Interface::surface7,Interface::clipper,Interface::d3d7,Interface::compat3d7,Interface::unknown,Interface::draw1,Interface::draw7})for(auto& [address,value]:objects){(void)address;if(value.native && value.kind==kind){for(U32 n=0;n<value.guest_refs;++n)value.native->Release();value.native=nullptr;}}
        cooperative_windows.clear();root_identities.clear();
    }
    std::string report() const override{std::ostringstream out;out<<"{\"backend\":"<<quote(p.options.legacy_d3d9?"ddraw-d3d9-bounded":"native-ddraw7")<<",\"d3d9_devices\":"<<renderer9_devices<<",\"d3d9_draws\":"<<renderer9_draws<<",\"d3d9_indexed_draws\":"<<renderer9_indexed_draws<<",\"d3d9_clears\":"<<renderer9_clears<<",\"d3d9_readbacks\":"<<renderer9_readbacks<<",\"d3d9_presents\":"<<renderer9_presents<<",\"d3d9_depth_surfaces\":"<<renderer9_depth_surfaces<<",\"d3d9_depth_binds\":"<<renderer9_depth_binds<<",\"objects_created\":"<<created<<",\"objects_retired\":"<<retired<<",\"adapter_callbacks\":"<<enumerated<<",\"mode_callbacks\":"<<mode_callbacks<<",\"d3d7_wrappers\":"<<d3d_wrappers<<",\"d3d_device_callbacks\":"<<device_callbacks<<",\"mode7_callbacks\":"<<mode7_callbacks<<",\"zformat_callbacks\":"<<zformat_callbacks<<",\"d3d_devices_created\":"<<render_devices<<",\"texture_callbacks\":"<<texture_callbacks<<",\"surface_locks\":"<<locks<<",\"surface_unlocks\":"<<unlocks<<",\"blits\":"<<blits<<"}";return out.str();}
};
U32 Draw::vtable(Interface kind){
    if(auto it=vtables.find(kind);it!=vtables.end())return it->second;
    const unsigned count=kind==Interface::unknown?3u:kind==Interface::draw1?23u:kind==Interface::draw7?30u:(kind==Interface::d3d7 || kind==Interface::compat3d7)?8u:kind==Interface::clipper?9u:49u;
    Allocation table(p,count*4);
    for(unsigned slot=0;slot<count;++slot){
        const auto name=std::string("WinRecompCOM.")+std::to_string(unsigned(kind))+"."+std::to_string(slot);
        unsigned argc=1;Function fn=[this,kind,slot](Args a)->U32{object(a[0],kind);stop("unimplemented COM method "+std::to_string(unsigned(kind))+":"+std::to_string(slot));};
        auto method=[&](unsigned n,Function f){argc=n;fn=std::move(f);};
        if(slot==0)method(3,[this,kind](Args a){return query(object(a[0],kind),a);});
        else if(slot==1)method(1,[this,kind](Args a){auto& value=object(a[0],kind);if(value.guest_refs==0xffffffffu)stop("reference count overflow");const auto result=(value.compat || value.depth_model)?value.guest_refs+1:value.native->AddRef();++value.guest_refs;return result;});
        else if(slot==2)method(1,[this,kind](Args a){return release(object(a[0],kind));});
        if(kind==Interface::draw1 && slot==8)method(5,[this](Args a){return enum_modes(a);});
        if(kind==Interface::draw7){
            if(slot==4)method(4,[this](Args a){return create_clipper(a);});
            if(slot==6)method(4,[this](Args a){return create_surface(a);});
            if(slot==8)method(5,[this](Args a){return enum_modes7(a);});
            if(slot==11)method(3,[this](Args a){return draw_caps(a);});
            if(slot==12)method(2,[this](Args a){p.memory.check(a[1],DescSize,Memory::Read|Memory::Write);if(p.memory.load(a[1],32)!=DescSize)stop("GetDisplayMode description size");DDSURFACEDESC2 d{};d.dwSize=sizeof(d);auto hr=static_cast<IDirectDraw7*>(object(a[0],Interface::draw7).native)->GetDisplayMode(&d);if(SUCCEEDED(hr)){if(d.lpSurface)stop("GetDisplayMode returned a native pixel pointer");write_description(a[1],d);}return U32(hr);});
            if(slot==20)method(3,[this](Args a){if(a[2]!=DDSCL_NORMAL)stop("only normal/windowed cooperative mode is supported");auto window=guest_window(a[1]);auto root=static_cast<IDirectDraw7*>(object(a[0],Interface::draw7).native);const auto hr=root->SetCooperativeLevel(window,a[2]);if(SUCCEEDED(hr))cooperative_windows[identity9(root)]=window;return U32(hr);});
            if(slot==27)method(3,[this](Args a){return device_identifier(a);});
            if(slot==26)method(1,[this](Args a){return U32(static_cast<IDirectDraw7*>(object(a[0],Interface::draw7).native)->TestCooperativeLevel());});
        }
        if(kind==Interface::d3d7){
            if(slot==4)method(4,[this](Args a){return create_device(a);});
            if(slot==3)method(3,[this](Args a){return enum_devices(a);});
            if(slot==6)method(4,[this](Args a){return enum_zformats(a);});
            if(slot==7)method(1,[this](Args a){return U32(static_cast<IDirect3D7*>(object(a[0],Interface::d3d7).native)->EvictManagedTextures());});
        }
        if(kind==Interface::device7){
            if(slot==3)method(2,[this](Args a){auto native=static_cast<IDirect3DDevice7*>(object(a[0],Interface::device7).native);p.memory.check(a[1],DeviceDescSize,Memory::Write);D3DDEVICEDESC7 caps{};auto hr=native->GetCaps(&caps);if(SUCCEEDED(hr))write_device(a[1],caps);return U32(hr);});
            if(slot==4)method(3,[this](Args a){return enum_texture_formats(a);});
            if(slot==7)method(2,[this](Args a){auto native=static_cast<IDirect3DDevice7*>(object(a[0],Interface::device7).native);p.memory.check(a[1],4,Memory::Write);IDirect3D7* result{};auto hr=native->GetDirect3D(&result);if(SUCCEEDED(hr)){if(!result)stop("native device returned no Direct3D interface");p.memory.store(a[1],wrap(result,Interface::d3d7),32);}return U32(hr);});
            if(slot==9)method(2,[this](Args a){auto native=static_cast<IDirect3DDevice7*>(object(a[0],Interface::device7).native);p.memory.check(a[1],4,Memory::Write);IDirectDrawSurface7* result{};auto hr=native->GetRenderTarget(&result);if(SUCCEEDED(hr)){if(!result)stop("native device returned no render target");p.memory.store(a[1],wrap(result,Interface::surface7),32);}return U32(hr);});
        }
        if(kind==Interface::compat3d7){
            if(slot==3)method(3,[this](Args a){return enum_devices9(a);});
            if(slot==4)method(4,[this](Args a){return create_device9(a);});
            if(slot==6)method(4,[this](Args a){return enum_depth9(a);});
        }
        if(kind==Interface::compatdevice7){
            if(slot==3)method(2,[this](Args a){object(a[0],Interface::compatdevice7);p.memory.check(a[1],DeviceDescSize,Memory::Write);D3DDEVICEDESC7 caps{};auto hr=caps9(caps);if(!host9::failed(hr))write_device(a[1],caps);return hr;});
            if(slot==4)method(3,[this](Args a){return enum_textures9(a);});
            if(slot==5)method(1,[this](Args a){return begin9(a);});
            if(slot==6)method(1,[this](Args a){return end9(a);});
            if(slot==7)method(2,[this](Args a){auto d=object(a[0],Interface::compatdevice7).compat;p.memory.check(a[1],4,Memory::Write);d->root->AddRef();p.memory.store(a[1],wrap(d->root,Interface::compat3d7),32);return U32(S_OK);});
            if(slot==9)method(2,[this](Args a){auto d=object(a[0],Interface::compatdevice7).compat;p.memory.check(a[1],4,Memory::Write);d->target->AddRef();p.memory.store(a[1],wrap(d->target,Interface::surface7,{},d->surface_model),32);return U32(S_OK);});
            if(slot==10)method(7,[this](Args a){return clear9(a);});
            if(slot==13)method(2,[this](Args a){auto d=object(a[0],Interface::compatdevice7).compat;host9::Viewport v{};p.memory.copy_out(a[1],std::span(reinterpret_cast<std::uint8_t*>(&v),sizeof(v)));return d->gpu->set_viewport(v);});
            if(slot==15)method(2,[this](Args a){auto d=object(a[0],Interface::compatdevice7).compat;p.memory.check(a[1],24,Memory::Write);host9::Viewport v{};auto hr=d->gpu->get_viewport(v);if(!host9::failed(hr))p.memory.copy_in(a[1],std::span(reinterpret_cast<const std::uint8_t*>(&v),sizeof(v)));return hr;});
            if(slot==20)method(3,[this](Args a){auto d=object(a[0],Interface::compatdevice7).compat;auto hr=d->gpu->set_state(a[1],a[2]);if(hr==U32(E_NOTIMPL))stop("render state/value outside bounded D3D9 profile");return hr;});
            if(slot==21)method(3,[this](Args a){auto d=object(a[0],Interface::compatdevice7).compat;p.memory.check(a[2],4,Memory::Write);U32 value{};auto hr=d->gpu->get_state(a[1],value);if(hr==U32(E_NOTIMPL))stop("render-state query outside bounded D3D9 profile");if(!host9::failed(hr))p.memory.store(a[2],value,32);return hr;});
            if(slot==25)method(6,[this](Args a){return triangles9(a);});
            if(slot==26)method(8,[this](Args a){return indexed_triangles9(a);});
            if(slot==34)method(3,[this](Args a){return get_texture9(a);});
            if(slot==35)method(3,[this](Args a){return set_texture9(a);});
            if(slot==36)method(4,[this](Args a){return stage9(a,false);});
            if(slot==37)method(4,[this](Args a){return stage9(a,true);});
        }
        if(kind==Interface::clipper){
            if(slot==4)method(2,[this](Args a){return get_clipper_window(a);});
            if(slot==6)method(2,[this](Args a){auto native=static_cast<IDirectDrawClipper*>(object(a[0],Interface::clipper).native);p.memory.check(a[1],4,Memory::Write);BOOL changed{};auto hr=native->IsClipListChanged(&changed);if(SUCCEEDED(hr))p.memory.store(a[1],U32(changed),32);return U32(hr);});
            if(slot==8)method(3,[this](Args a){return set_clipper_window(a);});
        }
        if(kind==Interface::depth7){
            if(slot==14)method(2,[this](Args a){auto d=object(a[0],Interface::depth7).depth_model;p.memory.copy_in(a[1],std::span(reinterpret_cast<const std::uint8_t*>(&d->description.ddsCaps),16));return U32(DD_OK);});
            if(slot==21)method(2,[this](Args a){auto d=object(a[0],Interface::depth7).depth_model;p.memory.check(a[1],32,Memory::Read|Memory::Write);if(p.memory.load(a[1],32)!=32)stop("depth GetPixelFormat size");p.memory.copy_in(a[1],std::span(reinterpret_cast<const std::uint8_t*>(&d->description.ddpfPixelFormat),32));return U32(DD_OK);});
            if(slot==22)method(2,[this](Args a){auto d=object(a[0],Interface::depth7).depth_model;p.memory.check(a[1],DescSize,Memory::Read|Memory::Write);if(p.memory.load(a[1],32)!=DescSize)stop("depth GetSurfaceDesc size");write_description(a[1],d->description);return U32(DD_OK);});
            if(slot==36)method(2,[this](Args a){auto d=object(a[0],Interface::depth7).depth_model;p.memory.check(a[1],4,Memory::Write);d->root->AddRef();p.memory.store(a[1],wrap(d->root.get(),Interface::draw7),32);return U32(DD_OK);});
        }
        if(kind==Interface::surface7){
            if(slot==3)method(2,[this](Args a){return attach_depth9(a);});
            if(slot==8)method(3,[this](Args a){return detach_depth9(a);});
            if(slot==12)method(3,[this](Args a){return attached_depth9(a);});
            if(slot==15)method(2,[this](Args a){auto native=static_cast<IDirectDrawSurface7*>(object(a[0],Interface::surface7).native);p.memory.check(a[1],4,Memory::Write);IDirectDrawClipper* result{};auto hr=native->GetClipper(&result);if(SUCCEEDED(hr)){if(!result)stop("native GetClipper succeeded without an object");p.memory.store(a[1],wrap(result,Interface::clipper),32);}return U32(hr);});
            if(slot==28)method(2,[this](Args a){auto& surface=object(a[0],Interface::surface7);if(surface.locked)stop("SetClipper while surface is locked");auto clipper=a[1]?static_cast<IDirectDrawClipper*>(object(a[1],Interface::clipper).native):nullptr;return U32(static_cast<IDirectDrawSurface7*>(surface.native)->SetClipper(clipper));});
            if(slot==5)method(6,[this](Args a){auto& value=object(a[0],Interface::surface7);if(value.locked)stop("blit while surface is locked");check_target9(value.native);
                if(p.options.legacy_d3d9 && value.surface_model && value.surface_model->primary && a[2])return present9(value,a);
                if(a[1] || a[2] || a[3] || (a[4]&~U32(DDBLT_COLORFILL|DDBLT_WAIT)) || !(a[4]&DDBLT_COLORFILL))stop("only whole-surface color fill or bounded D3D9 presentation is supported");p.memory.check(a[5],100,Memory::Read);if(p.memory.load(a[5],32)!=100)stop("DDBLTFX must have the 100-byte x86 layout");DDBLTFX fx{};fx.dwSize=sizeof(fx);fx.dwFillColor=p.memory.load(a[5]+80,32);auto hr=static_cast<IDirectDrawSurface7*>(value.native)->Blt(nullptr,nullptr,nullptr,a[4],&fx);if(SUCCEEDED(hr))++blits;return U32(hr);});
            if(slot==14)method(2,[this](Args a){
                auto& value=object(a[0],Interface::surface7);p.memory.check(a[1],16,Memory::Write);
                DDSCAPS2 caps{};auto hr=static_cast<IDirectDrawSurface7*>(value.native)->GetCaps(&caps);
                if(SUCCEEDED(hr)){if(value.texture_model)caps=value.texture_model->caps;p.memory.copy_in(a[1],std::span(reinterpret_cast<const std::uint8_t*>(&caps),16));}return U32(hr);
            });
            if(slot==21)method(2,[this](Args a){
                auto native=static_cast<IDirectDrawSurface7*>(object(a[0],Interface::surface7).native);p.memory.check(a[1],32,Memory::Read|Memory::Write);
                if(p.memory.load(a[1],32)!=32)stop("GetPixelFormat description size");DDPIXELFORMAT f{};f.dwSize=32;auto hr=native->GetPixelFormat(&f);
                if(SUCCEEDED(hr))write_texture_format(a[1],f);return U32(hr);
            });
            if(slot==22)method(2,[this](Args a){p.memory.check(a[1],DescSize,Memory::Read|Memory::Write);if(p.memory.load(a[1],32)!=DescSize)stop("GetSurfaceDesc description size");DDSURFACEDESC2 d{};d.dwSize=sizeof(d);auto& value=object(a[0],Interface::surface7);auto hr=static_cast<IDirectDrawSurface7*>(value.native)->GetSurfaceDesc(&d);if(SUCCEEDED(hr)){if((d.dwFlags&DDSD_LPSURFACE) && d.lpSurface)stop("GetSurfaceDesc returned a live native pixel pointer; use Lock");guest_surface_description(value,d);write_description(a[1],d);}return U32(hr);});
            if(slot==24)method(1,[this](Args a){return U32(static_cast<IDirectDrawSurface7*>(object(a[0],Interface::surface7).native)->IsLost());});
            if(slot==25)method(5,[this](Args a){auto& v=object(a[0],Interface::surface7);check_target9(v.native);return lock(v,a);});
            if(slot==32)method(2,[this](Args a){return unlock(object(a[0],Interface::surface7),a);});
        }
        api(name.c_str(),argc,std::move(fn));p.memory.store(table.address+4*slot,p.resolve("ddraw.dll",name),32);
    }
    p.memory.protect(table.address,4096,Memory::Read);const auto result=table.address;vtables.emplace(kind,result);table.keep();return result;
}
}
std::unique_ptr<DirectDrawBackend> install_directdraw(Process& p){return std::make_unique<Draw>(p);}
#endif
}
