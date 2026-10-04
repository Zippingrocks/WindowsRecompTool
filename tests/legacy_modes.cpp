// Legacy DirectDraw mode enumeration: real native API versus the guest ABI.
// This fixture uses no E3 data. Compiled-x86 coverage is in directdraw_program.py.
#include "winrecomp/process.hpp"
#include <array>
#include <iostream>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#define DIRECTDRAW_VERSION 0x0700
#include <windows.h>
#include <ddraw.h>
namespace {
unsigned checks{};
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("legacy mode assertion: " #x);}while(0)
constexpr wr::U32 Entry=0x401000,Callback=0x401010,Context=0xe1357246;
using Row=std::array<wr::U32,7>;
std::vector<Row> guest_rows;
wr::U32 expired{},reply=DDENUMRET_OK;
bool write_callback{},throw_callback{},exit_callback{};
wr::Process* current{};
wr::Image image(){wr::Image im;im.base=0x400000;im.entry=Entry;im.size=0x3000;im.headers_size=512;im.bytes.resize(1536);im.sections={{".text",0x1000,512,512,512,0x60000020},{".data",0x2000,512,1024,512,0xc0000040}};im.bytes[512]=im.bytes[528]=0xc3;return im;}
bool step(wr::Cpu& cpu,wr::Memory& m,std::uint64_t&){
    if(cpu.eip==Entry){cpu.eip=wr::pop(cpu,m);return true;}
    if(cpu.eip!=Callback)return false;
    const auto at=m.load(cpu.r[wr::ESP]+4,32);expired=at;
    CHECK(m.load(cpu.r[wr::ESP]+8,32)==Context);
    CHECK(m.load(at,32)==108);CHECK(m.load(at+36,32)==0);
    const auto flags=m.load(at+4,32);
    CHECK((flags&(DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT))==(DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT));
    CHECK(m.load(at+72,32)==32);
    guest_rows.push_back({m.load(at+12,32),m.load(at+8,32),m.load(at+84,32),m.load(at+88,32),m.load(at+92,32),m.load(at+96,32),(flags&DDSD_REFRESHRATE)?m.load(at+24,32):0});
    if(throw_callback)throw wr::GuestFault(wr::FaultKind::unsupported,Callback,"mode callback test fault");
    if(write_callback)m.store(at,0,32);
    if(exit_callback)current->exit(17);
    cpu.r[wr::EAX]=reply;cpu.eip=wr::pop(cpu,m);cpu.r[wr::ESP]+=8;return true;
}
struct NativeRows{std::vector<Row> rows;bool cancel{};};
HRESULT CALLBACK original(DDSURFACEDESC* d,void* state){
    auto& result=*static_cast<NativeRows*>(state);
    result.rows.push_back({d->dwWidth,d->dwHeight,d->ddpfPixelFormat.dwRGBBitCount,d->ddpfPixelFormat.dwRBitMask,d->ddpfPixelFormat.dwGBitMask,d->ddpfPixelFormat.dwBBitMask,(d->dwFlags&DDSD_REFRESHRATE)?d->dwRefreshRate:0});
    return result.cancel?DDENUMRET_CANCEL:DDENUMRET_OK;
}
wr::U32 invoke(wr::Process& p,wr::U32 target,std::initializer_list<wr::U32> args){
    const auto saved=p.cpu;
    try{
        for(auto it=args.end();it!=args.begin();)wr::push(p.cpu,p.memory,*--it);
        wr::push(p.cpu,p.memory,saved.eip);p.cpu.eip=target;CHECK(target && p.dispatch_api());
        CHECK(p.cpu.r[wr::ESP]==saved.r[wr::ESP] && p.cpu.eip==saved.eip);
        for(auto reg:{wr::EBX,wr::ESI,wr::EDI,wr::EBP})CHECK(p.cpu.r[reg]==saved.r[reg]);
        return p.cpu.r[wr::EAX];
    }catch(...){p.cpu=saved;throw;} // fixture recovery only; production stops
}
wr::U32 method(wr::Process& p,wr::U32 object,unsigned slot,std::initializer_list<wr::U32> args){return invoke(p,p.memory.load(p.memory.load(object,32)+4*slot,32),args);}
template<class F>void fault(F fn,wr::FaultKind kind){bool caught=false;try{fn();}catch(const wr::GuestFault& e){CHECK(e.kind==kind);caught=true;}CHECK(caught);}
struct Owned{IDirectDraw* value{};~Owned(){if(value)value->Release();}};
void run(){
    Owned native;CHECK(DirectDrawCreate(nullptr,&native.value,nullptr)==DD_OK);
    wr::Process p(step);current=&p;p.load(image());const auto data=p.allocate_bytes(8192);
    CHECK(invoke(p,p.resolve("ddraw.dll","DirectDrawCreate"),{0,data,0})==DD_OK);
    const auto object=p.memory.load(data,32),filter=data+256;
    auto enumerate=[&](wr::U32 at=0,wr::U32 flags=0){return method(p,object,8,{object,flags,at,Context,Callback});};
    NativeRows expected;CHECK(native.value->EnumDisplayModes(0,nullptr,&expected,original)==DD_OK);
    CHECK(!expected.rows.empty());const auto full_count=expected.rows.size();guest_rows.clear();CHECK(enumerate()==DD_OK);CHECK(guest_rows==expected.rows);
    fault([&]{p.memory.load(expired,32);},wr::FaultKind::memory);
    expected.rows.clear();expected.cancel=true;CHECK(native.value->EnumDisplayModes(0,nullptr,&expected,original)==DD_OK);
    // Measure the native callback sequence instead of assuming Wine's count.
    // The guest still returns CANCEL every time; all returned rows must match.
    std::cout<<"Native mode callbacks: continue="<<full_count<<", cancel="<<expected.rows.size()<<std::endl;
    CHECK(!expected.rows.empty() && expected.rows.size()<=full_count);
    const auto cancelled_rows=expected.rows;
    guest_rows.clear();reply=DDENUMRET_CANCEL;CHECK(enumerate()==DD_OK);CHECK(guest_rows==cancelled_rows);
    reply=DDENUMRET_OK;expected.cancel=false;
    // Same native/guest width filter. The guest input is a 108-byte structure
    // against a read-only page; no native callback may write back to it.
    DDSURFACEDESC d{};d.dwSize=sizeof(d);d.dwFlags=DDSD_WIDTH;d.dwWidth=expected.rows[0][0];
    p.memory.store(filter,108,32);p.memory.store(filter+4,DDSD_WIDTH,32);p.memory.store(filter+12,d.dwWidth,32);
    p.memory.protect(data,4096,wr::Memory::Read);
    expected.rows.clear();CHECK(native.value->EnumDisplayModes(0,&d,&expected,original)==DD_OK);
    guest_rows.clear();CHECK(enumerate(filter)==DD_OK);CHECK(guest_rows==expected.rows);CHECK(p.memory.load(filter+12,32)==d.dwWidth);
    p.memory.protect(data,4096,wr::Memory::Read|wr::Memory::Write);
    // A callback fault must unwind outside the native COM boundary and release
    // its temporary guest payload, without converting the failure into DD_OK.
    for(int mode=0;mode<3;++mode){
        write_callback=mode==0;throw_callback=mode==1;reply=mode==2?2:DDENUMRET_OK;
        fault([&]{enumerate();},mode==0?wr::FaultKind::memory:wr::FaultKind::unsupported);
        fault([&]{p.memory.load(expired,32);},wr::FaultKind::memory);
    }
    write_callback=throw_callback=false;reply=DDENUMRET_CANCEL;
    fault([&]{method(p,object,8,{object,0,0,Context,0});},wr::FaultKind::memory);
    p.memory.store(filter,124,32);fault([&]{enumerate(filter);},wr::FaultKind::unsupported);
    p.memory.store(filter,108,32);p.memory.store(filter+4,DDSD_LPSURFACE,32);fault([&]{enumerate(filter);},wr::FaultKind::unsupported);
    p.memory.store(filter+4,0,32);p.memory.store(filter+36,0x12345678,32);fault([&]{enumerate(filter);},wr::FaultKind::unsupported);
    p.memory.store(filter+36,0,32);p.memory.store(filter+4,DDSD_PIXELFORMAT,32);p.memory.store(filter+72,31,32);fault([&]{enumerate(filter);},wr::FaultKind::unsupported);
    p.memory.protect(data+4096,4096,wr::Memory::Read);
    fault([&]{enumerate(data+8192-104);},wr::FaultKind::memory);
    guest_rows.clear();CHECK(enumerate()==DD_OK);CHECK(guest_rows==cancelled_rows);
    method(p,object,2,{object});fault([&]{enumerate();},wr::FaultKind::unsupported);
    // Guest process exit cancels enumeration instead of invoking more callbacks.
    wr::Process exiting(step);current=&exiting;exiting.load(image());auto out=exiting.allocate_bytes(4);
    CHECK(invoke(exiting,exiting.resolve("ddraw.dll","DirectDrawCreate"),{0,out,0})==DD_OK);auto obj=exiting.memory.load(out,32);
    reply=DDENUMRET_OK;exit_callback=true;guest_rows.clear();CHECK(method(exiting,obj,8,{obj,0,0,Context,Callback})==DD_OK);
    CHECK(exiting.exited() && guest_rows.size()==1);fault([&]{exiting.memory.load(expired,32);},wr::FaultKind::memory);exit_callback=false;
}
}
int main(){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);try{run();std::cout<<checks<<" legacy DirectDraw enumeration assertions passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
