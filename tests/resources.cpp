#include "winrecomp/resources.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>
#define CHECK(x) do{if(!(x))throw std::runtime_error("resource check failed: " #x);++checks;}while(0)
namespace {
unsigned checks=0;
void put(wr::Image& im,std::size_t relative,std::uint32_t value,unsigned size=4){for(unsigned i=0;i<size;++i)im.bytes.at(512+relative+i)=std::uint8_t(value>>(i*8));}
wr::Image fixture(){
    wr::Image im;im.base=0x400000;im.size=0x3000;im.headers_size=512;im.bytes.resize(1024);
    im.sections={{".rsrc",0x1000,512,512,512,0x40000040}};im.directories[2]={0x1000,512};
    put(im,14,1,2);put(im,16,14);put(im,20,0x80000020);put(im,32+14,1,2);put(im,48,101);put(im,52,0x80000040);
    put(im,64+14,1,2);put(im,80,1033);put(im,84,96);put(im,96,0x1120);put(im,100,4);put(im,104,1252);put(im,0x120,0x12345678);return im;
}
void fails(const std::function<void(wr::Image&)>& edit){auto im=fixture();edit(im);bool caught=false;try{wr::resource_directory(im);}catch(const std::runtime_error&){caught=true;}CHECK(caught);}
}
int main(){try{
    auto im=fixture();auto r=wr::resource_directory(im);CHECK(r.size()==1);CHECK(r[0].language==1033 && r[0].codepage==1252);CHECK(r[0].size==4 && r[0].rva==0x1120);
    auto b=wr::resource_bytes(im,r[0]);CHECK(b.size()==4 && b[0]==0x78 && b[3]==0x12);
    CHECK(wr::find_resource(im,std::uint16_t(14),std::uint16_t(101)).has_value());CHECK(!wr::find_resource(im,std::uint16_t(14),std::uint16_t(100)));
    auto none=im;none.directories[2]={};CHECK(wr::resource_directory(none).empty());
    auto named=im;put(named,32+12,1,2);put(named,32+14,0,2);put(named,48,0x80000080);put(named,128,4,2);for(unsigned n=0;n<4;++n)put(named,130+2*n,unsigned("icon"[n]),2);
    CHECK(wr::find_resource(named,std::uint16_t(14),std::u16string(u"icon")).has_value());
    CHECK(!wr::find_resource(named,std::uint16_t(14),std::u16string(u"ICON")));
    auto empty=im;put(empty,100,0);CHECK(wr::resource_bytes(empty,wr::resource_directory(empty)[0]).empty());
    fails([](auto& x){x.directories[2].size=8;});fails([](auto& x){x.directories[2].rva=0;});
    fails([](auto& x){x.directories[2].rva=0xfffffff0;});fails([](auto& x){put(x,20,0x800001ff);});
    fails([](auto& x){put(x,20,32);});fails([](auto& x){put(x,52,64);});fails([](auto& x){put(x,84,0x80000000);});
    fails([](auto& x){put(x,20,0x80000000);});fails([](auto& x){put(x,16,65536);});
    fails([](auto& x){put(x,14,4097,2);});fails([](auto& x){put(x,16,0x800001ff);});
    fails([](auto& x){put(x,84,511);});fails([](auto& x){put(x,96,0xfffffffc);});
    fails([](auto& x){put(x,100,0x1000001);});fails([](auto& x){put(x,100,4096);});
    fails([](auto& x){put(x,108,1);});fails([](auto& x){put(x,96,0x2000);});
    fails([](auto& x){put(x,64+14,2,2);put(x,88,1033);put(x,92,96);});
    std::cout<<checks<<" resource parser assertions passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
