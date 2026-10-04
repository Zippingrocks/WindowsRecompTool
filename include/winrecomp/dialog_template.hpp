#pragma once
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
#include <set>

namespace wr {
// This is a bounded *standard* dialog-template profile, not an arbitrary native
// resource loader. It rejects custom classes, menus, owner draw and creation
// data before Windows sees the template. All offsets are relative to the span.
struct DialogControl { std::uint16_t id{},atom{};std::uint32_t style{}; };
struct DialogTemplate {
    std::uint32_t style{};
    std::u16string title;
    std::vector<DialogControl> controls;
};
inline DialogTemplate inspect_dialog_template(std::span<const std::uint8_t> bytes) {
    auto invalid=[](const char* why)->void {throw std::runtime_error(std::string("unsupported/invalid dialog template: ")+why);};
    if(bytes.size()<18 || bytes.size()>65536)invalid("size");
    std::size_t at=0;
    auto word=[&]()->std::uint16_t {
        if(at>bytes.size() || bytes.size()-at<2)invalid("truncated WORD");
        auto v=std::uint16_t(unsigned(bytes[at])|(unsigned(bytes[at+1])<<8));at+=2;return v;
    };
    auto dword=[&]()->std::uint32_t {auto low=word();return std::uint32_t(low)|(std::uint32_t(word())<<16);};
    auto text=[&]()->std::u16string {
        std::u16string s;
        for(unsigned n=0;n<=4096;++n){auto c=word();if(!c)return s;if(c==0xffff)invalid("ordinal text/resource reference");s+=char16_t(c);}
        invalid("string limit");return {};
    };
    if(bytes[2]==0xff && bytes[3]==0xff)invalid("extended templates not implemented");
    DialogTemplate out;out.style=dword();auto extended=dword();auto count=word();
    (void)word();(void)word();auto width=word(),height=word();
    if(!width || width>32767 || !height || height>32767 || count>256)invalid("dimensions/control limit");
    if((out.style&0x40000000u) || (out.style&0x400u) || (extended&0x80000u))invalid("child/control/layered dialog");
    if(word()!=0 || word()!=0)invalid("menu or custom dialog class");
    out.title=text();
    if(out.style&0x40){auto points=word();if(!points || points>256)invalid("font size");if(text().empty())invalid("font name");}
    std::set<std::uint16_t> ids;
    for(unsigned i=0;i<count;++i){
        // A standard DLGITEMTEMPLATE begins on a DWORD boundary.
        at=(at+3)&~std::size_t(3);auto style=dword();(void)dword();
        (void)word();(void)word();auto cx=word(),cy=word(),id=word();
        if(cx>32767 || cy>32767 || !(style&0x40000000u))invalid("control dimensions/style");
        if(id!=0xffff && !ids.insert(id).second)invalid("duplicate control identifier");
        if(word()!=0xffff)invalid("custom control class");
        auto atom=word();if(atom<0x80 || atom>0x85)invalid("control class atom");
        if((atom==0x80 && (style&15)==11) || (atom==0x82 && ((style&31)==3 || (style&31)==13 || (style&31)==14 || (style&31)==15)) ||
           ((atom==0x83 || atom==0x85) && (style&0x30)) || (atom==0x83 && (style&0x2000)))invalid("owner-draw/resource/data-only control");
        (void)text();if(word()!=0)invalid("control creation data");
        out.controls.push_back({id,atom,style});
    }
    if(at>bytes.size() || bytes.size()-at>3)invalid("trailing data");
    while(at<bytes.size())if(bytes[at++])invalid("nonzero trailing padding");
    return out;
}
}
