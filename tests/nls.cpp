#include "winrecomp/process.hpp"
#include "winrecomp/nls_reference.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
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
    wr::push(p.cpu,p.memory,pc);p.cpu.eip=p.resolve("kernel32.dll",name);
    CHECK(p.dispatch_api());CHECK(p.cpu.r[wr::ESP]==sp && p.cpu.eip==pc);
    return p.cpu.r[wr::EAX];
}
void equal(const char* api,unsigned code,unsigned flags,unsigned actual,unsigned expected){
    if(actual!=expected)throw std::runtime_error(std::string(api)+" U+"+wr::hex(code)+
        " flags="+wr::hex(flags)+" guest="+wr::hex(actual)+" oracle="+wr::hex(expected));
}
#ifdef _WIN32
unsigned snapshot_differences=0;
void drift(const char* api,unsigned code,unsigned flags,unsigned current,unsigned recorded){
    if(current==recorded)return;
    ++snapshot_differences;
    if(snapshot_differences<=16)std::cout<<"NLS snapshot drift: "<<api<<" code="<<wr::hex(code)
        <<" flags="<<wr::hex(flags)<<" current="<<wr::hex(current)<<" recorded="<<wr::hex(recorded)<<'\n';
}
#endif
}
int main(){try{
    wr::Process p(step);p.memory.map(0x100000,0x10000,3);p.cpu.r[wr::ESP]=0x10f000;p.cpu.eip=0x401000;
    const wr::U32 src=0x101000,dst=0x102000,used=0x103000;unsigned comparisons=0;
    // The Windows profile intentionally delegates to the current host NLS APIs.
    // Its oracle must be a separate direct call, not a different OS's frozen data.
    // Linux's bounded profile must match the recorded corpus exactly. nls_data
    // independently verifies that the generated header reproduces that corpus.
    for(const auto& row:wr::nls_rows){
        p.memory.store(src,row.code,16);
        for(unsigned i=0;i<3;++i){
            auto flag=1u<<i;const std::array recorded{row.ctype1,row.ctype2,row.ctype3};
            unsigned expected=recorded[i];
#ifdef _WIN32
            wchar_t ch=wchar_t(row.code);WORD native{};CHECK(::GetStringTypeW(flag,&ch,1,&native));
            expected=native;drift("GetStringTypeW",row.code,flag,expected,recorded[i]);
#endif
            CHECK(call(p,"GetStringTypeW",{flag,src,1,dst})==1);
            equal("GetStringTypeW",row.code,flag,p.memory.load(dst,16),expected);++comparisons;
        }
        for(unsigned flag:{0x100u,0x200u}){
            unsigned expected=flag==0x100?row.lower:row.upper;
#ifdef _WIN32
            wchar_t ch=wchar_t(row.code),native[8]{};CHECK(::LCMapStringW(0x409,flag,&ch,1,native,8)==1);
            drift("LCMapStringW",row.code,flag,unsigned(native[0]),expected);expected=unsigned(native[0]);
#endif
            CHECK(call(p,"LCMapStringW",{0x409,flag,src,1,0,0})==1);
            CHECK(call(p,"LCMapStringW",{0x409,flag,src,1,dst,1})==1);
            equal("LCMapStringW",row.code,flag,p.memory.load(dst,16),expected);++comparisons;
        }
    }
    const std::array<wr::U32,4> flags{0,0x200,0x220,0x400};
    for(const auto& row:wr::nls_encodings){
        p.memory.store(src,row.code,16);
        for(unsigned i=0;i<4;++i){
            unsigned expected=row.value[i];bool substituted=row.substituted[i];
#ifdef _WIN32
            wchar_t ch=wchar_t(row.code);char result[8]{};BOOL native_used=FALSE;
            CHECK(::WideCharToMultiByte(1252,flags[i],&ch,1,result,8,nullptr,&native_used)==1);
            drift("WideCharToMultiByte",row.code,flags[i],static_cast<unsigned char>(result[0]),expected);
            drift("WideCharToMultiByte.used",row.code,flags[i],native_used!=FALSE,substituted);
            expected=static_cast<unsigned char>(result[0]);substituted=native_used!=FALSE;
#endif
            p.memory.store(used,0xcccccccc,32);
            CHECK(call(p,"WideCharToMultiByte",{1252,flags[i],src,1,dst,1,0,used})==1);
            equal("WideCharToMultiByte",row.code,flags[i],p.memory.load(dst,8),expected);
            equal("WideCharToMultiByte.used",row.code,flags[i],p.memory.load(used,32),unsigned(substituted));++comparisons;
        }
    }
    // Explicit-length bulk conversion includes embedded NULs; never use strlen.
    for(unsigned n=0;n<wr::nls_encodings.size();++n)p.memory.store(src+n*2,wr::nls_encodings[n].code,16);
    for(unsigned i=0;i<4;++i){
        auto count=wr::U32(wr::nls_encodings.size());std::vector<unsigned char> expected(count);bool any=false;
        for(unsigned n=0;n<count;++n){expected[n]=wr::nls_encodings[n].value[i];any|=wr::nls_encodings[n].substituted[i];}
#ifdef _WIN32
        std::wstring text;for(const auto& row:wr::nls_encodings)text.push_back(wchar_t(row.code));
        BOOL native_used=FALSE;
        CHECK(::WideCharToMultiByte(1252,flags[i],text.data(),int(count),
              reinterpret_cast<char*>(expected.data()),int(count),nullptr,&native_used)==int(count));
        any=native_used!=FALSE;
#endif
        CHECK(call(p,"WideCharToMultiByte",{1252,flags[i],src,count,dst,count,0,used})==count);
        for(unsigned n=0;n<count;++n)equal("WideCharToMultiByte.bulk",wr::nls_encodings[n].code,flags[i],p.memory.load(dst+n,8),expected[n]);
        equal("WideCharToMultiByte.bulk.used",0,flags[i],p.memory.load(used,32),unsigned(any));++comparisons;
    }
    std::cout<<comparisons<<" bounded NLS classification/casing/encoding checks passed";
#ifdef _WIN32
    std::cout<<" against direct Windows APIs; recorded-snapshot differences="<<snapshot_differences;
#else
    std::cout<<" against the immutable Windows-measured portable corpus";
#endif
    std::cout<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
