// Native dialog boundary tests; callback is a test double, not recovered game code.
#include "winrecomp/process.hpp"
#include "dialog_fixture.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
namespace {
unsigned checks{};
#define CHECK(v) do{++checks;if(!(v))throw std::runtime_error("dialog assertion: " #v);}while(0)
constexpr wr::U32 Entry=0x401000,Callback=0x401010,Nested=0x401020,Custom=WM_APP+55;
wr::Process* current{};
wr::U32 last_dialog{},last_control{},expected_result=209;
unsigned init_count{},commands{},depth{};
enum class Mode{normal,fault,nested,bad_output};Mode mode=Mode::normal;
wr::U32 api(wr::Process& p,const char* name,std::initializer_list<wr::U32> args={}){
    const auto saved=p.cpu;
    try{
        for(auto it=args.end();it!=args.begin();)wr::push(p.cpu,p.memory,*--it);
        wr::push(p.cpu,p.memory,saved.eip);p.cpu.eip=p.resolve("user32.dll",name);CHECK(p.cpu.eip);
        CHECK(p.dispatch_api());CHECK(p.cpu.r[wr::ESP]==saved.r[wr::ESP]);CHECK(p.cpu.eip==saved.eip);
        for(auto r:{wr::EBX,wr::ESI,wr::EDI,wr::EBP})CHECK(p.cpu.r[r]==saved.r[r]);
        return p.cpu.r[wr::EAX];
    }catch(...){p.cpu=saved;throw;}
}
bool step(wr::Cpu& cpu,wr::Memory& memory,std::uint64_t&){
    if(cpu.eip==Entry){cpu.eip=wr::pop(cpu,memory);return true;}
    if(cpu.eip!=Callback && cpu.eip!=Nested)return false;
    const auto procedure=cpu.eip;auto& p=*current;
    const auto h=memory.load(cpu.r[wr::ESP]+4,32),message=memory.load(cpu.r[wr::ESP]+8,32),wp=memory.load(cpu.r[wr::ESP]+12,32),lp=memory.load(cpu.r[wr::ESP]+16,32);
    wr::U32 result=0;
    if(message==WM_INITDIALOG){
        ++init_count;++depth;CHECK(lp==0xabcdef12);CHECK(api(p,"IsWindow",{h})==1);
        auto list=api(p,"GetDlgItem",{h,1001});CHECK(list && list!=h);CHECK(api(p,"IsWindow",{list})==1);
        if(procedure==Nested){CHECK(depth==2);CHECK(api(p,"EndDialog",{h,0xfffffffeu}));--depth;}
        else {
            last_dialog=h;last_control=list;
            if(mode==Mode::fault)throw wr::GuestFault(wr::FaultKind::memory,lp,"deliberate dialog callback failure");
            CHECK(FindWindowA("#32770","WinRecomp modal fixture")!=nullptr);
            CHECK(api(p,"GetWindowLongA",{h,4})==Callback);
            CHECK(api(p,"SetWindowLongA",{h,8,0x87654321})==0);
            CHECK(api(p,"GetWindowLongA",{h,8})==0x87654321);
            CHECK(api(p,"GetWindowLongA",{h,wr::U32(-6)})==p.image_base);
            auto text=p.put_string("Rasterizer text");CHECK(api(p,"SendMessageA",{list,LB_ADDSTRING,0,text})==0);
            CHECK(api(p,"SendMessageA",{list,LB_SETITEMDATA,0,0xfedcba98})!=wr::U32(LB_ERR));
            CHECK(api(p,"SendMessageA",{list,LB_GETITEMDATA,0,0})==0xfedcba98);
            CHECK(api(p,"SendMessageA",{list,LB_SETCURSEL,0,0})==0);CHECK(api(p,"SendMessageA",{list,LB_GETCURSEL,0,0})==0);
            auto buffer=p.allocate_bytes(8192);p.memory.protect(buffer+4096,4096,wr::Memory::Read);
            auto end=buffer+4096;memory.store(end-20,0xa5a5a5a5,32);
            if(mode==Mode::bad_output)api(p,"SendMessageA",{list,LB_GETTEXT,0,end-4});
            CHECK(api(p,"SendMessageA",{list,LB_GETTEXT,0,end-16})==15);CHECK(p.read_string(end-16)=="Rasterizer text");CHECK(memory.load(end-20,32)==0xa5a5a5a5);
            CHECK(api(p,"CheckDlgButton",{h,1002,BST_CHECKED}));CHECK(api(p,"IsDlgButtonChecked",{h,1002})==BST_CHECKED);
            CHECK(api(p,"CheckRadioButton",{h,1003,1004,1004}));CHECK(api(p,"IsDlgButtonChecked",{h,1004})==BST_CHECKED);CHECK(api(p,"IsDlgButtonChecked",{h,1003})==BST_UNCHECKED);
            if(mode==Mode::nested){CHECK(api(p,"DialogBoxParamA",{p.image_base,101,h,Nested,0xabcdef12})==0xfffffffeu);CHECK(api(p,"IsWindow",{h})==1);CHECK(api(p,"IsWindow",{list})==1);}
            CHECK(api(p,"SendMessageA",{h,Custom,0,0})==0x23456789);
            auto button=api(p,"GetDlgItem",{h,IDOK});CHECK(button);
            // The modal manager must actually pump and dispatch this command.
            CHECK(api(p,"PostMessageA",{h,WM_COMMAND,IDOK,button}));--depth;
        }
        result=1;
    }else if(message==Custom){api(p,"SetWindowLongA",{h,0,0x23456789});result=1;}
    else if(message==WM_COMMAND && (wp&0xffff)==IDOK){++commands;CHECK(lp==api(p,"GetDlgItem",{h,IDOK}));CHECK(api(p,"EndDialog",{h,expected_result}));result=1;}
    cpu.r[wr::EAX]=result;cpu.eip=wr::pop(cpu,memory);cpu.r[wr::ESP]+=16;return true;
}
void run(Mode m,wr::U32 result){
    mode=m;depth=init_count=commands=0;expected_result=result;
    wr::Process p{step};current=&p;p.load(dialog_test::image());
    auto regions=p.memory.regions().size();
    CHECK(api(p,"DialogBoxParamA",{p.image_base,999,0,Callback,0})==0xffffffffu && p.last_error==1814);
    CHECK(init_count==0);
    CHECK(api(p,"DialogBoxParamA",{p.image_base,101,0x12345678,Callback,0})==0 && p.last_error==1400);
    CHECK(init_count==0);
    bool failed=false;try{CHECK(api(p,"DialogBoxParamA",{p.image_base,101,0,Callback,0xabcdef12})==result);}catch(const wr::GuestFault& e){std::cerr<<e.what()<<std::endl;CHECK(m==Mode::fault || m==Mode::bad_output);CHECK(e.kind==wr::FaultKind::memory);failed=true;}
    if(m==Mode::fault || m==Mode::bad_output){CHECK(failed);CHECK(!FindWindowA("#32770","WinRecomp modal fixture"));return;}
    CHECK(!failed && commands==1 && init_count==(m==Mode::nested?2u:1u));
    CHECK(!FindWindowA("#32770","WinRecomp modal fixture"));
    CHECK(api(p,"IsWindow",{last_dialog})==0);CHECK(api(p,"IsWindow",{last_control})==0);
    CHECK(api(p,"GetDlgItem",{last_dialog,1001})==0 && p.last_error==1400);
    CHECK(api(p,"EndDialog",{last_dialog,1})==0 && p.last_error==1400);
    CHECK(p.memory.regions().size()<regions+8);
}
}
int main(){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);try{
    for(auto r:{wr::U32(209),wr::U32(0),wr::U32(0x80000001)})run(Mode::normal,r);
    run(Mode::nested,209);run(Mode::fault,0);run(Mode::bad_output,0);
    std::cout<<checks<<" native modal-dialog assertions passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
