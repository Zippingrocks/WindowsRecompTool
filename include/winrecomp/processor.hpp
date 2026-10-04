#pragma once
#include "winrecomp/runtime.hpp"
namespace wr {
constexpr U32 TF=0x100, IF=0x200, IOPL=0x3000, NT=0x4000;
constexpr U32 RF=0x10000, VM=0x20000, AC=0x40000, ID=0x200000;
// User-mode, CPL=3, IOPL=0. Reserved bits are zero, bit 1 is fixed one.
constexpr U32 EFLAGS_DEFINED_BITS=0x003f7fd7u;
inline U32 flags_image(U32 flags,unsigned width) {
    if(width!=16 && width!=32)throw std::runtime_error("unsupported FLAGS stack width");
    return ((flags&EFLAGS_DEFINED_BITS&~(RF|VM))|2u)&mask(width);
}
inline void push_flags(Cpu& s,Memory& m,unsigned width) {
    // Reading undefined arithmetic flags retains the selected runtime value,
    // not a claim that those unspecified values equal every physical processor.
    push(s,m,flags_image(s.flags,width),width);
}
inline void pop_flags(Cpu& s,Memory& m,unsigned width) {
    if(width!=16 && width!=32)throw std::runtime_error("unsupported FLAGS stack width");
    const auto value=m.load(s.r[ESP],width);
    if(s.flags&(TF|NT|AC|VM|IOPL))
        throw GuestFault(FaultKind::unsupported,s.eip,"FLAGS restore outside CPL3/IOPL0 profile");
    if(value&(TF|NT|(width==32?AC:0u)))
        throw GuestFault(FaultKind::unsupported,s.eip,"trap/task/alignment-check modes are not implemented");
    // IF and IOPL are not writable by this user-mode guest. RF is cleared;
    // VM/VIF/VIP are not taken from the stack. POPF16 preserves AC and ID.
    const auto writable=STATUS_FLAGS|DF|(width==32?ID:0u);
    const auto updated=((s.flags&~(writable|RF))|(value&writable))&EFLAGS_DEFINED_BITS;
    s.flags=updated|2u;s.defined_flags|=STATUS_FLAGS;s.r[ESP]+=width/8;
}
inline const char* processor_profile_name(ProcessorProfile p) {
    switch(p) {case ProcessorProfile::unspecified:return "unspecified";
    case ProcessorProfile::scalar_v1:return "scalar-v1";}
    return "invalid";
}
inline void cpuid(Cpu& s) {
    if(s.processor_profile!=ProcessorProfile::scalar_v1)
        throw GuestFault(FaultKind::unsupported,s.eip,"CPUID requires explicit --cpu-profile scalar-v1");
    const auto leaf=s.r[EAX];
    s.r[EAX]=s.r[EBX]=s.r[ECX]=s.r[EDX]=0;
    // A transparent, versioned virtual CPU contract, not host CPUID passthrough.
    // FPU+CMOV; no advertised SIMD/MMX/3DNow/TSC/XSAVE/AVX. Some translated SIMD
    // forms exist but are not advertised as a complete ISA family.
    if(leaf==0) {
        s.r[EAX]=1;s.r[EBX]=0x526e6957;s.r[EDX]=0x6d6f6365;s.r[ECX]=0x55504370; // WinRecompCPU
    } else if(leaf==1) {
        s.r[EAX]=0x600;s.r[ECX]=0x80000000u;s.r[EDX]=(1u<<0)|(1u<<15);
    } else if(leaf==0x80000000u) {
        s.r[EAX]=0x80000000u;
    } else if(leaf==0x40000000u) {
        s.r[EAX]=0x40000000u;s.r[EBX]=0x526e6957;s.r[ECX]=0x6d6f6365;s.r[EDX]=0x55504370;
    }
    // No flags are changed; subleaves are unused in this bounded profile.
}
inline U32 processor_feature(const Cpu& s,U32 feature) {
    if(s.processor_profile!=ProcessorProfile::scalar_v1 && feature>1)
        throw GuestFault(FaultKind::unsupported,s.eip,"processor feature query requires an explicit CPU profile");
    // PF_FLOATING_POINT_EMULATED/PRECISION_ERRATA are both false. Scalar-v1
    // deliberately advertises none of the optional Windows PF extensions.
    return 0;
}
}
