#include "winrecomp/core.hpp"
#include "winrecomp/runtime.hpp"
#include <iostream>
#include <stdexcept>
#define CHECK(x) do { if(!(x))throw std::runtime_error("check failed: " #x); } while(0)
int main(){try{
    wr::Cpu s;CHECK(wr::add(s,0xffffffffu,1,32)==0);CHECK((s.flags&(wr::CF|wr::ZF))==(wr::CF|wr::ZF));
    CHECK(wr::add(s,0x7fffffffu,1,32)==0x80000000u);CHECK(s.flags&wr::OF);
    CHECK(wr::sub(s,0,1,8)==255);CHECK(s.flags&wr::CF);
    CHECK(wr::sub(s,0x80000000u,1,32)==0x7fffffffu);CHECK(s.flags&wr::OF);
    s.flags|=wr::CF;CHECK(wr::incdec(s,255,8,false)==0);CHECK(s.flags&wr::CF);
    wr::set_reg(s,wr::EAX,0x12345678);wr::set_reg(s,wr::EAX,0xaa,8,8);CHECK(s.r[wr::EAX]==0x1234aa78);
    CHECK(wr::sign_extend(0x80,8)==0xffffff80u);
    wr::Memory m;m.map(0x1000,16,wr::Memory::Read|wr::Memory::Write);m.store(0x1001,0x89abcdef,32);CHECK(m.load(0x1001,32)==0x89abcdef);
    bool fault=false;try{m.store(0x100e,0xffffffff,32);}catch(const std::exception&){fault=true;}CHECK(fault);CHECK(m.load(0x100e,16)==0);
    s.r[wr::ESP]=0x1010;wr::push(s,m,0x12345678);CHECK(s.r[wr::ESP]==0x100c);CHECK(wr::pop(s,m)==0x12345678);CHECK(s.r[wr::ESP]==0x1010);
    wr::Decoder d;std::array<std::uint8_t,5>b{0xe8,0xfb,0xff,0xff,0xff};auto i=d.decode(b,0x401000);CHECK(i && i->target==0x401000 && i->flow==wr::Flow::call);
    std::array<std::uint8_t,1>bad{0x0f};CHECK(!d.decode(bad,0x401000));
    std::cout<<"core checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
