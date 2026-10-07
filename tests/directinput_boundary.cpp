// Native Windows legacy DirectInput boundary for the E3-observed input path.
// No game bytes. Guest DIDATAFORMAT pointers/layouts are explicitly serialized.
#include "winrecomp/process.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define DIRECTINPUT_VERSION 0x0700
#include <dinput.h>
namespace {
using U32=wr::U32;
unsigned checks{};
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("DirectInput assertion: " #x);}while(0)
constexpr U32 Entry=0x401000,Wnd=0x401020;
wr::Process* current{};
wr::Image image(){wr::Image im;im.base=0x400000;im.entry=Entry;im.size=0x2000;im.headers_size=512;im.bytes.resize(1024);im.sections={{".text",0x1000,512,512,512,0x60000020}};im.bytes[512]=im.bytes[544]=0xc3;return im;}
bool step(wr::Cpu& cpu,wr::Memory& memory,std::uint64_t&){if(cpu.eip==Entry){cpu.eip=wr::pop(cpu,memory);return true;}if(cpu.eip==Wnd){cpu.eip=current->resolve("user32.dll","DefWindowProcA");return true;}return false;}
U32 call(wr::Process& p,U32 target,std::initializer_list<U32> args){auto saved=p.cpu;try{for(auto it=args.end();it!=args.begin();)wr::push(p.cpu,p.memory,*--it);wr::push(p.cpu,p.memory,saved.eip);p.cpu.eip=target;CHECK(target&&p.dispatch_api());CHECK(p.cpu.eip==saved.eip&&p.cpu.r[wr::ESP]==saved.r[wr::ESP]);auto result=p.cpu.r[wr::EAX];p.cpu=saved;return result;}catch(...){p.cpu=saved;throw;}}
U32 api(wr::Process& p,const char* dll,const char* name,std::initializer_list<U32> args){return call(p,p.resolve(dll,name),args);}
U32 com(wr::Process& p,U32 object,unsigned slot,std::initializer_list<U32> args){return call(p,p.memory.load(p.memory.load(object,32)+slot*4,32),args);}
template<class F>void unsupported(F fn){bool caught=false;try{fn();}catch(const wr::GuestFault& e){caught=true;CHECK(e.kind==wr::FaultKind::unsupported);}CHECK(caught);}
void put_guid(wr::Process& p,U32 at,const GUID& id){p.memory.copy_in(at,std::span(reinterpret_cast<const std::uint8_t*>(&id),16));}
struct GuestFormatObject {const GUID* guid;U32 offset,type,flags;};
U32 put_format(wr::Process& p,U32 data_size,std::span<const GuestFormatObject> list){
    CHECK(!list.empty() && list.size()<=512);
    const U32 count=U32(list.size()),total=24+count*16+count*16,base=p.allocate_bytes(total),objects=base+24,guids=objects+count*16;
    p.memory.store(base,24,32);p.memory.store(base+4,16,32);p.memory.store(base+8,DIDF_RELAXIS,32);p.memory.store(base+12,data_size,32);p.memory.store(base+16,count,32);p.memory.store(base+20,objects,32);
    for(U32 i=0;i<count;++i){const auto& o=list[i];const auto at=objects+i*16;p.memory.store(at,o.guid?guids+i*16:0,32);p.memory.store(at+4,o.offset,32);p.memory.store(at+8,o.type,32);p.memory.store(at+12,o.flags,32);if(o.guid)put_guid(p,guids+i*16,*o.guid);}
    return base;
}
U32 put_mouse_format(wr::Process& p){
    constexpr U32 axis=DIDFT_AXIS|DIDFT_ANYINSTANCE,button=DIDFT_BUTTON|DIDFT_ANYINSTANCE;
    const std::array<GuestFormatObject,7> objects{{
        {&GUID_XAxis,0,axis,0},{&GUID_YAxis,4,axis,0},{&GUID_ZAxis,8,axis|DIDFT_OPTIONAL,0},
        {nullptr,12,button,0},{nullptr,13,button,0},{nullptr,14,button|DIDFT_OPTIONAL,0},{nullptr,15,button|DIDFT_OPTIONAL,0}
    }};
    return put_format(p,16,objects);
}
U32 put_keyboard_format(wr::Process& p){
    std::vector<GuestFormatObject> objects;objects.reserve(256);
    for(U32 i=0;i<256;++i)objects.push_back({&GUID_Key,i,DIDFT_BUTTON|DIDFT_MAKEINSTANCE(i)|DIDFT_OPTIONAL,0});
    return put_format(p,256,objects);
}
void run(){
    wr::Process p(step);current=&p;p.load(image());const auto data=p.allocate_bytes(4096),wc=data+64,out=data+512,mouse_id=data+600,key_id=data+620;
    auto cls=p.put_string("WinRecompDirectInputBoundary");for(auto [off,value]:{std::pair<U32,U32>{0,48},{8,Wnd},{20,0x400000},{40,cls}})p.memory.store(wc+off,value,32);
    CHECK(api(p,"user32.dll","RegisterClassExA",{wc})!=0);auto window=api(p,"user32.dll","CreateWindowExA",{0,cls,cls,0x90000000,20,20,320,240,0,0,0x400000,0});CHECK(window);
    put_guid(p,mouse_id,GUID_SysMouse);put_guid(p,key_id,GUID_SysKeyboard);
    unsupported([&]{api(p,"dinput.dll","DirectInputCreateA",{0x400000,0x0800,out,0});});
    unsupported([&]{api(p,"dinput.dll","DirectInputCreateA",{0x400000,0x0300,out,1});});
    CHECK(api(p,"dinput.dll","DirectInputCreateA",{0x400000,0x0300,out,0})==DI_OK);const auto input=p.memory.load(out,32);CHECK(input);
    GUID fake{0x12345678,1,2,{3,4,5,6,7,8,9,10}};put_guid(p,data+700,fake);unsupported([&]{com(p,input,3,{input,data+700,out,0});});
    CHECK(com(p,input,3,{input,mouse_id,out,0})==DI_OK);const auto mouse=p.memory.load(out,32);CHECK(mouse);
    CHECK(com(p,input,3,{input,key_id,out,0})==DI_OK);const auto keyboard=p.memory.load(out,32);CHECK(keyboard&&keyboard!=mouse);
    CHECK(com(p,mouse,13,{mouse,window,DISCL_NONEXCLUSIVE|DISCL_FOREGROUND})==DI_OK);
    CHECK(com(p,keyboard,13,{keyboard,window,DISCL_NONEXCLUSIVE|DISCL_FOREGROUND})==DI_OK);
    unsupported([&]{com(p,mouse,13,{mouse,window,DISCL_EXCLUSIVE|DISCL_FOREGROUND});});
    const auto mouse_format=put_mouse_format(p),key_format=put_keyboard_format(p);
    CHECK(com(p,mouse,11,{mouse,mouse_format})==DI_OK);CHECK(com(p,keyboard,11,{keyboard,key_format})==DI_OK);
    p.memory.store(mouse_format,20,32);unsupported([&]{com(p,mouse,11,{mouse,mouse_format});});p.memory.store(mouse_format,24,32);
    const auto prop=data+768;p.memory.store(prop,20,32);p.memory.store(prop+4,16,32);p.memory.store(prop+8,8,32);p.memory.store(prop+12,DIPH_BYOFFSET,32);p.memory.store(prop+16,0,32);
    CHECK(com(p,mouse,5,{mouse,3,prop})==DI_OK);CHECK(p.memory.load(prop+16,32)>0);
    unsupported([&]{com(p,mouse,5,{mouse,4,prop});});
    auto native_window=reinterpret_cast<HWND>(p.gui()->native_window(window));ShowWindow(native_window,SW_SHOW);SetForegroundWindow(native_window);SetFocus(native_window);
    const auto mouse_acquire=HRESULT(com(p,mouse,7,{mouse})),key_acquire=HRESULT(com(p,keyboard,7,{keyboard}));
    CHECK(SUCCEEDED(mouse_acquire)||mouse_acquire==DIERR_OTHERAPPHASPRIO);CHECK(SUCCEEDED(key_acquire)||key_acquire==DIERR_OTHERAPPHASPRIO);
    const auto mouse_state=data+1024,key_state=data+1280;for(U32 i=0;i<16;++i)p.memory.store(mouse_state+i,0xa5,8);for(U32 i=0;i<256;++i)p.memory.store(key_state+i,0xa5,8);
    auto mstate=HRESULT(com(p,mouse,9,{mouse,16,mouse_state}));auto kstate=HRESULT(com(p,keyboard,9,{keyboard,256,key_state}));
    CHECK(SUCCEEDED(mstate)||mstate==DIERR_NOTACQUIRED||mstate==DIERR_INPUTLOST);CHECK(SUCCEEDED(kstate)||kstate==DIERR_NOTACQUIRED||kstate==DIERR_INPUTLOST);
    unsupported([&]{com(p,mouse,9,{mouse,15,mouse_state});});
    CHECK(SUCCEEDED(HRESULT(com(p,mouse,8,{mouse}))));CHECK(SUCCEEDED(HRESULT(com(p,keyboard,8,{keyboard}))));
    const auto report=p.report();CHECK(report.find("\"directinput\":{\"backend\":\"native-legacy\"")!=std::string::npos);CHECK(report.find("\"devices\":2")!=std::string::npos);CHECK(report.find("\"formats\":2")!=std::string::npos);
    com(p,mouse,2,{mouse});com(p,keyboard,2,{keyboard});com(p,input,2,{input});CHECK(api(p,"user32.dll","DestroyWindow",{window})!=0);CHECK(api(p,"user32.dll","UnregisterClassA",{cls,0x400000})!=0);
}
}
int main(){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);try{run();std::cout<<checks<<" legacy DirectInput boundary assertions passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
