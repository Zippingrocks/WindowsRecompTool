#include "winrecomp/indexed_geometry.hpp"
#include "winrecomp/d3d9_host.hpp"
#include <array>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>
namespace {
unsigned checks{};
#define CHECK(x) do {++checks;if(!(x))throw std::runtime_error("index validation: " #x);}while(0)
void test(){
    using wr::geometry::index_range;
    std::array<std::uint16_t,6> a{4,1,6,4,6,6};
    auto v=index_range(8,a);CHECK(v && v->first==1 && v->vertices==6 && v->primitives==2);
    CHECK(!index_range(6,a));CHECK(!index_range(8,a,5));CHECK(!index_range(8,a,6,1));
    CHECK(!index_range(0,a));CHECK(!index_range(65536,a));CHECK(!index_range(0xffffffffu,a));
    CHECK(!index_range(8,{}));CHECK(!index_range(8,std::span(a).first(5)));
    CHECK(index_range(8,a,6,2).has_value());
    std::array<std::uint16_t,3> one{0,0,0}, high{65532,65533,65534}, bad{0,1,65535};
    CHECK(index_range(1,one)->vertices==1);CHECK(index_range(65535,high)->first==65532);
    CHECK(!index_range(65535,bad));
    std::vector<std::uint16_t> many(65535,0);CHECK(index_range(1,many)->primitives==21845);
    many.push_back(0);CHECK(!index_range(1,many));many.resize(65538);CHECK(!index_range(1,many));
    std::array<wr::host9::TexturedVertex,8> verts{};
    for(auto& p:verts)p={1,2,.5f,1,0xff000000,0,0};
    using wr::geometry::referenced_vertices_valid;
    auto s=std::span<const wr::host9::TexturedVertex>(verts);
    CHECK(referenced_vertices_valid(s,a));
    verts[0].x=std::numeric_limits<float>::quiet_NaN();CHECK(referenced_vertices_valid(s,a));
    verts[4].x=std::numeric_limits<float>::infinity();CHECK(!referenced_vertices_valid(s,a));
    verts[4].x=1;verts[4].u=std::numeric_limits<float>::quiet_NaN();CHECK(!referenced_vertices_valid(s,a));
    verts[4].u=0;verts[4].rhw=0;CHECK(!referenced_vertices_valid(s,a));
    verts[4].rhw=-1;CHECK(!referenced_vertices_valid(s,a));
    verts[4].rhw=1;CHECK(referenced_vertices_valid(s,a));CHECK(!referenced_vertices_valid(s,bad));
    // Differential range verification against a deliberately simple scan.
    std::mt19937 random(0x41d90026);
    for(unsigned n=0;n<5000;++n){
        const unsigned count=1+random()%1024, size=3*(1+random()%32);
        const unsigned limit=random()%1200, primitive_limit=random()%40;
        std::vector<std::uint16_t> indices(size);unsigned low=65535,up=0;bool valid=true;
        for(auto& i:indices){i=std::uint16_t(random()%(count+2));if(i>=count || i>limit)valid=false;low=std::min(low,unsigned(i));up=std::max(up,unsigned(i));}
        if(size/3>primitive_limit)valid=false;
        auto result=index_range(count,indices,limit,primitive_limit);CHECK(bool(result)==valid);
        if(result)CHECK(result->first==low && result->vertices==up-low+1 && result->primitives==size/3);
    }
}
}
int main(){try{test();std::cout<<checks<<" indexed range and payload assertions passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
