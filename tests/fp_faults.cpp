#include "winrecomp/sse.hpp"
#include <iostream>
#include <stdexcept>
extern "C" void wr_x87_217_m0(void*,void*,wr::U32*); // FLD m32
extern "C" void wr_sse_243_94_r1(void*,void*,wr::U32*); // DIVSS xmm0,xmm1
#define CHECK(x) do{if(!(x))throw std::runtime_error("FP fault check failed: " #x);}while(0)
template<class F> void fault(F&& f,wr::FaultKind kind){bool seen=false;try{f();}catch(const wr::GuestFault& e){seen=true;CHECK(e.kind==kind);}CHECK(seen);}
int main(){try{
    wr::Memory memory;memory.map(0x10000,4096,3);wr::Cpu cpu;cpu.eip=0x401000;
    // A signaling NaN raises an unmasked invalid operation. No guest data or
    // register changes are committed; host floating-point remains usable.
    memory.store(0x10000,0x7fa12345,32);cpu.fp.put16(0,0x37e);auto before=cpu;
    fault([&]{wr::x87_memory(cpu,memory,wr_x87_217_m0,0x10000,4,false,0x100);},wr::FaultKind::floating);
    CHECK(cpu.fp.bytes==before.fp.bytes && cpu.flags==before.flags);CHECK(memory.load(0x10000,32)==0x7fa12345);
    cpu.fp.put16(0,0x37f);wr::x87_memory(cpu,memory,wr_x87_217_m0,0x10000,4,false,0x100);CHECK(cpu.fp.get16(2)&1u);
    // Pending exceptions remain explicit until cleared by a non-waiting control.
    cpu.fp.put16(0,0x37e);before=cpu;fault([&]{wr::x87_wait(cpu);},wr::FaultKind::floating);CHECK(cpu.fp.bytes==before.fp.bytes);
    wr::x87_clear(cpu);wr::x87_wait(cpu);wr::x87_init(cpu);
    // Unmasked SSE divide by zero likewise faults without committing XMM/MXCSR.
    cpu.fp.put32(160,0x3f800000);cpu.fp.put32(176,0);cpu.fp.put32(24,0x1d80);before=cpu;
    wr::U32 dummy{};fault([&]{wr::sse_execute(cpu,wr_sse_243_94_r1,&dummy);},wr::FaultKind::floating);
    CHECK(cpu.fp.bytes==before.fp.bytes && cpu.flags==before.flags);
    cpu.fp.put32(24,0x1f80);wr::sse_execute(cpu,wr_sse_243_94_r1,&dummy);CHECK(cpu.fp.get32(160)==0x7f800000 && (cpu.fp.get32(24)&4u));
    // Reserved MXCSR and incorrectly aligned operands are rejected before native execution.
    cpu.fp.put32(24,0x10000);fault([&]{wr::sse_execute(cpu,wr_sse_243_94_r1,&dummy);},wr::FaultKind::unsupported);
    fault([&]{wr::sse_memory(cpu,memory,wr_sse_243_94_r1,0x10001,16,false,16);},wr::FaultKind::memory);
    // Legacy environment pointers are guest addresses; host selector values
    // must not leak in or be silently discarded on restore.
    wr::x87_init(cpu);cpu.fp.put64(8,0x412345);cpu.fp.put64(16,0x501234);cpu.fp.put16(6,0x321);
    wr::x87_env(cpu,memory,0x10100,false,false);
    CHECK(memory.load(0x1010c,32)==0x412345 && memory.load(0x10114,32)==0x501234);
    CHECK(memory.load(0x10110,16)==0 && memory.load(0x10118,16)==0);
    CHECK(memory.load(0x10112,16)==0x321 && memory.load(0x1011a,16)==0xffff);
    for(auto offset:{16u,24u}) {
        before=cpu;memory.store(0x10100+offset,0x2b,16);
        fault([&]{wr::x87_env(cpu,memory,0x10100,true,false);},wr::FaultKind::unsupported);
        CHECK(cpu.fp.bytes==before.fp.bytes);memory.store(0x10100+offset,0,16);
    }
    wr::x87_env(cpu,memory,0x10100,true,false);
    CHECK(cpu.fp.get64(8)==0x412345 && cpu.fp.get64(16)==0x501234 && cpu.fp.get16(6)==0x321);
    std::cout<<"unmasked/pending FP faults, masked recovery, state preservation and alignment checks passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
