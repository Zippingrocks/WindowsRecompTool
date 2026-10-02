#include "winrecomp/integer.hpp"
#include <iostream>
#include <thread>
#define CHECK(x) do {if(!(x))throw std::runtime_error("check failed: " #x);}while(0)
template<class F> void fault(wr::FaultKind kind,F&& fn){bool caught=false;try{fn();}catch(const wr::GuestFault& e){CHECK(e.kind==kind);caught=true;}CHECK(caught);}
int main(){try{
    using namespace wr;
    Memory m;m.reserve(0x10000,0x3000);
    fault(FaultKind::memory,[&]{m.load(0x10000,8);});
    m.commit(0x10000,0x2000,Memory::Read|Memory::Write);CHECK(m.load(0x10000,32)==0);
    m.store(0x10000,0x12345678,32);m.commit(0x10000,0x1000,Memory::Read);CHECK(m.load(0x10000,32)==0x12345678);CHECK(m.permissions(0x10000)&Memory::Write);
    m.protect(0x11000,0x1000,Memory::Read);fault(FaultKind::memory,[&]{m.store(0x10fff,0x1234,16);});CHECK(m.load(0x10fff,8)==0);
    fault(FaultKind::memory,[&]{m.protect(0x10000,0x3000,Memory::Read);});CHECK(m.permissions(0x10000)&Memory::Write);
    std::array<std::uint8_t,4> out{1,2,3,4};fault(FaultKind::memory,[&]{m.copy_out(0x11ffe,out);});CHECK((out==std::array<std::uint8_t,4>{1,2,3,4}));
    m.decommit(0x10000,0x1000);m.commit(0x10000,0x1000,Memory::Read|Memory::Write);CHECK(m.load(0x10000,32)==0);
    fault(FaultKind::memory,[&]{m.release(0x11000);});m.release(0x10000);CHECK(m.available(0x10000,0x3000));
    m.map(0xfffffffcu,4,Memory::Read|Memory::Write);m.store(0xfffffffcu,0x12345678,32);CHECK(m.load(0xfffffffcu,32)==0x12345678);
    fault(FaultKind::memory,[&]{m.store(0xfffffffeu,0,32);});CHECK(m.load(0xfffffffcu,32)==0x12345678);
    Cpu s;s.r[EAX]=0x12345678;s.r[EDX]=0x01234567;s.eip=0x401000;
    const auto before=s;fault(FaultKind::divide,[&]{divide(s,0,32,false);});CHECK(s.r==before.r && s.flags==before.flags);
    s.r[EDX]=0xffffffff;s.r[EAX]=0x80000000;const auto divbefore=s;
    fault(FaultKind::divide,[&]{divide(s,0xffffffff,32,true);});CHECK(s.r==divbefore.r && s.flags==divbefore.flags);
    fault(FaultKind::unsupported,[&]{segment_address(s,0,1);});s.fs_valid=true;s.fs_base=0x7000;CHECK(segment_address(s,4,1)==0x7004);
    s.flags=0x8d7;s.defined_flags=STATUS_FLAGS;const auto old=s;CHECK(shift(s,0x98765432,0,32,0)==0x98765432);CHECK(s.flags==old.flags && s.defined_flags==old.defined_flags);
    Memory strings;strings.map(0x1000,6,Memory::Read|Memory::Write);strings.map(0x2000,4,Memory::Read|Memory::Write);strings.store(0x1000,0x44332211,32);
    Cpu rep;rep.r[ESI]=0x1000;rep.r[EDI]=0x2000;rep.r[ECX]=3;rep.eip=0x401100;std::uint64_t budget=10;
    fault(FaultKind::memory,[&]{string_op(rep,strings,0,16,32,1,0,budget);});CHECK(rep.r[ECX]==1 && rep.r[ESI]==0x1004 && rep.r[EDI]==0x2004 && rep.eip==0x401100);CHECK(strings.load(0x2000,32)==0x44332211);
    rep.r[ECX]=0;rep.r[ESI]=0;rep.r[EDI]=0;string_op(rep,strings,0,32,32,1,0,budget);
    rep.r[ECX]=20;rep.r[ESI]=0x1000;rep.r[EDI]=0x2000;budget=0;fault(FaultKind::budget,[&]{string_op(rep,strings,0,8,32,1,0,budget);});CHECK(rep.r[ECX]==20 && rep.r[ESI]==0x1000);
    // LOCK scope shares the exact mutex used by normal guest memory accesses.
    Memory shared;shared.map(0x1000,4,Memory::Read|Memory::Write);std::vector<std::thread> workers;
    for(int n=0;n<4;++n)workers.emplace_back([&]{Cpu cpu;for(int k=0;k<10000;++k){auto lock=shared.lock();auto v=add(cpu,shared.load(0x1000,32),1,32);shared.store(0x1000,v,32);}});
    for(auto& t:workers)t.join();CHECK(shared.load(0x1000,32)==40000);
    std::cout<<"sparse memory, atomic ranges, precise faults, REP restart and LOCK checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
