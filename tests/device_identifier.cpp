// Real DD7 identity data compared field-by-field with the fixed x86 layout.
#include "winrecomp/process.hpp"
#include <array>
#include <cstring>
#include <iostream>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#define DIRECTDRAW_VERSION 0x0700
#include <windows.h>
#include <ddraw.h>
namespace {
unsigned checks{};
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("device identifier: " #x);}while(0)
wr::Image image(){wr::Image v;v.base=0x400000;v.entry=0x401000;v.size=0x2000;v.headers_size=512;v.bytes.resize(1024);v.sections={{".text",0x1000,512,512,512,0x60000020}};v.bytes[512]=0xc3;return v;}
bool step(wr::Cpu& c,wr::Memory& m,std::uint64_t&){if(c.eip!=0x401000)return false;c.eip=wr::pop(c,m);return true;}
wr::U32 call(wr::Process& p,wr::U32 target,std::initializer_list<wr::U32> args){auto saved=p.cpu;try{for(auto it=args.end();it!=args.begin();)wr::push(p.cpu,p.memory,*--it);wr::push(p.cpu,p.memory,saved.eip);p.cpu.eip=target;CHECK(target && p.dispatch_api());CHECK(p.cpu.r[wr::ESP]==saved.r[wr::ESP] && p.cpu.eip==saved.eip);return p.cpu.r[wr::EAX];}catch(...){p.cpu=saved;throw;}}
template<class F>void memory_fault(F f){bool caught=false;try{f();}catch(const wr::GuestFault& e){CHECK(e.kind==wr::FaultKind::memory);caught=true;}CHECK(caught);}
struct Native{IDirectDraw7* value{};~Native(){if(value)value->Release();}};
void run(){Native native;CHECK(DirectDrawCreateEx(nullptr,reinterpret_cast<void**>(&native.value),IID_IDirectDraw7,nullptr)==DD_OK);
wr::Process p(step);p.load(image());auto data=p.allocate_bytes(8192);p.memory.copy_in(data,std::span(reinterpret_cast<const std::uint8_t*>(&IID_IDirectDraw7),16));
CHECK(call(p,p.resolve("ddraw.dll","DirectDrawCreateEx"),{0,data+16,data,0})==DD_OK);auto obj=p.memory.load(data+16,32),table=p.memory.load(obj,32),target=p.memory.load(table+27*4,32);auto at=data+512;
for(auto flag:{0u,1u}){DDDEVICEIDENTIFIER2 d{};auto hr=native.value->GetDeviceIdentifier(&d,flag);CHECK(SUCCEEDED(hr));p.memory.store(at+1068,0xa1234567,32);CHECK(call(p,target,{obj,at,flag})==wr::U32(hr));CHECK(p.read_string(at,512)==d.szDriver && p.read_string(at+512,512)==d.szDescription);
CHECK(p.memory.load(at+1024,32)==d.liDriverVersion.LowPart);CHECK(p.memory.load(at+1028,32)==wr::U32(d.liDriverVersion.HighPart));CHECK(p.memory.load(at+1032,32)==d.dwVendorId);CHECK(p.memory.load(at+1036,32)==d.dwDeviceId);CHECK(p.memory.load(at+1040,32)==d.dwSubSysId);CHECK(p.memory.load(at+1044,32)==d.dwRevision);CHECK(p.memory.load(at+1064,32)==d.dwWHQLLevel);
GUID id{};p.memory.copy_out(at+1048,std::span(reinterpret_cast<std::uint8_t*>(&id),16));CHECK(IsEqualGUID(id,d.guidDeviceIdentifier));CHECK(p.memory.load(at+1068,32)==0xa1234567);
for(auto [off,text]:{std::pair<unsigned,const char*>{0,d.szDriver},{512,d.szDescription}})for(std::size_t n=std::strlen(text);n<512;++n)CHECK(p.memory.load(at+off+wr::U32(n),8)==0);
}
p.memory.protect(data+4096,4096,0);memory_fault([&]{call(p,target,{obj,data+4096-1064,0});});memory_fault([&]{call(p,target,{obj,0,0});});
p.memory.protect(data,4096,wr::Memory::Read);memory_fault([&]{call(p,target,{obj,at,0});});p.memory.protect(data,4096,wr::Memory::Read|wr::Memory::Write);
// Check native invalid-flag policy, rather than guessing its HRESULT.
DDDEVICEIDENTIFIER2 invalid{};auto hr=native.value->GetDeviceIdentifier(&invalid,0xffffffffu);std::array<std::uint8_t,1068> before{};p.memory.copy_out(at,before);CHECK(call(p,target,{obj,at,0xffffffffu})==wr::U32(hr));if(FAILED(hr)){std::array<std::uint8_t,1068> after{};p.memory.copy_out(at,after);CHECK(before==after);}
// DD7 GetCaps: compare defined fields, excluding only volatile memory totals.
const auto caps_target=p.memory.load(table+11*4,32),hw=data+2048,sw=data+2560;
for(unsigned mode=1;mode<=3;++mode){
    DDCAPS hardware{},software{};hardware.dwSize=software.dwSize=sizeof(DDCAPS);
    auto result=native.value->GetCaps((mode&1)?&hardware:nullptr,(mode&2)?&software:nullptr);CHECK(SUCCEEDED(result));
    p.memory.store(hw,380,32);p.memory.store(sw,380,32);p.memory.store(hw+380,0xbad1,32);p.memory.store(sw+380,0xbad2,32);
    CHECK(call(p,caps_target,{obj,(mode&1)?hw:0,(mode&2)?sw:0})==wr::U32(result));
    for(auto [out,expected]:{std::pair<wr::U32,DDCAPS*>{(mode&1)?hw:0,&hardware},{(mode&2)?sw:0,&software}})if(out){
        DDCAPS got{};p.memory.copy_out(out,std::span(reinterpret_cast<std::uint8_t*>(&got),380));
        // The spec calls these live snapshots, not stable driver identity data.
        got.dwVidMemTotal=got.dwVidMemFree=expected->dwVidMemTotal=expected->dwVidMemFree=0;
        expected->dwReserved1=expected->dwReserved2=expected->dwReserved3=0;
        CHECK(std::memcmp(&got,expected,380)==0);
    }
    CHECK(p.memory.load(hw+380,32)==0xbad1 && p.memory.load(sw+380,32)==0xbad2);
}
CHECK(call(p,caps_target,{obj,0,0})==wr::U32(native.value->GetCaps(nullptr,nullptr)));
p.memory.store(hw,380,32);p.memory.store(sw,380,32);
memory_fault([&]{call(p,caps_target,{obj,hw,data+4096-376});});
p.memory.protect(data,4096,wr::Memory::Read);memory_fault([&]{call(p,caps_target,{obj,hw,0});});p.memory.protect(data,4096,wr::Memory::Read|wr::Memory::Write);
for(unsigned mode=0;mode<2;++mode){bool caught=false;if(!mode)p.memory.store(hw,364,32);else p.memory.store(hw,380,32);try{call(p,caps_target,{obj,hw,mode?hw:0});}catch(const wr::GuestFault& e){CHECK(e.kind==wr::FaultKind::unsupported);caught=true;}CHECK(caught);}
call(p,p.memory.load(table+8,32),{obj});
}
}
int main(){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);try{run();std::cout<<checks<<" native device identifier and x86-boundary assertions passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
