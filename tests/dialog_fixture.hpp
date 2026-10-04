#pragma once
#include "winrecomp/dialog_template.hpp"
#include "winrecomp/core.hpp"
#include <algorithm>
#include <array>
#include <vector>
namespace dialog_test {
using Bytes=std::vector<std::uint8_t>;
inline void word(Bytes& b,unsigned v){b.push_back(std::uint8_t(v));b.push_back(std::uint8_t(v>>8));}
inline void dword(Bytes& b,std::uint32_t v){word(b,v);word(b,v>>16);}
inline void put16(Bytes& b,std::size_t a,unsigned v){b.at(a)=std::uint8_t(v);b.at(a+1)=std::uint8_t(v>>8);}
inline void put32(Bytes& b,std::size_t a,std::uint32_t v){put16(b,a,v);put16(b,a+2,v>>16);}
inline void text(Bytes& b,const std::u16string& s){for(auto c:s)word(b,c);word(b,0);}
inline void align(Bytes& b){while(b.size()%4)b.push_back(0);}
inline Bytes resource(){
    Bytes b;dword(b,0x80c000c0);dword(b,0);word(b,6);word(b,0);word(b,0);word(b,210);word(b,100);word(b,0);word(b,0);
    text(b,u"WinRecomp modal fixture");word(b,8);text(b,u"MS Sans Serif");
    auto control=[&](std::uint32_t style,unsigned id,unsigned atom,const char16_t* name,unsigned y){
        align(b);dword(b,style);dword(b,0);word(b,5);word(b,y);word(b,100);word(b,12);word(b,id);word(b,0xffff);word(b,atom);text(b,name);word(b,0);
    };
    control(0x50010001,1,0x80,u"OK",5);
    control(0x50010000,2,0x80,u"Cancel",20);
    control(0x50a10101,1001,0x83,u"",35);
    control(0x50010003,1002,0x80,u"Check",50);
    control(0x50020009,1003,0x80,u"First",65);
    control(0x50000009,1004,0x80,u"Second",80);
    return b;
}
inline wr::Image image(Bytes b=resource()){
    wr::Image im;im.base=0x400000;im.entry=0x401000;im.size=0x4000;im.headers_size=512;
    im.bytes.resize(0x1600);im.sections={{".text",0x1000,512,512,512,0x60000020},{".data",0x2000,512,1024,512,0xc0000040},{".rsrc",0x3000,4096,1536,4096,0x40000040}};
    im.bytes[512]=0xc3;im.bytes[528]=0xc3;im.bytes[544]=0xc3;
    const unsigned base=1536;
    put16(im.bytes,base+14,1);put32(im.bytes,base+16,5);put32(im.bytes,base+20,0x80000018);
    put16(im.bytes,base+24+14,1);put32(im.bytes,base+40,101);put32(im.bytes,base+44,0x80000030);
    put16(im.bytes,base+48+14,1);put32(im.bytes,base+64,1033);put32(im.bytes,base+68,72);
    put32(im.bytes,base+72,0x3060);put32(im.bytes,base+76,std::uint32_t(b.size()));
    std::copy(b.begin(),b.end(),im.bytes.begin()+base+96);
    im.directories[2]={0x3000,std::uint32_t(96+b.size())};return im;
}
}
