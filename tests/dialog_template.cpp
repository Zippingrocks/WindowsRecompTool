#include "dialog_fixture.hpp"
#include <iostream>
#include <stdexcept>
namespace {
unsigned checks{};
void check(bool v){++checks;if(!v)throw std::runtime_error("dialog-template regression failed");}
void reject(const dialog_test::Bytes& b){bool failed=false;try{(void)wr::inspect_dialog_template(b);}catch(const std::runtime_error&){failed=true;}check(failed);}
}
int main(){try{
    auto b=dialog_test::resource();auto value=wr::inspect_dialog_template(b);
    check(value.title==u"WinRecomp modal fixture");check(value.controls.size()==6);check(value.controls[2].id==1001 && value.controls[2].atom==0x83);
    for(std::size_t n=0;n<b.size();++n)reject({b.begin(),b.begin()+n});
    for(auto tail: {dialog_test::Bytes{0,0,0,0},dialog_test::Bytes{1}}){auto v=b;v.insert(v.end(),tail.begin(),tail.end());reject(v);}
    for(auto [offset,x]:std::array<std::pair<std::size_t,std::uint32_t>,4>{{{0,0xffff0001u},{0,0xc0c000c0u},{4,0x80000},{8,257}}}){
        auto v=b;if(offset==8)dialog_test::put16(v,offset,x);else dialog_test::put32(v,offset,x);reject(v);
    }
    for(auto entry:value.controls){check(entry.atom>=0x80 && entry.atom<=0x85);}
    auto v=b;dialog_test::put16(v,18,1);reject(v);v=b;dialog_test::put16(v,20,1);reject(v);
    // A known item can be found by its exact DWORD style, rather than accepting
    // accidental native alignment or relying on sizeof(DLGTEMPLATE).
    std::vector<std::size_t> items;
    for(std::size_t at=4;at+22<b.size();at+=4)if(b[at+18]==0xff && b[at+19]==0xff && b[at+20]>=0x80 && b[at+20]<=0x85)items.push_back(at);
    check(items.size()==6);
    v=b;dialog_test::put32(v,items[0],0x5001000b);reject(v);
    v=b;dialog_test::put32(v,items[2],0x50a10111);reject(v);
    v=b;dialog_test::put32(v,items[2],0x50a12101);reject(v);
    v=b;dialog_test::put16(v,items[1]+16,1);reject(v);
    v=b;dialog_test::put16(v,items[0]+20,0x90);reject(v);
    v=b;dialog_test::put16(v,items[0]+18,0x41);reject(v);
    v=b;dialog_test::put16(v,items[0]+22,0xffff);reject(v);
    v=b;dialog_test::put16(v,v.size()-2,2);reject(v);
    std::cout<<checks<<" standard-dialog-template acceptance/rejection assertions passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
