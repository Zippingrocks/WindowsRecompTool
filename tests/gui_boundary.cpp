// Native Win32 boundary regressions. The small callback below is a test double;
// independently generated machine-code WNDPROC coverage is in window_program.py.
#include "winrecomp/process.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
namespace {
unsigned checks=0;
#define CHECK(x) do{if(!(x))throw std::runtime_error("GUI assertion failed: " #x);++checks;}while(0)
constexpr wr::U32 Entry=0x401000, Callback=0x401010, Custom=WM_APP+21;
thread_local wr::Process* current=nullptr;
enum class Mode {normal,fault_on_create,fault_on_custom};
thread_local Mode mode=Mode::normal;
wr::Image image(){
    wr::Image im;im.base=0x400000;im.entry=Entry;im.size=0x3000;im.headers_size=512;
    im.bytes.resize(1536);im.sections={{".text",0x1000,512,512,512,0x60000020},{".data",0x2000,512,1024,512,0xc0000040}};
    im.bytes[512]=0xc3;im.bytes[528]=0xc3;return im;
}
bool step(wr::Cpu& cpu,wr::Memory& memory,std::uint64_t&){
    if(cpu.eip==Entry){cpu.eip=wr::pop(cpu,memory);return true;}
    if(cpu.eip!=Callback)return false;
    const auto message=memory.load(cpu.r[wr::ESP]+8,32);
    if((mode==Mode::fault_on_create && message==WM_CREATE) || (mode==Mode::fault_on_custom && message==Custom))
        throw wr::GuestFault(wr::FaultKind::unsupported,cpu.eip,"deliberate WNDPROC fault");
    if(message==Custom){
        cpu.r[wr::EAX]=memory.load(cpu.r[wr::ESP]+12,32)+memory.load(cpu.r[wr::ESP]+16,32);
        cpu.eip=wr::pop(cpu,memory);cpu.r[wr::ESP]+=16;return true;
    }
    // Tail-transfer to the real default-window API, with the same guest stack.
    cpu.eip=current->resolve("user32.dll","DefWindowProcA");return true;
}
wr::U32 invoke(wr::Process& p,const char* name,std::initializer_list<wr::U32> args={},const char* dll="user32.dll"){
    auto saved=p.cpu;
    try{
        for(auto it=args.end();it!=args.begin();)wr::push(p.cpu,p.memory,*--it);
        wr::push(p.cpu,p.memory,saved.eip);p.cpu.eip=p.resolve(dll,name);CHECK(p.cpu.eip);
        CHECK(p.dispatch_api());CHECK(p.cpu.r[wr::ESP]==saved.r[wr::ESP]);CHECK(p.cpu.eip==saved.eip);
        for(auto r:{wr::EBX,wr::ESI,wr::EDI,wr::EBP})CHECK(p.cpu.r[r]==saved.r[r]);
        return p.cpu.r[wr::EAX];
    }catch(...){
        // Test harness recovery only. Production guest faults are not resumed.
        p.cpu=saved;throw;
    }
}
template<class F>void fault(F f,wr::FaultKind expected){
    bool caught=false;try{f();}catch(const wr::GuestFault& e){caught=true;CHECK(e.kind==expected);}CHECK(caught);
}
struct TestWindow {
    wr::Process p{step};
    wr::U32 name{},title{},wc{},handle{};
    std::string title_text;
    explicit TestWindow(const std::string& tag,bool create=true){
        current=&p;p.load(image());title_text="WinRecomp boundary "+std::to_string(GetCurrentProcessId())+" "+tag;
        name=p.put_string("BoundaryClass-"+tag);title=p.put_string(title_text);wc=p.allocate_bytes(4096);
        std::array<wr::U32,12> fields{48,CS_OWNDC,Callback,0,4,p.image_base,0,0,0,0,name,0};
        for(unsigned i=0;i<fields.size();++i)p.memory.store(wc+4*i,fields[i],32);
        if(create){CHECK(invoke(p,"RegisterClassExA",{wc}));handle=make();CHECK(handle);}
    }
    wr::U32 make(){return invoke(p,"CreateWindowExA",{0,name,title,WS_OVERLAPPEDWINDOW,30,30,128,128,0,0,p.image_base,0});}
    HWND native() const{return FindWindowA(nullptr,title_text.c_str());}
    ~TestWindow(){current=&p;}
};
void basic(){
    mode=Mode::normal;TestWindow w("basic");auto& p=w.p;CHECK(w.native());
    auto icon=invoke(p,"LoadIconA",{0,32512});CHECK(icon);CHECK(invoke(p,"IsWindow",{icon})==0);
    CHECK(invoke(p,"ShowWindow",{icon,SW_HIDE})==0 && p.last_error==1400);
    CHECK(invoke(p,"ReleaseDC",{0,icon})==0 && p.last_error==6);
    CHECK(invoke(p,"LoadIconA",{p.image_base,999})==0 && p.last_error==1814);
    auto cursor=invoke(p,"LoadCursorA",{0,32512});CHECK(cursor);
    CHECK(invoke(p,"IsWindow",{cursor})==0);
    auto scratch=p.allocate_bytes(8192);
    p.memory.protect(scratch+4096,4096,wr::Memory::Read);
    auto rect=scratch+4096-16;p.memory.store(rect-4,0x11223344,32);
    CHECK(invoke(p,"GetClientRect",{w.handle,rect})==1);CHECK(p.memory.load(rect-4,32)==0x11223344);
    fault([&]{invoke(p,"GetClientRect",{w.handle,rect+4});},wr::FaultKind::memory);
    CHECK(invoke(p,"IsWindow",{w.handle})==1);
    auto msg=scratch+4096-28;p.memory.store(msg-4,0x55667788,32);
    CHECK(invoke(p,"PostMessageA",{w.handle,Custom,23,31}));
    CHECK(invoke(p,"PeekMessageA",{msg,w.handle,Custom,Custom,PM_NOREMOVE})==1);
    std::array<wr::U32,7> first{};for(unsigned i=0;i<7;++i)first[i]=p.memory.load(msg+4*i,32);
    CHECK(first[0]==w.handle && first[1]==Custom && first[2]==23 && first[3]==31);
    CHECK(invoke(p,"PeekMessageA",{msg,w.handle,Custom,Custom,PM_NOREMOVE})==1);
    for(unsigned i=0;i<7;++i)CHECK(p.memory.load(msg+4*i,32)==first[i]);
    CHECK(invoke(p,"PeekMessageA",{msg,w.handle,Custom,Custom,PM_REMOVE})==1);
    CHECK(invoke(p,"DispatchMessageA",{msg})==54);
    CHECK(invoke(p,"PeekMessageA",{msg,w.handle,Custom,Custom,PM_REMOVE})==0);
    CHECK(p.memory.load(msg-4,32)==0x55667788);
    fault([&]{invoke(p,"PeekMessageA",{msg+4,w.handle,Custom,Custom,PM_REMOVE});},wr::FaultKind::memory);
    fault([&]{invoke(p,"SendMessageA",{w.handle,WM_COPYDATA,0,scratch});},wr::FaultKind::unsupported);
    fault([&]{invoke(p,"SendMessageA",{w.handle,WM_TIMER,1,Callback});},wr::FaultKind::unsupported);
    fault([&]{invoke(p,"DefWindowProcA",{w.handle,WM_SETTEXT,0,w.title});},wr::FaultKind::unsupported);
    CHECK(invoke(p,"SetWindowLongA",{w.handle,wr::U32(-21),0xabcdef12})==0);
    CHECK(invoke(p,"GetWindowLongA",{w.handle,wr::U32(-21)})==0xabcdef12);
    CHECK(invoke(p,"SetWindowLongA",{w.handle,0,0x13572468})==0);
    CHECK(invoke(p,"GetWindowLongA",{w.handle,0})==0x13572468);
    auto hdc=invoke(p,"GetDC",{w.handle});CHECK(hdc);CHECK(invoke(p,"IsWindow",{hdc})==0);
    CHECK(invoke(p,"ReleaseDC",{w.handle,hdc}));CHECK(invoke(p,"ReleaseDC",{w.handle,hdc})==0);
    fault([&]{invoke(p,"glClear",{0x4000},"opengl32.dll");},wr::FaultKind::unsupported);
    auto rendering_dc=invoke(p,"GetDC",{w.handle});CHECK(rendering_dc);
    PIXELFORMATDESCRIPTOR descriptor{};descriptor.nSize=40;descriptor.nVersion=1;
    descriptor.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
    descriptor.iPixelType=PFD_TYPE_RGBA;descriptor.cColorBits=24;descriptor.cDepthBits=24;
    p.memory.copy_in(scratch,std::span(reinterpret_cast<const std::uint8_t*>(&descriptor),40));
    auto format=invoke(p,"ChoosePixelFormat",{rendering_dc,scratch},"gdi32.dll");CHECK(format);
    CHECK(invoke(p,"SetPixelFormat",{rendering_dc,format,scratch},"gdi32.dll"));
    auto context=invoke(p,"wglCreateContext",{rendering_dc},"opengl32.dll");CHECK(context);
    CHECK(invoke(p,"wglMakeCurrent",{rendering_dc,context},"opengl32.dll"));
    for(const auto* symbol:{"WinRecompMissingProcedure","glGetString","glAddSwapHintRectWIN","wglGetExtensionsStringARB"}){
        const auto native_address=reinterpret_cast<std::uintptr_t>(::wglGetProcAddress(symbol));
        auto name=p.put_string(symbol);
        if(native_address<=3 || native_address==~std::uintptr_t(0))
            CHECK(invoke(p,"wglGetProcAddress",{name},"opengl32.dll")==0);
        else if(!p.resolve("opengl32.dll",symbol))
            fault([&]{invoke(p,"wglGetProcAddress",{name},"opengl32.dll");},wr::FaultKind::unsupported);
        else CHECK(invoke(p,"wglGetProcAddress",{name},"opengl32.dll")==p.resolve("opengl32.dll",symbol));
    }
    auto vendor=invoke(p,"glGetString",{GL_VENDOR},"opengl32.dll");CHECK(vendor);
    CHECK(p.read_string(vendor)==reinterpret_cast<const char*>(::glGetString(GL_VENDOR)));
    fault([&]{p.memory.store(vendor,0,8);},wr::FaultKind::memory);
    CHECK(invoke(p,"wglMakeCurrent",{0,0},"opengl32.dll"));
    CHECK(invoke(p,"wglDeleteContext",{context},"opengl32.dll"));
    CHECK(invoke(p,"ReleaseDC",{w.handle,rendering_dc}));
    bool thread_rejected=false;std::thread other([&]{try{invoke(p,"GetSystemMetrics",{0});}catch(const wr::GuestFault& e){thread_rejected=e.kind==wr::FaultKind::unsupported;}});other.join();CHECK(thread_rejected);
    CHECK(invoke(p,"SendMessageA",{w.handle,WM_CLOSE,0,0})==0);
    CHECK(!w.native());CHECK(invoke(p,"IsWindow",{w.handle})==0);
    CHECK(invoke(p,"ShowWindow",{w.handle,SW_SHOW})==0 && p.last_error==1400);
    CHECK(invoke(p,"UnregisterClassA",{w.name,p.image_base})==1);
}
void class_errors(){
    mode=Mode::normal;TestWindow w("classes",false);auto& p=w.p;
    p.memory.store(w.wc,40,32);CHECK(invoke(p,"RegisterClassExA",{w.wc})==0 && p.last_error==87);
    p.memory.store(w.wc,48,32);p.memory.store(w.wc+40,1,32);CHECK(invoke(p,"RegisterClassExA",{w.wc})==0 && p.last_error==87);
    p.memory.store(w.wc+40,0x80000000,32);fault([&]{invoke(p,"RegisterClassExA",{w.wc});},wr::FaultKind::memory);
    p.memory.store(w.wc+40,w.name,32);CHECK(invoke(p,"RegisterClassExA",{w.wc}));
    CHECK(invoke(p,"RegisterClassExA",{w.wc})==0 && p.last_error==1410);
    CHECK(invoke(p,"CreateWindowExA",{0,p.put_string("missing-class"),w.title,0,0,0,100,100,0,0,p.image_base,0})==0 && p.last_error==1407);
    fault([&]{invoke(p,"CreateWindowExA",{0,w.name,w.title,WS_CHILD,0,0,100,100,0,0,p.image_base,0});},wr::FaultKind::unsupported);
    CHECK(invoke(p,"UnregisterClassA",{w.name,p.image_base}));
}
void callback_errors(){
    mode=Mode::fault_on_create;
    {TestWindow w("creation-fault",false);CHECK(invoke(w.p,"RegisterClassExA",{w.wc}));fault([&]{w.make();},wr::FaultKind::unsupported);CHECK(!w.native());}
    mode=Mode::normal;
    std::string retired;
    {TestWindow w("guest-fault");retired=w.title_text;mode=Mode::fault_on_custom;
     fault([&]{invoke(w.p,"SendMessageA",{w.handle,Custom,0,0});},wr::FaultKind::unsupported);
     // The first failure remains sticky; it is never converted to success.
     fault([&]{invoke(w.p,"GetSystemMetrics",{0});},wr::FaultKind::unsupported);}
    CHECK(!FindWindowA(nullptr,retired.c_str()));mode=Mode::normal;
    {TestWindow w("host-pointer");retired=w.title_text;CHECK(w.native());
     // The native ABI returns without letting C++ unwind through USER32.
     SendMessageA(w.native(),Custom,WPARAM(0x123456789000ull),0);
     bool failed=false;try{invoke(w.p,"GetSystemMetrics",{0});}catch(const std::runtime_error& e){failed=std::string(e.what()).find("32-bit scalar")!=std::string::npos;}CHECK(failed);}
    CHECK(!FindWindowA(nullptr,retired.c_str()));
}
}
int main(){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);try{
    basic();class_errors();callback_errors();
    std::cout<<checks<<" native GUI boundary assertions passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
