#include "winrecomp/process.hpp"
#include "winrecomp/nls_reference.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
#define CHECK(x) do{if(!(x))throw std::runtime_error("NLS check failed: " #x);}while(0)
namespace {
bool step(wr::Cpu&,wr::Memory&,std::uint64_t&){return false;}
wr::U32 call(wr::Process& p,const char* name,std::initializer_list<wr::U32> args){
 const auto sp=p.cpu.r[wr::ESP],pc=p.cpu.eip;
 for(auto i=args.end();i!=args.begin();)wr::push(p.cpu,p.memory,*--i);
 wr::push(p.cpu,p.memory,pc);p.cpu.eip=p.resolve("kernel32.dll",name);CHECK(p.dispatch_api());CHECK(p.cpu.r[wr::ESP]==sp && p.cpu.eip==pc);return p.cpu.r[wr::EAX];
}
}
int main(){try{
 wr::Process p(step);p.memory.map(0x100000,0x10000,3);p.cpu.r[wr::ESP]=0x10f000;p.cpu.eip=0x401000;
 const wr::U32 src=0x101000,dst=0x102000,used=0x103000;unsigned comparisons=0;
 for(const auto& row:wr::nls_rows){
  p.memory.store(src,row.code,16);
  for(unsigned i=0;i<3;++i){auto flag=1u<<i;CHECK(call(p,"GetStringTypeW",{flag,src,1,dst})==1);const std::array expected{row.ctype1,row.ctype2,row.ctype3};CHECK(p.memory.load(dst,16)==expected[i]);
#ifdef _WIN32
   wchar_t ch=wchar_t(row.code);WORD native{};CHECK(::GetStringTypeW(flag,&ch,1,&native));CHECK(p.memory.load(dst,16)==native);
#endif
   ++comparisons;
  }
  for(unsigned flag:{0x100u,0x200u}){CHECK(call(p,"LCMapStringW",{0x409,flag,src,1,0,0})==1);CHECK(call(p,"LCMapStringW",{0x409,flag,src,1,dst,1})==1);CHECK(p.memory.load(dst,16)==(flag==0x100?row.lower:row.upper));
#ifdef _WIN32
   wchar_t ch=wchar_t(row.code),native[8]{};CHECK(::LCMapStringW(0x409,flag,&ch,1,native,8)==1);CHECK(p.memory.load(dst,16)==unsigned(native[0]));
#endif
   ++comparisons;
  }
 }
 const std::array<wr::U32,4> flags{0,0x200,0x220,0x400};
 for(const auto& row:wr::nls_encodings){p.memory.store(src,row.code,16);
  for(unsigned i=0;i<4;++i){p.memory.store(used,0xcccccccc,32);CHECK(call(p,"WideCharToMultiByte",{1252,flags[i],src,1,dst,1,0,used})==1);CHECK(p.memory.load(dst,8)==row.value[i]);CHECK(p.memory.load(used,32)==row.substituted[i]);
#ifdef _WIN32
   wchar_t ch=wchar_t(row.code);char result[8]{};BOOL native_used=FALSE;CHECK(::WideCharToMultiByte(1252,flags[i],&ch,1,result,8,nullptr,&native_used)==1);CHECK(p.memory.load(dst,8)==static_cast<unsigned char>(result[0]));CHECK(bool(p.memory.load(used,32))==bool(native_used));
#endif
   ++comparisons;
  }
 }
 // A measured explicit-length corpus also preserves embedded NULs in bulk.
 for(unsigned n=0;n<wr::nls_encodings.size();++n)p.memory.store(src+n*2,wr::nls_encodings[n].code,16);
 for(unsigned i=0;i<4;++i){auto count=wr::U32(wr::nls_encodings.size());CHECK(call(p,"WideCharToMultiByte",{1252,flags[i],src,count,dst,count,0,used})==count);bool any=false;for(unsigned n=0;n<count;++n){CHECK(p.memory.load(dst+n,8)==wr::nls_encodings[n].value[i]);any|=wr::nls_encodings[n].substituted[i];}CHECK(bool(p.memory.load(used,32))==any);++comparisons;}
 std::cout<<comparisons<<" bounded NLS classification/casing/encoding checks passed";
#ifdef _WIN32
 std::cout<<" including direct Windows API comparisons";
#endif
 std::cout<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
