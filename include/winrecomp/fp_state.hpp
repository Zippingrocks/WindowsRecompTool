#pragma once
#include <array>
#include <cstdint>
namespace wr {
// FXSAVE64 layout. x87 values remain ten-byte extended-precision bit patterns;
// no C++ float/double/long double conversion occurs in the runtime.
struct alignas(16) FpState {
    std::array<std::uint8_t,512> bytes{};
    FpState() { put16(0,0x037f);put32(24,0x1f80); }
    std::uint16_t get16(unsigned off) const {return std::uint16_t(unsigned(bytes.at(off))|(unsigned(bytes.at(off+1))<<8));}
    std::uint32_t get32(unsigned off) const {return std::uint32_t(get16(off))|(std::uint32_t(get16(off+2))<<16);}
    std::uint64_t get64(unsigned off) const {return std::uint64_t(get32(off))|(std::uint64_t(get32(off+4))<<32);}
    void put16(unsigned off,std::uint16_t value){bytes.at(off)=std::uint8_t(value);bytes.at(off+1)=std::uint8_t(value>>8);}
    void put32(unsigned off,std::uint32_t value){put16(off,std::uint16_t(value));put16(off+2,std::uint16_t(value>>16));}
    void put64(unsigned off,std::uint64_t value){put32(off,std::uint32_t(value));put32(off+4,std::uint32_t(value>>32));}
    unsigned top() const {return (get16(2)>>11)&7u;}
    std::uint16_t full_tag() const {
        unsigned result=0;
        for(unsigned physical=0;physical<8;++physical) {
            unsigned tag=3;
            if(bytes[4]&(1u<<physical)) {
                const auto logical=(physical+8-top())&7u;const auto at=32+logical*16;
                const auto sig=get64(at);const auto exponent=get16(at+8)&0x7fffu;
                tag=(!exponent && !sig)?1u:(exponent && exponent!=0x7fffu && (sig>>63))?0u:2u;
            }
            result|=tag<<(physical*2);
        }
        return std::uint16_t(result);
    }
};
static_assert(alignof(FpState)>=16);
}
