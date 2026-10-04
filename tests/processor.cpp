#include "winrecomp/processor.hpp"
#include <iostream>
#include <random>
#include <cstring>
extern "C" std::uint64_t wr_flags_oracle_16(std::uint32_t,std::uint32_t);
extern "C" std::uint64_t wr_flags_oracle_32(std::uint32_t,std::uint32_t);
namespace {
unsigned checks{},hardware{};
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("processor assertion: " #x);}while(0)
template<class F>void fault(F&& fn,wr::FaultKind k=wr::FaultKind::unsupported){bool caught=false;try{fn();}catch(const wr::GuestFault& e){CHECK(e.kind==k);caught=true;}CHECK(caught);}
void run(){
    wr::Cpu c;wr::Memory m;m.map(0x10000,4096,wr::Memory::Read|wr::Memory::Write);std::mt19937 rng(0xc9b2);
    for(unsigned width:{16u,32u})for(unsigned n=0;n<4096;++n){
        const auto old=0x202u|(wr::U32(rng())&(wr::STATUS_FLAGS|wr::DF|wr::ID));
        // Never change TF/NT/AC on the actual host. IF/IOPL/reserved bits may
        // be requested: user-mode hardware must retain/ignore them correctly.
        const auto value=wr::U32(rng())&~(wr::TF|wr::NT|wr::AC);
        const auto original=width==16?wr_flags_oracle_16(old,value):wr_flags_oracle_32(old,value);
        c.flags=old;c.defined_flags=0;c.r[wr::ESP]=0x10800;m.store(c.r[wr::ESP],value,width);
        wr::pop_flags(c,m,width);++hardware;
        CHECK(c.flags==std::uint32_t(original));CHECK(c.r[wr::ESP]==0x10800+width/8);CHECK(c.defined_flags==wr::STATUS_FLAGS);
        auto preserved=c.flags;wr::push_flags(c,m,width);CHECK(c.flags==preserved);CHECK(c.r[wr::ESP]==0x10800);
        CHECK(m.load(0x10800,width)==(std::uint32_t(original)&wr::mask(width)));
    }
    c.flags=0x202|wr::RF;c.r[wr::ESP]=0x10800;wr::push_flags(c,m,32);CHECK(m.load(c.r[wr::ESP],32)==0x202);CHECK(c.flags&wr::RF);
    for(unsigned width:{16u,32u})for(wr::U32 bit:{wr::TF,wr::NT,wr::AC}) {
        if(width==16 && bit==wr::AC)continue;
        c.flags=0x202;c.r[wr::ESP]=0x10800;m.store(c.r[wr::ESP],bit,width);auto before=c;
        fault([&]{wr::pop_flags(c,m,width);});CHECK(c.r==before.r && c.flags==before.flags);
    }
    c.flags=0x202;c.r[wr::ESP]=0;auto before=c;fault([&]{wr::pop_flags(c,m,32);},wr::FaultKind::memory);CHECK(c.r==before.r && c.flags==before.flags);
    c.r[wr::ESP]=0x10000;before=c;fault([&]{wr::push_flags(c,m,32);},wr::FaultKind::memory);CHECK(c.r==before.r && c.flags==before.flags);
    c.r={0,13,12,11,10,9,8,7};before=c;fault([&]{wr::cpuid(c);});CHECK(c.r==before.r && c.flags==before.flags);
    CHECK(wr::processor_feature(c,0)==0 && wr::processor_feature(c,1)==0);fault([&]{wr::processor_feature(c,6);});
    c.processor_profile=wr::ProcessorProfile::scalar_v1;
    for(auto leaf:{0u,1u,2u,7u,0x40000000u,0x80000000u,0x80000001u,0xffffffffu})for(auto sub:{0u,1u,0xffffffffu}){
        c.r={leaf,sub,0x1234,0x8765,10,9,8,7};before=c;wr::cpuid(c);
        CHECK(c.flags==before.flags && c.defined_flags==before.defined_flags);for(unsigned i=4;i<8;++i)CHECK(c.r[i]==before.r[i]);
        if(leaf==0){char vendor[13]{};std::memcpy(vendor,&c.r[wr::EBX],4);std::memcpy(vendor+4,&c.r[wr::EDX],4);std::memcpy(vendor+8,&c.r[wr::ECX],4);CHECK(std::string(vendor)=="WinRecompCPU");CHECK(c.r[wr::EAX]==1);}
        else if(leaf==1){CHECK(c.r[wr::EDX]==0x8001);CHECK(c.r[wr::ECX]==0x80000000);CHECK(c.r[wr::EBX]==0);CHECK(c.r[wr::EAX]==0x600);}
        else if(leaf==0x80000000u){CHECK(c.r[wr::EAX]==0x80000000u && !c.r[wr::EBX] && !c.r[wr::ECX] && !c.r[wr::EDX]);}
        else if(leaf!=0x40000000u)CHECK(c.r[wr::EAX]==0 && c.r[wr::EBX]==0 && c.r[wr::ECX]==0 && c.r[wr::EDX]==0);
    }
    for(unsigned f=0;f<64;++f)CHECK(wr::processor_feature(c,f)==0);
}
}
int main(){try{run();std::cout<<checks<<" processor assertions; "<<hardware<<" uninterrupted native FLAGS comparisons passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
