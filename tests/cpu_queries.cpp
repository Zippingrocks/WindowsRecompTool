#include "winrecomp/cpu_queries.hpp"
#include <iostream>
#include <random>
namespace {
unsigned checks{};
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("CPU query assertion: " #x);}while(0)
}
int main(){try{
    wr::Cpu s;wr::Memory m;m.map(0x10000,4096,3);s.r[wr::ESP]=0x10800;
    std::mt19937 random(982531);
    for(unsigned i=0;i<1024;++i){
        auto flags=(random()&(wr::STATUS_FLAGS|wr::DF|0x200000u))|0x202u;
        s.flags=flags;const auto sp=s.r[wr::ESP];
        wr::push_flags(s,m,32);CHECK(m.load(sp-4,32)==flags);
        s.flags=0x202;wr::pop_flags(s,m,32);CHECK(s.flags==flags && s.r[wr::ESP]==sp);
        wr::push(s,m,0xffffffffu&~(0x100u|0x40000u|0x4000u));wr::pop_flags(s,m,32);
        CHECK(s.flags==((wr::STATUS_FLAGS|wr::DF|0x200000u)|0x202u));
        s.flags=flags;wr::push(s,m,0x2u,16);wr::pop_flags(s,m,16);
        CHECK(s.flags==((flags&0x200000u)|0x202u));CHECK(s.r[wr::ESP]==sp);
    }
    for(auto flags:{0x100u,0x40000u}){
        wr::push(s,m,flags);const auto old=s;bool caught=false;
        try{wr::pop_flags(s,m,32);}catch(const wr::GuestFault& e){CHECK(e.kind==wr::FaultKind::unsupported);caught=true;}
        CHECK(caught && s.r==old.r && s.flags==old.flags);wr::pop(s,m);
    }
    s.flags=0x10202;wr::push_flags(s,m,32);CHECK(wr::pop(s,m)==0x202);
    // CPUID(0) is stable across cores. Compare direct native instruction output
    // with the helper and verify that unrelated guest state is preserved.
    wr::U32 a,b,c,d;
#if defined(_MSC_VER)
    int output[4];__cpuidex(output,0,0);a=wr::U32(output[0]);b=wr::U32(output[1]);c=wr::U32(output[2]);d=wr::U32(output[3]);
#else
    __asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(0),"c"(0));
#endif
    for(unsigned i=0;i<256;++i){
        for(auto& reg:s.r)reg=random();s.r[wr::EAX]=0;s.r[wr::ECX]=0;
        s.flags=0x202u|(random()&wr::STATUS_FLAGS);const auto before=s;
        wr::host_cpuid(s);CHECK(s.r[wr::EAX]==a && s.r[wr::EBX]==b && s.r[wr::ECX]==c && s.r[wr::EDX]==d);
        CHECK(s.flags==before.flags && s.defined_flags==before.defined_flags && s.eip==before.eip);
        for(unsigned r:{wr::ESP,wr::EBP,wr::ESI,wr::EDI})CHECK(s.r[r]==before.r[r]);
    }
    std::cout<<checks<<" CPU flag/query assertions passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
