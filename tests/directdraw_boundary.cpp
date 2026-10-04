// Native DirectDraw boundary failures; end-to-end compiled x86 coverage lives
// in directdraw_program.py. No game bytes and no replacement driver.
#include "winrecomp/process.hpp"
#include <array>
#include <iostream>
#include <thread>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define DIRECTDRAW_VERSION 0x0700
#include <windows.h>
#include <ddraw.h>
namespace {
unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x))throw std::runtime_error("DirectDraw assertion: " #x);} while(0)
constexpr wr::U32 Entry=0x401000,Callback=0x401010,Context=0x12345678;
wr::U32 saved_description{};unsigned callbacks{};bool throw_callback{};
wr::Image image(){wr::Image im;im.base=0x400000;im.entry=Entry;im.size=0x3000;im.headers_size=512;im.bytes.resize(1536);im.sections={{".text",0x1000,512,512,512,0x60000020},{".data",0x2000,512,1024,512,0xc0000040}};im.bytes[512]=im.bytes[528]=0xc3;return im;}
bool step(wr::Cpu& cpu,wr::Memory& memory,std::uint64_t&){
    if(cpu.eip==Entry){cpu.eip=wr::pop(cpu,memory);return true;}
    if(cpu.eip!=Callback)return false;
    if(throw_callback)throw wr::GuestFault(wr::FaultKind::unsupported,Callback,"deliberate adapter callback failure");
    CHECK(memory.load(cpu.r[wr::ESP]+16,32)==Context);
    saved_description=memory.load(cpu.r[wr::ESP]+8,32);CHECK(saved_description && memory.load(saved_description,8));
    ++callbacks;cpu.r[wr::EAX]=0;cpu.eip=wr::pop(cpu,memory);cpu.r[wr::ESP]+=20;return true;
}
wr::U32 call(wr::Process& p,wr::U32 target,std::initializer_list<wr::U32> args){
    auto saved=p.cpu;
    try{for(auto it=args.end();it!=args.begin();)wr::push(p.cpu,p.memory,*--it);wr::push(p.cpu,p.memory,saved.eip);p.cpu.eip=target;CHECK(target && p.dispatch_api());CHECK(p.cpu.eip==saved.eip && p.cpu.r[wr::ESP]==saved.r[wr::ESP]);for(auto r:{wr::EBX,wr::EBP,wr::ESI,wr::EDI})CHECK(p.cpu.r[r]==saved.r[r]);return p.cpu.r[wr::EAX];}
    catch(...){p.cpu=saved;throw;}
}
wr::U32 api(wr::Process& p,const char* name,std::initializer_list<wr::U32> args){return call(p,p.resolve("ddraw.dll",name),args);}
wr::U32 method(wr::Process& p,wr::U32 obj,unsigned slot,std::initializer_list<wr::U32> args){return call(p,p.memory.load(p.memory.load(obj,32)+slot*4,32),args);}
template<class F>void fault(F fn,wr::FaultKind kind){bool caught=false;try{fn();}catch(const wr::GuestFault& e){caught=true;CHECK(e.kind==kind);}CHECK(caught);}
void put_guid(wr::Process& p,wr::U32 at,const GUID& value){p.memory.copy_in(at,std::span(reinterpret_cast<const std::uint8_t*>(&value),16));}
void description(wr::Process& p,wr::U32 at){
    std::array<std::uint8_t,124> zero{};p.memory.copy_in(at,zero);
    for(auto [offset,value]:std::initializer_list<std::pair<unsigned,wr::U32>>{{0,124},{4,0x1007},{8,3},{12,7},{72,32},{76,DDPF_RGB},{84,32},{88,0xff0000},{92,0xff00},{96,0xff},{104,DDSCAPS_OFFSCREENPLAIN|DDSCAPS_SYSTEMMEMORY}})p.memory.store(at+offset,value,32);
}
void basic(){
    wr::Process p(step);p.load(image());const auto scratch=p.allocate_bytes(8192);p.memory.protect(scratch+4096,4096,0);
    const auto id=scratch,output=scratch+4092,desc=scratch+2048,fx=scratch+64;
    put_guid(p,id,IID_IDirectDraw7);
    callbacks=0;CHECK(api(p,"DirectDrawEnumerateExA",{Callback,Context,0})==DD_OK && callbacks==1);
    fault([&]{p.memory.load(saved_description,8);},wr::FaultKind::memory);
    throw_callback=true;fault([&]{api(p,"DirectDrawEnumerateExA",{Callback,Context,0});},wr::FaultKind::unsupported);throw_callback=false;
    fault([&]{api(p,"DirectDrawEnumerateExA",{scratch,Context,0});},wr::FaultKind::memory);
    fault([&]{api(p,"DirectDrawCreateEx",{2,output+1,id,0});},wr::FaultKind::memory);
    CHECK(api(p,"DirectDrawCreateEx",{2,output,id,0})==DD_OK);const auto draw=p.memory.load(output,32);CHECK(draw);
    fault([&]{p.memory.store(draw,0,32);},wr::FaultKind::memory);
    fault([&]{p.memory.store(p.memory.load(draw,32),0,32);},wr::FaultKind::memory);
    fault([&]{method(p,draw,20,{draw,0,DDSCL_FULLSCREEN|DDSCL_EXCLUSIVE});},wr::FaultKind::unsupported);
    CHECK(method(p,draw,20,{draw,0,DDSCL_NORMAL})==DD_OK);
    bool rejected=false;std::thread foreign([&]{try{api(p,"DirectDrawCreateEx",{2,output,id,0});}catch(const wr::GuestFault& e){rejected=e.kind==wr::FaultKind::unsupported;}});foreign.join();CHECK(rejected);
    description(p,desc);p.memory.store(desc+124,0xa5a5a5a5,32);
    CHECK(method(p,draw,6,{draw,desc,output,0})==DD_OK);auto surface=p.memory.load(output,32);CHECK(surface);
    // Only the exact interface-specific this pointer may enter a method thunk.
    const auto lock_target=p.memory.load(p.memory.load(surface,32)+25*4,32);
    fault([&]{call(p,lock_target,{draw,0,desc,1,0});},wr::FaultKind::unsupported);
    p.memory.store(fx,100,32);p.memory.store(fx+80,0x0013579b,32);
    CHECK(method(p,surface,5,{surface,0,0,0,DDBLT_COLORFILL|DDBLT_WAIT,fx})==DD_OK);
    fault([&]{method(p,surface,25,{surface,0,scratch+4096-120,1,0});},wr::FaultKind::memory);
    fault([&]{method(p,surface,25,{surface,0,desc,DDLOCK_EVENT,0});},wr::FaultKind::unsupported);
    CHECK(method(p,surface,25,{surface,0,desc,DDLOCK_WAIT,0})==DD_OK);auto pointer=p.memory.load(desc+36,32);CHECK(pointer);
    CHECK((p.memory.load(pointer,32)&0xffffff)==0x13579b);CHECK(p.memory.load(desc+124,32)==0xa5a5a5a5);
    fault([&]{method(p,surface,2,{surface});},wr::FaultKind::unsupported);
    fault([&]{method(p,surface,25,{surface,0,desc,1,0});},wr::FaultKind::unsupported);
    p.memory.store(pointer,0x002468ac,32);
    CHECK(method(p,surface,32,{surface,0})==DD_OK);fault([&]{p.memory.load(pointer,32);},wr::FaultKind::memory);
    CHECK(method(p,surface,25,{surface,0,desc,DDLOCK_WAIT|DDLOCK_READONLY,0})==DD_OK);pointer=p.memory.load(desc+36,32);CHECK((p.memory.load(pointer,32)&0xffffff)==0x2468ac);
    fault([&]{p.memory.store(pointer,0,32);},wr::FaultKind::memory);
    CHECK(method(p,surface,32,{surface,0})==DD_OK);fault([&]{p.memory.load(pointer,32);},wr::FaultKind::memory);
    method(p,surface,2,{surface});fault([&]{method(p,surface,1,{surface});},wr::FaultKind::unsupported);
    method(p,draw,2,{draw});fault([&]{method(p,draw,1,{draw});},wr::FaultKind::unsupported);
}
}
int main(){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);try{basic();std::cout<<checks<<" native DirectDraw boundary assertions passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
