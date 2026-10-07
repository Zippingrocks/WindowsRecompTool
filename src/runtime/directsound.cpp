#include "winrecomp/directsound.hpp"
#include "winrecomp/process.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>
#endif

namespace wr {
#ifndef _WIN32
namespace {
class UnavailableSound final:public DirectSoundBackend {
public:
    void shutdown() noexcept override{}
    std::string report() const override{return "{\"backend\":\"unavailable\"}";}
};
}
std::unique_ptr<DirectSoundBackend> install_directsound(Process&){return std::make_unique<UnavailableSound>();}
#else
namespace {
using Args=std::span<const U32>;
enum class Interface {sound,buffer,listener,buffer3d};
constexpr U32 MaxObjects=256,MaxBufferBytes=16u*1024u*1024u;
constexpr GUID IID_KsPropertySet_Observed{0x31efac30,0x515c,0x11d0,{0xa9,0xaa,0x00,0xaa,0x00,0x61,0xbe,0x93}};

struct Allocation {
    Process& p;U32 address{};
    Allocation(Process& q,std::size_t n):p(q),address(q.allocate_bytes(n)){}
    ~Allocation(){if(address)try{p.memory.release(address);}catch(...){}}
    U32 keep(){return std::exchange(address,0u);}
};
struct Object {
    Interface kind{};
    IUnknown* native{};
    U32 guest_refs{};
    bool primary{};
    DWORD flags{};
};

class Sound final:public DirectSoundBackend {
    Process& p;
    DWORD thread{GetCurrentThreadId()};
    bool stopped{};
    std::map<U32,Object> objects;
    std::map<Interface,U32> vtables;
    HMODULE module{};
    using CreateFn=HRESULT (WINAPI*)(LPCGUID,LPDIRECTSOUND*,LPUNKNOWN);
    CreateFn create{};
    std::uint64_t creates{},buffers{},primary_buffers{},secondary_buffers{},cooperative{},caps_queries{},formats{};
    std::uint64_t listeners{},buffers3d{},listener_updates{},buffer3d_updates{},buffer_updates{},plays{};

    [[noreturn]]void stop(const std::string& why){throw GuestFault(FaultKind::unsupported,p.cpu.eip,"DirectSound: "+why);}
    void enter(){if(stopped || GetCurrentThreadId()!=thread)stop("foreign-thread or retired backend");}
    CreateFn create_function(){
        if(create)return create;
        module=LoadLibraryW(L"dsound.dll");
        if(!module)stop("native dsound.dll is unavailable");
        create=reinterpret_cast<CreateFn>(GetProcAddress(module,"DirectSoundCreate"));
        if(!create){FreeLibrary(module);module=nullptr;stop("native dsound.dll lacks DirectSoundCreate");}
        return create;
    }
    GUID guid(U32 at){GUID value{};p.memory.check(at,16,Memory::Read);p.memory.copy_out(at,std::span(reinterpret_cast<std::uint8_t*>(&value),16));return value;}
    Object& object(U32 at,Interface kind){
        auto it=objects.find(at);
        if(it==objects.end() || !it->second.native || !it->second.guest_refs || it->second.kind!=kind)
            stop("stale or wrong-interface guest COM object");
        return it->second;
    }
    float real(U32 bits){
        const auto value=std::bit_cast<float>(bits);
        if(!std::isfinite(value))stop("non-finite 3D audio scalar");
        return value;
    }
    U32 wrap(IUnknown* native,Interface kind,bool primary=false,DWORD flags=0){
        if(!native)stop("attempted to wrap null native object");
        for(auto& [at,o]:objects)if(o.native==native && o.kind==kind){
            if(o.guest_refs==0xffffffffu)stop("reference count overflow");
            ++o.guest_refs;return at;
        }
        if(objects.size()>=MaxObjects)stop("COM object budget exhausted");
        Allocation memory(p,4);p.memory.store(memory.address,vtable(kind),32);p.memory.protect(memory.address,4096,Memory::Read);
        const auto at=memory.keep();objects.emplace(at,Object{kind,native,1,primary,flags});return at;
    }
    U32 release(Object& o){
        const auto n=o.native->Release();
        if(!--o.guest_refs)o.native=nullptr;
        return U32(n);
    }
    U32 self_query(Object& o,Args a){
        p.memory.check(a[1],16,Memory::Read);p.memory.check(a[2],4,Memory::Write);
        const auto id=guid(a[1]);
        bool same=IsEqualIID(id,IID_IUnknown);
        if(o.kind==Interface::sound)same=same||IsEqualIID(id,IID_IDirectSound);
        if(o.kind==Interface::buffer)same=same||IsEqualIID(id,IID_IDirectSoundBuffer);
        if(o.kind==Interface::listener)same=same||IsEqualIID(id,IID_IDirectSound3DListener);
        if(o.kind==Interface::buffer3d)same=same||IsEqualIID(id,IID_IDirectSound3DBuffer);
        if(same){
            if(o.guest_refs==0xffffffffu)stop("reference count overflow");
            o.native->AddRef();++o.guest_refs;p.memory.store(a[2],a[0],32);return U32(S_OK);
        }
        if(IsEqualIID(id,IID_KsPropertySet_Observed)){
            // E3 probes EAX 2.0 through IKsPropertySet and has a clean fallback.
            // Do not advertise an extension path this bridge does not model.
            p.memory.store(a[2],0,32);return U32(E_NOINTERFACE);
        }
        Interface target{};
        bool allowed=false;
        if(o.kind==Interface::buffer && o.primary && IsEqualIID(id,IID_IDirectSound3DListener)){
            target=Interface::listener;allowed=true;
        } else if(o.kind==Interface::buffer && !o.primary && (o.flags&DSBCAPS_CTRL3D) && IsEqualIID(id,IID_IDirectSound3DBuffer)){
            target=Interface::buffer3d;allowed=true;
        }
        if(!allowed){p.memory.store(a[2],0,32);return U32(E_NOINTERFACE);}
        void* result{};const auto hr=o.native->QueryInterface(id,&result);
        if(FAILED(hr)){p.memory.store(a[2],0,32);return U32(hr);}
        if(!result)stop("native QueryInterface succeeded without an object");
        p.memory.store(a[2],wrap(static_cast<IUnknown*>(result),target),32);
        if(target==Interface::listener)++listeners;else ++buffers3d;
        return U32(hr);
    }
    WAVEFORMATEX read_format(U32 at){
        p.memory.check(at,16,Memory::Read);
        WAVEFORMATEX f{};
        f.wFormatTag=WORD(p.memory.load(at,16));
        f.nChannels=WORD(p.memory.load(at+2,16));
        f.nSamplesPerSec=p.memory.load(at+4,32);
        f.nAvgBytesPerSec=p.memory.load(at+8,32);
        f.nBlockAlign=WORD(p.memory.load(at+12,16));
        f.wBitsPerSample=WORD(p.memory.load(at+14,16));
        f.cbSize=0;
        if(f.wFormatTag!=WAVE_FORMAT_PCM || (f.nChannels!=1 && f.nChannels!=2) ||
           (f.nSamplesPerSec!=11025 && f.nSamplesPerSec!=22050) || f.wBitsPerSample!=16 ||
           f.nBlockAlign!=f.nChannels*2 || f.nAvgBytesPerSec!=f.nSamplesPerSec*f.nBlockAlign)
            stop("PCM format outside observed E3 profile");
        return f;
    }
    U32 create_buffer(Args a){
        auto native=static_cast<IDirectSound*>(object(a[0],Interface::sound).native);
        constexpr U32 GuestDesc=36;p.memory.check(a[1],GuestDesc,Memory::Read);p.memory.check(a[2],4,Memory::Write);
        if(a[3])stop("CreateSoundBuffer aggregation is unsupported");
        if(p.memory.load(a[1],32)!=GuestDesc)stop("CreateSoundBuffer requires 36-byte x86 DSBUFFERDESC");
        const DWORD flags=p.memory.load(a[1]+4,32),bytes=p.memory.load(a[1]+8,32),reserved=p.memory.load(a[1]+12,32);
        const U32 format_at=p.memory.load(a[1]+16,32);
        if(reserved)stop("CreateSoundBuffer reserved field is nonzero");
        GUID algorithm{};p.memory.copy_out(a[1]+20,std::span(reinterpret_cast<std::uint8_t*>(&algorithm),16));
        if(!IsEqualGUID(algorithm,GUID_NULL))stop("non-default 3D algorithm is outside observed E3 profile");
        constexpr DWORD Primary=DSBCAPS_PRIMARYBUFFER|DSBCAPS_CTRL3D;
        constexpr DWORD Common=DSBCAPS_STATIC|DSBCAPS_CTRLFREQUENCY|DSBCAPS_CTRLVOLUME|DSBCAPS_GETCURRENTPOSITION2;
        constexpr DWORD ThreeD=Common|DSBCAPS_CTRL3D;
        const bool primary=flags==Primary;
        if(!primary && flags!=Common && flags!=ThreeD)stop("buffer caps outside observed E3 profile");
        WAVEFORMATEX format{};WAVEFORMATEX* native_format=nullptr;
        if(primary){
            if(bytes || format_at)stop("primary buffer must not provide storage or a format in CreateSoundBuffer");
        } else {
            if(!bytes || bytes>MaxBufferBytes || !format_at)stop("secondary buffer size/format outside observed E3 profile");
            format=read_format(format_at);native_format=&format;
        }
        DSBUFFERDESC d{};d.dwSize=sizeof(d);d.dwFlags=flags;d.dwBufferBytes=bytes;d.dwReserved=0;d.lpwfxFormat=native_format;d.guid3DAlgorithm=GUID_NULL;
        IDirectSoundBuffer* result{};const auto hr=native->CreateSoundBuffer(&d,&result,nullptr);
        if(SUCCEEDED(hr)){
            if(!result)stop("CreateSoundBuffer succeeded without a buffer");
            p.memory.store(a[2],wrap(result,Interface::buffer,primary,flags),32);
            ++buffers;if(primary)++primary_buffers;else ++secondary_buffers;
        }
        return U32(hr);
    }
    U32 get_caps(Args a){
        auto native=static_cast<IDirectSound*>(object(a[0],Interface::sound).native);
        constexpr U32 Size=96;p.memory.check(a[1],Size,Memory::Read|Memory::Write);
        if(p.memory.load(a[1],32)!=Size)stop("IDirectSound::GetCaps requires 96-byte DSCAPS");
        DSCAPS caps{};caps.dwSize=sizeof(caps);const auto hr=native->GetCaps(&caps);
        if(SUCCEEDED(hr)){static_assert(sizeof(DSCAPS)==Size);p.memory.copy_in(a[1],std::span(reinterpret_cast<const std::uint8_t*>(&caps),Size));++caps_queries;}
        return U32(hr);
    }
    U32 set_cooperative(Args a){
        auto native=static_cast<IDirectSound*>(object(a[0],Interface::sound).native);
        if(a[2]!=DSSCL_NORMAL && a[2]!=DSSCL_EXCLUSIVE)stop("cooperative level outside observed E3 profile");
        if(!a[1] || !p.gui())stop("SetCooperativeLevel requires an owned guest window");
        std::uintptr_t host{};try{host=p.gui()->native_window(a[1]);}catch(const GuestFault&){throw;}catch(const std::exception& e){stop(std::string("guest window lookup: ")+e.what());}
        if(!host)stop("guest window has no native HWND");
        const auto hr=native->SetCooperativeLevel(reinterpret_cast<HWND>(host),a[2]);if(SUCCEEDED(hr))++cooperative;return U32(hr);
    }
    U32 buffer_position(Args a){
        auto native=static_cast<IDirectSoundBuffer*>(object(a[0],Interface::buffer).native);
        DWORD play{},write{};const auto hr=native->GetCurrentPosition(a[1]?&play:nullptr,a[2]?&write:nullptr);
        if(SUCCEEDED(hr)){if(a[1]){p.memory.check(a[1],4,Memory::Write);p.memory.store(a[1],play,32);}if(a[2]){p.memory.check(a[2],4,Memory::Write);p.memory.store(a[2],write,32);}}
        return U32(hr);
    }
    U32 set_buffer_format(Args a){
        auto native=static_cast<IDirectSoundBuffer*>(object(a[0],Interface::buffer).native);auto f=read_format(a[1]);
        const auto hr=native->SetFormat(&f);if(SUCCEEDED(hr))++formats;return U32(hr);
    }
    U32 buffer_update(Interface kind,unsigned slot,Args a){
        if(kind==Interface::buffer){
            auto native=static_cast<IDirectSoundBuffer*>(object(a[0],kind).native);HRESULT hr=E_NOTIMPL;
            switch(slot){
            case 9:{DWORD status{};p.memory.check(a[1],4,Memory::Write);hr=native->GetStatus(&status);if(SUCCEEDED(hr))p.memory.store(a[1],status,32);break;}
            case 12:if(a[1]||a[2]||(a[3]&~U32(DSBPLAY_LOOPING)))stop("Play arguments outside observed E3 profile");else{hr=native->Play(0,0,a[3]);if(SUCCEEDED(hr))++plays;}break;
            case 13:hr=native->SetCurrentPosition(a[1]);break;
            case 15:hr=native->SetVolume(std::bit_cast<std::int32_t>(a[1]));break;
            case 16:hr=native->SetPan(std::bit_cast<std::int32_t>(a[1]));break;
            case 17:hr=native->SetFrequency(a[1]);break;
            case 18:hr=native->Stop();break;
            case 20:hr=native->Restore();break;
            default:stop("unmodelled buffer update");
            }
            if(SUCCEEDED(hr))++buffer_updates;return U32(hr);
        }
        if(kind==Interface::listener){
            auto native=static_cast<IDirectSound3DListener*>(object(a[0],kind).native);HRESULT hr=E_NOTIMPL;
            switch(slot){
            case 11:hr=native->SetDistanceFactor(real(a[1]),a[2]);break;
            case 12:hr=native->SetDopplerFactor(real(a[1]),a[2]);break;
            case 13:hr=native->SetOrientation(real(a[1]),real(a[2]),real(a[3]),real(a[4]),real(a[5]),real(a[6]),a[7]);break;
            case 14:hr=native->SetPosition(real(a[1]),real(a[2]),real(a[3]),a[4]);break;
            case 15:hr=native->SetRolloffFactor(real(a[1]),a[2]);break;
            case 16:hr=native->SetVelocity(real(a[1]),real(a[2]),real(a[3]),a[4]);break;
            case 17:hr=native->CommitDeferredSettings();break;
            default:stop("unmodelled listener update");
            }
            if(SUCCEEDED(hr))++listener_updates;return U32(hr);
        }
        auto native=static_cast<IDirectSound3DBuffer*>(object(a[0],Interface::buffer3d).native);HRESULT hr=E_NOTIMPL;
        switch(slot){
        case 13:hr=native->SetConeAngles(a[1],a[2],a[3]);break;
        case 14:hr=native->SetConeOrientation(real(a[1]),real(a[2]),real(a[3]),a[4]);break;
        case 15:hr=native->SetConeOutsideVolume(std::bit_cast<std::int32_t>(a[1]),a[2]);break;
        case 16:hr=native->SetMaxDistance(real(a[1]),a[2]);break;
        case 17:hr=native->SetMinDistance(real(a[1]),a[2]);break;
        case 18:hr=native->SetMode(a[1],a[2]);break;
        case 19:hr=native->SetPosition(real(a[1]),real(a[2]),real(a[3]),a[4]);break;
        case 20:hr=native->SetVelocity(real(a[1]),real(a[2]),real(a[3]),a[4]);break;
        default:stop("unmodelled 3D buffer update");
        }
        if(SUCCEEDED(hr))++buffer3d_updates;return U32(hr);
    }
    U32 vtable(Interface kind){
        if(auto it=vtables.find(kind);it!=vtables.end())return it->second;
        const unsigned count=kind==Interface::sound?11u:kind==Interface::listener?18u:21u;
        Allocation table(p,count*4);
        for(unsigned slot=0;slot<count;++slot){
            const auto name=std::string("WinRecompDS.")+std::to_string(unsigned(kind))+"."+std::to_string(slot);
            unsigned argc=1;std::function<U32(Args)> fn=[this,kind,slot](Args a)->U32{object(a[0],kind);stop("unimplemented COM method "+std::to_string(unsigned(kind))+":"+std::to_string(slot));};
            auto method=[&](unsigned n,std::function<U32(Args)> f){argc=n;fn=std::move(f);};
            if(slot==0)method(3,[this,kind](Args a){return self_query(object(a[0],kind),a);});
            else if(slot==1)method(1,[this,kind](Args a){auto& o=object(a[0],kind);if(o.guest_refs==0xffffffffu)stop("reference count overflow");auto n=o.native->AddRef();++o.guest_refs;return U32(n);});
            else if(slot==2)method(1,[this,kind](Args a){return release(object(a[0],kind));});
            if(kind==Interface::sound){
                if(slot==3)method(4,[this](Args a){return create_buffer(a);});
                if(slot==4)method(2,[this](Args a){return get_caps(a);});
                if(slot==6)method(3,[this](Args a){return set_cooperative(a);});
            } else if(kind==Interface::buffer){
                if(slot==4)method(3,[this](Args a){return buffer_position(a);});
                if(slot==9)method(2,[this](Args a){return buffer_update(Interface::buffer,9,a);});
                if(slot==12)method(4,[this](Args a){return buffer_update(Interface::buffer,12,a);});
                if(slot==13)method(2,[this](Args a){return buffer_update(Interface::buffer,13,a);});
                if(slot==14)method(2,[this](Args a){return set_buffer_format(a);});
                if(slot==15)method(2,[this](Args a){return buffer_update(Interface::buffer,15,a);});
                if(slot==16)method(2,[this](Args a){return buffer_update(Interface::buffer,16,a);});
                if(slot==17)method(2,[this](Args a){return buffer_update(Interface::buffer,17,a);});
                if(slot==18)method(1,[this](Args a){return buffer_update(Interface::buffer,18,a);});
                if(slot==20)method(1,[this](Args a){return buffer_update(Interface::buffer,20,a);});
            } else if(kind==Interface::listener){
                if(slot==11)method(3,[this](Args a){return buffer_update(Interface::listener,11,a);});
                if(slot==12)method(3,[this](Args a){return buffer_update(Interface::listener,12,a);});
                if(slot==13)method(8,[this](Args a){return buffer_update(Interface::listener,13,a);});
                if(slot==14)method(5,[this](Args a){return buffer_update(Interface::listener,14,a);});
                if(slot==15)method(3,[this](Args a){return buffer_update(Interface::listener,15,a);});
                if(slot==16)method(5,[this](Args a){return buffer_update(Interface::listener,16,a);});
                if(slot==17)method(1,[this](Args a){return buffer_update(Interface::listener,17,a);});
            } else {
                if(slot==13)method(4,[this](Args a){return buffer_update(Interface::buffer3d,13,a);});
                if(slot==14)method(5,[this](Args a){return buffer_update(Interface::buffer3d,14,a);});
                if(slot==15)method(3,[this](Args a){return buffer_update(Interface::buffer3d,15,a);});
                if(slot==16)method(3,[this](Args a){return buffer_update(Interface::buffer3d,16,a);});
                if(slot==17)method(3,[this](Args a){return buffer_update(Interface::buffer3d,17,a);});
                if(slot==18)method(3,[this](Args a){return buffer_update(Interface::buffer3d,18,a);});
                if(slot==19)method(5,[this](Args a){return buffer_update(Interface::buffer3d,19,a);});
                if(slot==20)method(5,[this](Args a){return buffer_update(Interface::buffer3d,20,a);});
            }
            p.register_api("dsound.dll",name,argc,[this,fn=std::move(fn),argc](Process& q){enter();std::array<U32,8> a{};for(unsigned i=0;i<argc;++i)a[i]=q.argument(i);return fn(std::span(a).first(argc));});
            p.memory.store(table.address+slot*4,p.resolve("dsound.dll",name),32);
        }
        p.memory.protect(table.address,4096,Memory::Read);const auto at=table.keep();vtables[kind]=at;return at;
    }
    U32 direct_sound_create(Process& q){
        enter();const U32 guid_at=q.argument(0),out=q.argument(1),outer=q.argument(2);
        q.memory.check(out,4,Memory::Write);if(outer)stop("DirectSoundCreate aggregation is unsupported");
        GUID id{};GUID* ptr=nullptr;if(guid_at){id=guid(guid_at);ptr=&id;}
        IDirectSound* result{};const auto hr=create_function()(ptr,&result,nullptr);
        if(SUCCEEDED(hr)){if(!result)stop("DirectSoundCreate succeeded without an object");q.memory.store(out,wrap(result,Interface::sound),32);++creates;}
        return U32(hr);
    }
    void install(){
        auto call=[this](Process& q){return direct_sound_create(q);};
        p.register_api("dsound.dll","#1",3,call);
        p.register_api("dsound.dll","DirectSoundCreate",3,call);
    }
public:
    explicit Sound(Process& q):p(q){install();}
    ~Sound() override{shutdown();}
    void shutdown() noexcept override{
        if(stopped)return;stopped=true;
        for(auto it=objects.rbegin();it!=objects.rend();++it){auto& o=it->second;if(o.native){for(U32 n=0;n<o.guest_refs;++n)o.native->Release();o.native=nullptr;o.guest_refs=0;}}
        objects.clear();if(module){FreeLibrary(module);module=nullptr;create=nullptr;}
    }
    std::string report() const override{
        std::ostringstream s;s<<"{\"backend\":\"native-legacy\",\"creates\":"<<creates<<",\"buffers\":"<<buffers
          <<",\"primary_buffers\":"<<primary_buffers<<",\"secondary_buffers\":"<<secondary_buffers
          <<",\"cooperative\":"<<cooperative<<",\"caps_queries\":"<<caps_queries<<",\"formats\":"<<formats
          <<",\"listeners\":"<<listeners<<",\"buffers3d\":"<<buffers3d<<",\"listener_updates\":"<<listener_updates
          <<",\"buffer3d_updates\":"<<buffer3d_updates<<",\"buffer_updates\":"<<buffer_updates<<",\"plays\":"<<plays<<"}";return s.str();
    }
};
}
std::unique_ptr<DirectSoundBackend> install_directsound(Process& p){return std::make_unique<Sound>(p);}
#endif
}
