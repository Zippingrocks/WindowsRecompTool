#pragma once
#include "winrecomp/integer.hpp"
#include "winrecomp/processor.hpp"
namespace wr {
inline void check_code(Memory& memory,U32 pc,std::initializer_list<std::uint8_t> expected) {
    if(expected.size()>15)throw std::runtime_error("invalid generated instruction length");
    std::array<std::uint8_t,15> actual{};memory.fetch(pc,std::span(actual.data(),expected.size()));
    if(!std::equal(expected.begin(),expected.end(),actual.begin()))throw GuestFault(FaultKind::unsupported,pc,"self-modified code differs from its static translation");
}
}
