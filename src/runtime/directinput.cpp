#include "winrecomp/directinput.hpp"
#include "winrecomp/process.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define DIRECTINPUT_VERSION 0x0700
#include <dinput.h>
#endif
namespace wr {
#ifndef _WIN32
namespace {
class UnavailableInput final:public DirectInputBackend {
public:
    void shutdown() noexcept override{}
    std::string report() const override{return "{\"backend\":\"unavailable\"}";}
};
}
std::unique_ptr<DirectInputBackend> install_directinput(Process&){return std::make_unique<UnavailableInput>();}
#else
namespace {
using Args=std::span<const U32>;
enum class Interface {input,device};
constexpr U32 MaxObjects=8,MaxFormatObjects=512,MaxStateBytes=64u*1024;
struct Allocation {
    Process& p;U32 address{};
    Allocation(Process& q,std::size_t n):p(q),address(q.allocate_bytes(n)){}
    ~Allocation(){if(address)try{p.memory.release(address);}catch(...){}}
    U32 keep(){return std::exchange(address,0u);}
};
struct FormatStorage {
    DIDATAFORMAT format{};
    std::vector<GUID> guids;
    std::vector<DIOBJECTDATAFORMAT> objects;
};
struct Object {
    Interface kind{};IUnknown* native{};U32 guest_refs{};
    std::shared_ptr<FormatStorage> format;
};
class Input final:public DirectInputBackend {
    Process& p;DWORD thread{GetCurrentThreadId()};bool stopped{};
    std::map<U32,Object> objects;std::map<Interface,U32> vtables;
    std::uint64_t creates{},devices{},formats{},cooperative{},acquires{},unacquires{},states{},properties{};
    using CreateFn=HRESULT (WINAPI*)(HINSTANCE,DWORD,LPDIRECTINPUTA*,LPUNKNOWN);
    HMODULE module{};CreateFn create{};
    [[noreturn]]void stop(const std::string& why){throw GuestFault(FaultKind::unsupported,p.cpu.eip,"DirectInput: "+why);}
    void enter(){if(stopped || GetCurrentThreadId()!=thread)stop("foreign-thread or retired backend");}
    CreateFn create_function(){
        if(create)return create;
        module=LoadLibraryW(L"dinput.dll");
        if(!module)stop("native dinput.dll is unavailable");
        create=reinterpret_cast<CreateFn>(GetProcAddress(module,"DirectInputCreateA"));
        if(!create){FreeLibrary(module);module=nullptr;stop("native dinput.dll lacks DirectInputCreateA");}
        return create;
    }
    GUID guid(U32 at){GUID value{};p.memory.copy_out(at,std::span(reinterpret_cast<std::uint8_t*>(&value),16));return value;}
    Object& object(U32 at,Interface kind){auto it=objects.find(at);if(it==objects.end() || !it->second.native || !it->second.guest_refs || it->second.kind!=kind)stop("stale or wrong-interface guest COM object");return it->second;}
    U32 vtable(Interface kind){
        if(auto it=vtables.find(kind);it!=vtables.end())return it->second;
        const unsigned count=kind==Interface::input?8u:18u;Allocation table(p,count*4);
        for(unsigned slot=0;slot<count;++slot){
            const auto name=std::string("WinRecompDI.")+std::to_string(unsigned(kind))+"."+std::to_string(slot);
            unsigned argc=1;std::function<U32(Args)> fn=[this,kind,slot](Args a)->U32{object(a[0],kind);stop("unimplemented COM method "+std::to_string(unsigned(kind))+":"+std::to_string(slot));};
            auto method=[&](unsigned n,std::function<U32(Args)> f){argc=n;fn=std::move(f);};
            if(slot==0)method(3,[this,kind](Args a){return query(object(a[0],kind),a);});
            else if(slot==1)method(1,[this,kind](Args a){auto& o=object(a[0],kind);if(o.guest_refs==0xffffffffu)stop("reference count overflow");auto n=o.native->AddRef();++o.guest_refs;return U32(n);});
            else if(slot==2)method(1,[this,kind](Args a){return release(object(a[0],kind));});
            if(kind==Interface::input && slot==3)method(4,[this](Args a){return create_device(a);});
            if(kind==Interface::device){
                if(slot==5)method(3,[this](Args a){return get_property(a);});
                if(slot==7)method(1,[this](Args a){auto hr=static_cast<IDirectInputDeviceA*>(object(a[0],Interface::device).native)->Acquire();++acquires;return U32(hr);});
                if(slot==8)method(1,[this](Args a){auto hr=static_cast<IDirectInputDeviceA*>(object(a[0],Interface::device).native)->Unacquire();++unacquires;return U32(hr);});
                if(slot==9)method(3,[this](Args a){return get_state(a);});
                if(slot==11)method(2,[this](Args a){return set_format(a);});
                if(slot==13)method(3,[this](Args a){return set_cooperative(a);});
            }
            p.register_api("dinput.dll",name,argc,[this,fn=std::move(fn),argc](Process& q){enter();std::array<U32,4> a{};for(unsigned i=0;i<argc;++i)a[i]=q.argument(i);return fn(std::span(a).first(argc));});
            p.memory.store(table.address+slot*4,p.resolve("dinput.dll",name),32);
        }
        p.memory.protect(table.address,4096,Memory::Read);auto at=table.keep();vtables[kind]=at;return at;
    }
    U32 wrap(IUnknown* native,Interface kind){
        if(!native)stop("attempted to wrap null native object");
        for(auto& [at,o]:objects)if(o.native==native && o.kind==kind){if(o.guest_refs==0xffffffffu)stop("reference count overflow");++o.guest_refs;return at;}
        if(objects.size()>=MaxObjects)stop("COM object budget exhausted");
        Allocation memory(p,4);p.memory.store(memory.address,vtable(kind),32);p.memory.protect(memory.address,4096,Memory::Read);
        auto at=memory.keep();objects.emplace(at,Object{kind,native,1,{}});return at;
    }
    U32 query(Object& o,Args a){
        p.memory.check(a[1],16,Memory::Read);p.memory.check(a[2],4,Memory::Write);const auto id=guid(a[1]);
        const bool ok=IsEqualIID(id,IID_IUnknown) || (o.kind==Interface::input?IsEqualIID(id,IID_IDirectInputA):IsEqualIID(id,IID_IDirectInputDeviceA));
        if(!ok){p.memory.store(a[2],0,32);return U32(E_NOINTERFACE);}
        if(o.guest_refs==0xffffffffu)stop("reference count overflow");o.native->AddRef();++o.guest_refs;p.memory.store(a[2],a[0],32);return U32(S_OK);
    }
    U32 release(Object& o){const auto native=o.native;const auto count=native->Release();if(!--o.guest_refs){o.native=nullptr;o.format.reset();}return U32(count);}
    U32 create_device(Args a){
        auto native=static_cast<IDirectInputA*>(object(a[0],Interface::input).native);p.memory.check(a[1],16,Memory::Read);p.memory.check(a[2],4,Memory::Write);if(a[3])stop("CreateDevice aggregation is unsupported");
        const auto id=guid(a[1]);if(!IsEqualGUID(id,GUID_SysMouse) && !IsEqualGUID(id,GUID_SysKeyboard))stop("only system mouse and keyboard are in the E3 profile");
        IDirectInputDeviceA* result{};const auto hr=native->CreateDevice(id,&result,nullptr);if(SUCCEEDED(hr)){if(!result)stop("CreateDevice succeeded without an object");p.memory.store(a[2],wrap(result,Interface::device),32);++devices;}return U32(hr);
    }
    std::shared_ptr<FormatStorage> translate_format(U32 at){
        constexpr U32 GuestFormat=24,GuestObject=16;p.memory.check(at,GuestFormat,Memory::Read);
        const auto size=p.memory.load(at,32),obj_size=p.memory.load(at+4,32),flags=p.memory.load(at+8,32),data_size=p.memory.load(at+12,32),count=p.memory.load(at+16,32),objects_at=p.memory.load(at+20,32);
        if(size!=GuestFormat || obj_size!=GuestObject)stop("SetDataFormat requires 24/16-byte x86 layouts");
        if(flags!=DIDF_RELAXIS)stop("SetDataFormat flags outside observed relative-axis E3 profile");
        if(!data_size || data_size>MaxStateBytes || !count || count>MaxFormatObjects || !objects_at)stop("SetDataFormat bounds outside profile");
        const auto bytes=std::uint64_t(count)*GuestObject;if(std::uint64_t(objects_at)+bytes>0x100000000ull)stop("SetDataFormat object array wraps guest address space");p.memory.check(objects_at,std::size_t(bytes),Memory::Read);
        auto out=std::make_shared<FormatStorage>();out->guids.resize(count);out->objects.resize(count);
        for(U32 i=0;i<count;++i){const auto obj=objects_at+i*GuestObject,pguid=p.memory.load(obj,32),ofs=p.memory.load(obj+4,32),type=p.memory.load(obj+8,32),object_flags=p.memory.load(obj+12,32);
            if(ofs>=data_size || object_flags)stop("SetDataFormat object outside observed E3 profile");
            auto& native=out->objects[i];native.dwOfs=ofs;native.dwType=type;native.dwFlags=0;
            if(pguid){out->guids[i]=guid(pguid);native.pguid=&out->guids[i];}else native.pguid=nullptr;
        }
        out->format.dwSize=sizeof(DIDATAFORMAT);out->format.dwObjSize=sizeof(DIOBJECTDATAFORMAT);out->format.dwFlags=flags;out->format.dwDataSize=data_size;out->format.dwNumObjs=count;out->format.rgodf=out->objects.data();return out;
    }
    U32 set_format(Args a){auto& o=object(a[0],Interface::device);auto pending=translate_format(a[1]);const auto hr=static_cast<IDirectInputDeviceA*>(o.native)->SetDataFormat(&pending->format);if(SUCCEEDED(hr)){o.format=std::move(pending);++formats;}return U32(hr);}
    U32 set_cooperative(Args a){
        auto native=static_cast<IDirectInputDeviceA*>(object(a[0],Interface::device).native);if(a[2]!=(DISCL_NONEXCLUSIVE|DISCL_FOREGROUND))stop("SetCooperativeLevel flags outside observed E3 profile");if(!a[1])stop("SetCooperativeLevel requires a guest window");
        if(!p.gui())stop("SetCooperativeLevel requires GUI backend");std::uintptr_t host{};try{host=p.gui()->native_window(a[1]);}catch(const GuestFault&){throw;}catch(const std::exception& e){stop(std::string("guest window lookup: ")+e.what());}
        if(!host)stop("guest window has no native HWND");const auto hr=native->SetCooperativeLevel(reinterpret_cast<HWND>(host),a[2]);if(SUCCEEDED(hr))++cooperative;return U32(hr);
    }
    U32 get_property(Args a){
        auto& o=object(a[0],Interface::device);if(a[1]!=3)stop("GetProperty supports only DIPROP_GRANULARITY in observed E3 profile");if(!o.format)stop("GetProperty before SetDataFormat");
        constexpr U32 GuestSize=20;p.memory.check(a[2],GuestSize,Memory::Read|Memory::Write);if(p.memory.load(a[2],32)!=GuestSize || p.memory.load(a[2]+4,32)!=16)stop("GetProperty requires 20-byte DIPROPDWORD/16-byte header");
        DIPROPDWORD prop{};prop.diph.dwSize=sizeof(prop);prop.diph.dwHeaderSize=sizeof(DIPROPHEADER);prop.diph.dwObj=p.memory.load(a[2]+8,32);prop.diph.dwHow=p.memory.load(a[2]+12,32);prop.dwData=p.memory.load(a[2]+16,32);
        if(prop.diph.dwHow!=DIPH_BYOFFSET || prop.diph.dwObj>=o.format->format.dwDataSize)stop("GetProperty object selector outside observed E3 profile");
        const auto hr=static_cast<IDirectInputDeviceA*>(o.native)->GetProperty(DIPROP_GRANULARITY,&prop.diph);if(SUCCEEDED(hr)){p.memory.store(a[2]+16,prop.dwData,32);++properties;}return U32(hr);
    }
    U32 get_state(Args a){
        auto& o=object(a[0],Interface::device);if(!o.format)stop("GetDeviceState before SetDataFormat");if(a[1]!=o.format->format.dwDataSize || !a[1] || a[1]>MaxStateBytes)stop("GetDeviceState size differs from installed data format");
        p.memory.check(a[2],a[1],Memory::Write);std::vector<std::uint8_t> data(a[1]);const auto hr=static_cast<IDirectInputDeviceA*>(o.native)->GetDeviceState(a[1],data.data());if(SUCCEEDED(hr)){p.memory.copy_in(a[2],data);++states;}return U32(hr);
    }
    void install(){
        p.register_api("dinput.dll","DirectInputCreateA",4,[this](Process& q){enter();const auto hinst=q.argument(0),version=q.argument(1),output=q.argument(2),outer=q.argument(3);q.memory.check(output,4,Memory::Write);if(outer)stop("DirectInputCreateA aggregation is unsupported");if(version!=0x0300)stop("DirectInputCreateA version outside observed E3 profile");if(hinst && hinst!=q.image_base)stop("DirectInputCreateA HINSTANCE is not the guest image base");
            IDirectInputA* result{};const auto hr=create_function()(GetModuleHandleW(nullptr),version,&result,nullptr);if(SUCCEEDED(hr)){if(!result)stop("DirectInputCreateA succeeded without an object");q.memory.store(output,wrap(result,Interface::input),32);++creates;}return U32(hr);});
    }
public:
    explicit Input(Process& q):p(q){install();}
    ~Input() override{shutdown();}
    void shutdown() noexcept override{if(stopped)return;stopped=true;for(auto& [at,o]:objects){(void)at;if(o.native){for(U32 n=0;n<o.guest_refs;++n)o.native->Release();o.native=nullptr;o.guest_refs=0;o.format.reset();}}objects.clear();if(module){FreeLibrary(module);module=nullptr;create=nullptr;}}
    std::string report() const override{std::ostringstream out;out<<"{\"backend\":\"native-legacy\",\"creates\":"<<creates<<",\"devices\":"<<devices<<",\"formats\":"<<formats<<",\"cooperative\":"<<cooperative<<",\"acquires\":"<<acquires<<",\"unacquires\":"<<unacquires<<",\"states\":"<<states<<",\"properties\":"<<properties<<"}";return out.str();}
};
}
std::unique_ptr<DirectInputBackend> install_directinput(Process& p){return std::make_unique<Input>(p);}
#endif
}
