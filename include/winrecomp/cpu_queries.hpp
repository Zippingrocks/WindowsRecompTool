#pragma once
#include "winrecomp/runtime.hpp"
#include <atomic>
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif

namespace wr {
// CPUID is a query of the current x64 execution host, not a promise that the
// lifter implements every advertised ISA extension. Unsupported guest forms
// continue to fail closed; feature bits are never edited to force a game path.
inline void host_cpuid(Cpu& s) {
    std::atomic_thread_fence(std::memory_order_seq_cst);
#if defined(_MSC_VER)
    int result[4];
    __cpuidex(result,std::bit_cast<int>(s.r[EAX]),std::bit_cast<int>(s.r[ECX]));
    s.r[EAX]=U32(result[0]);s.r[EBX]=U32(result[1]);s.r[ECX]=U32(result[2]);s.r[EDX]=U32(result[3]);
#else
    U32 a,b,c,d;
    __cpuid_count(s.r[EAX],s.r[ECX],a,b,c,d);
    s.r[EAX]=a;s.r[EBX]=b;s.r[ECX]=c;s.r[EDX]=d;
#endif
    std::atomic_thread_fence(std::memory_order_seq_cst);
}
inline void push_flags(Cpu& s,Memory& m,unsigned width) {
    if(width!=16 && width!=32)throw std::runtime_error("invalid PUSHF operand width");
    if(s.flags&0x23000u)throw GuestFault(FaultKind::unsupported,s.eip,"PUSHF requires the CPL3/IOPL0 non-VM86 profile");
    push(s,m,(s.flags&~0x30000u)|2u,width);
}
inline void pop_flags(Cpu& s,Memory& m,unsigned width) {
    if(width!=16 && width!=32)throw std::runtime_error("invalid POPF operand width");
    if(s.flags&0x23000u)throw GuestFault(FaultKind::unsupported,s.eip,"POPF requires the CPL3/IOPL0 non-VM86 profile");
    const auto value=m.load(s.r[ESP],width);
    // At CPL3 and IOPL0, IF and IOPL are unchanged, as are VIF/VIP/VM.
    // ID and AC can only change in the 32-bit form; reserved bits stay clear.
    const U32 writable=(STATUS_FLAGS|DF|0x100u|0x4000u|(width==32?0x240000u:0u));
    const U32 result=(((s.flags&~writable)|(value&writable))&0x003e7fd7u)|2u;
    // Single-step delivery and alignment-check faults are not implemented.
    // Refuse before changing ESP/flags, rather than silently losing a trap.
    if(result&(0x100u|0x40000u))throw GuestFault(FaultKind::unsupported,s.eip,"POPF trap/alignment mode is not implemented");
    s.flags=result;s.r[ESP]+=width/8;s.defined_flags|=STATUS_FLAGS;
}
}
