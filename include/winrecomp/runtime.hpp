#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace wr {
using U32=std::uint32_t;
constexpr U32 CF=1, PF=4, AF=16, ZF=64, SF=128, OF=2048, STATUS_FLAGS=CF|PF|AF|ZF|SF|OF;
enum RegisterIndex { EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI };
struct Cpu { std::array<U32,8> r{}; U32 eip{}, flags{0x202}, defined_flags{STATUS_FLAGS}; };
class Memory {
public:
    enum Permission : unsigned { Read=1, Write=2, Execute=4 };
private:
    struct Region { U32 base; std::vector<std::uint8_t> data; unsigned permissions; };
    std::vector<Region> regions_;
    Region& region(U32 address,unsigned access) {
        for(auto& r:regions_) {
            if(address>=r.base && std::uint64_t(address)-r.base<r.data.size()) {
                if((r.permissions&access)!=access)throw std::runtime_error("guest memory permission fault at "+std::to_string(address));
                return r;
            }
        }
        throw std::runtime_error("unmapped guest address "+std::to_string(address));
    }
    static void width_check(unsigned width) { if(width!=8 && width!=16 && width!=32)throw std::runtime_error("unsupported memory width"); }
public:
    void map(U32 base,std::size_t size,unsigned permissions) {
        if(!size || size>512ull*1024*1024 || std::uint64_t(base)+size>0x100000000ull)throw std::runtime_error("invalid guest mapping");
        for(const auto& r:regions_)if(base<std::uint64_t(r.base)+r.data.size() && r.base<std::uint64_t(base)+size)throw std::runtime_error("overlapping guest mapping");
        regions_.push_back({base,std::vector<std::uint8_t>(size,0),permissions});
    }
    void initialize(U32 base,std::span<const std::uint8_t> bytes) {
        if(std::uint64_t(base)+bytes.size()>0x100000000ull)throw std::runtime_error("initialization wraps");
        // Validate all ranges first, then copy contiguous spans rather than individual bytes.
        for(std::size_t done=0;done<bytes.size();) {
            auto a=base+U32(done);auto& r=region(a,0);
            done+=std::min(bytes.size()-done,r.data.size()-std::size_t(a-r.base));
        }
        for(std::size_t done=0;done<bytes.size();) {
            auto a=base+U32(done);auto& r=region(a,0);
            auto n=std::min(bytes.size()-done,r.data.size()-std::size_t(a-r.base));
            std::copy_n(bytes.data()+done,n,r.data.data()+a-r.base);done+=n;
        }
    }
    void copy_out(U32 base,std::span<std::uint8_t> bytes) {
        if(std::uint64_t(base)+bytes.size()>0x100000000ull)throw std::runtime_error("read range wraps");
        for(std::size_t done=0;done<bytes.size();) {
            auto a=base+U32(done);auto& r=region(a,Read);
            auto n=std::min(bytes.size()-done,r.data.size()-std::size_t(a-r.base));
            std::copy_n(r.data.data()+a-r.base,n,bytes.data()+done);done+=n;
        }
    }
    U32 load(U32 address,unsigned width) {
        width_check(width);if(std::uint64_t(address)+width/8>0x100000000ull)throw std::runtime_error("guest load wraps address space");
        U32 value=0;for(unsigned i=0;i<width/8;++i){auto a=address+i;auto& r=region(a,Read);value|=U32(r.data[a-r.base])<<(8*i);}return value;
    }
    void store(U32 address,U32 value,unsigned width) {
        width_check(width);if(std::uint64_t(address)+width/8>0x100000000ull)throw std::runtime_error("guest store wraps address space");
        // Validate the complete store before changing any byte.
        for(unsigned i=0;i<width/8;++i)(void)region(address+i,Write);
        for(unsigned i=0;i<width/8;++i){auto a=address+i;auto& r=region(a,Write);r.data[a-r.base]=std::uint8_t(value>>(i*8));}
    }
};
inline U32 mask(unsigned width) { if(width!=8 && width!=16 && width!=32)throw std::runtime_error("unsupported integer width");return width==32?0xffffffffu:(U32(1)<<width)-1; }
inline U32 reg(const Cpu& s,unsigned index,unsigned width=32,unsigned shift=0) { return (s.r.at(index)>>shift)&mask(width); }
inline void set_reg(Cpu& s,unsigned index,U32 value,unsigned width=32,unsigned shift=0) { const auto m=mask(width)<<shift;s.r.at(index)=(s.r.at(index)&~m)|((value<<shift)&m); }
inline U32 sign_extend(U32 v,unsigned width) { v&=mask(width);const U32 sign=U32(1)<<(width-1);return (v^sign)-sign; }
inline U32 szp(U32 value,unsigned width) {
    value&=mask(width);
    return (value==0?ZF:0) | (value&(U32(1)<<(width-1))?SF:0) | ((std::popcount(value&0xffu)&1)==0?PF:0);
}
inline U32 add(Cpu& s,U32 a,U32 b,unsigned width,U32 carry=0) {
    const U32 m=mask(width), sign=U32(1)<<(width-1);a&=m;b&=m;carry&=1;
    const std::uint64_t wide=std::uint64_t(a)+b+carry;const U32 r=U32(wide)&m;
    const U32 f=szp(r,width) | (wide>m?CF:0) | ((~(a^b)&(a^r)&sign)?OF:0) | ((a^b^r)&AF);
    s.flags=(s.flags&~STATUS_FLAGS)|f;s.defined_flags|=STATUS_FLAGS;return r;
}
inline U32 sub(Cpu& s,U32 a,U32 b,unsigned width,U32 borrow=0) {
    const U32 m=mask(width), sign=U32(1)<<(width-1);a&=m;b&=m;borrow&=1;
    const std::uint64_t rhs=std::uint64_t(b)+borrow;const U32 r=U32(std::uint64_t(a)-rhs)&m;
    const U32 f=szp(r,width) | (std::uint64_t(a)<rhs?CF:0) | (((a^b)&(a^r)&sign)?OF:0) | ((a^b^r)&AF);
    s.flags=(s.flags&~STATUS_FLAGS)|f;s.defined_flags|=STATUS_FLAGS;return r;
}
inline U32 logic(Cpu& s,U32 r,unsigned width) {
    r&=mask(width);s.flags=(s.flags&~(CF|PF|ZF|SF|OF))|szp(r,width);s.defined_flags=(s.defined_flags|STATUS_FLAGS)&~AF;return r;
}
inline U32 incdec(Cpu& s,U32 a,unsigned width,bool decrement) {
    const auto carry=s.flags&CF, defined=s.defined_flags&CF;
    const auto r=decrement?sub(s,a,1,width):add(s,a,1,width);
    s.flags=(s.flags&~CF)|carry;s.defined_flags=(s.defined_flags&~CF)|defined;return r;
}
inline void push(Cpu& s,Memory& m,U32 v,unsigned width=32) { const auto p=s.r[ESP]-width/8;m.store(p,v,width);s.r[ESP]=p; }
inline U32 pop(Cpu& s,Memory& m,unsigned width=32) { const auto v=m.load(s.r[ESP],width);s.r[ESP]+=width/8;return v; }
}
