#include "winrecomp/resources.hpp"
#include <iostream>
#include <stdexcept>
#define CHECK(x) do{if(!(x))throw std::runtime_error("resource check failed: " #x);}while(0)
namespace {
void put(wr::Image& i,unsigned at,std::uint32_t v){for(unsigned k=0;k<4;++k)i.bytes.at(512+at+k)=std::uint8_t(v>>(8*k));}
wr::Image fixture(){
    wr::Image i;i.base=0x400000;i.entry=0x401000;i.size=0x2000;i.headers_size=512;i.bytes.resize(1536);
    i.sections={{".rsrc",0x1000,1024,512,1024,0x40000040}};i.directories[2]={0x1000,0x200};
    put(i,12,1u<<16);put(i,16,14);put(i,20,0x80000020);put(i,0x20+12,1u<<16);put(i,0x30,101);put(i,0x34,0x80000040);
    put(i,0x40+12,2u<<16);put(i,0x50,0x409);put(i,0x54,0x80);put(i,0x58,0x411);put(i,0x5c,0x90);
    put(i,0x80,0x1100);put(i,0x84,4);put(i,0x88,1252);put(i,0x90,0x1104);put(i,0x94,4);
    put(i,0x100,0x12345678);put(i,0x104,0xabcdef12);return i;
}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::runtime_error&){rejected=true;}CHECK(rejected);}
}
int main(int argc,char** argv){try{
    if(argc>1){auto image=wr::Image::load(argv[1]);auto table=wr::ResourceTable::parse(image);std::cout<<"real input resource leaves="<<table.size()<<'\n';return 0;}
    auto i=fixture();auto r=wr::ResourceTable::parse(i);CHECK(r.size()==2);CHECK(r.find(14u,101u));CHECK(r.find(14u,101u)->bytes.at(0)==0x78);
    CHECK(r.find(14u,101u,0x411)->bytes.at(0)==0x12);CHECK(!r.find(14u,102u));CHECK(!r.find(3u,101u));
    auto named=i;put(named,0x20+12,1);put(named,0x30,0x80000140);named.bytes[512+0x140]=4;
    const char16_t text[]=u"MAIN";for(unsigned k=0;k<4;++k){named.bytes[512+0x142+2*k]=std::uint8_t(text[k]);named.bytes[512+0x143+2*k]=0;}
    auto n=wr::ResourceTable::parse(named);CHECK(n.find(14u,std::u16string(u"MAIN")));CHECK(!n.find(14u,101u));
    {auto x=i;put(x,20,0x80000000);rejects([&]{wr::ResourceTable::parse(x);});}
    {auto x=i;put(x,20,0x800001ff);rejects([&]{wr::ResourceTable::parse(x);});}
    {auto x=i;put(x,0x54,0x80000080);rejects([&]{wr::ResourceTable::parse(x);});}
    {auto x=i;put(x,0x80,0xfffffff0);rejects([&]{wr::ResourceTable::parse(x);});}
    {auto x=i;put(x,0x84,0x40000001);rejects([&]{wr::ResourceTable::parse(x);});}
    {auto x=i;put(x,0x58,0x409);rejects([&]{wr::ResourceTable::parse(x);});}
    {auto x=i;put(x,0x54,0x1fc);rejects([&]{wr::ResourceTable::parse(x);});}
    {auto x=i;x.directories[2].size=15;rejects([&]{wr::ResourceTable::parse(x);});}
    {auto x=i;put(x,0x30,0x800001fc);rejects([&]{wr::ResourceTable::parse(x);});}
    {auto x=i;put(x,12,0xffff0000);rejects([&]{wr::ResourceTable::parse(x);});}
    {auto x=i;put(x,0x80,0x1ffe);rejects([&]{wr::ResourceTable::parse(x);});}
    auto empty=i;empty.directories[2]={};CHECK(wr::ResourceTable::parse(empty).size()==0);
    std::cout<<"resource ids, names, languages, cycles, duplicates, bounds and budgets passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
