#include "winrecomp/gui.hpp"
#include "winrecomp/process.hpp"
#include "winrecomp/win32_state.hpp"
#include <atomic>
#include <cstring>
#include <bit>
#include <exception>
#include <sstream>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace wr {
#ifdef _WIN32
struct GuiClass { std::string guest_name,native_name;U32 guest_atom{},proc{},instance{},style{};ATOM atom{}; };
struct GuiWindow {
    Process* process{};std::shared_ptr<GuiClass> cls;HWND hwnd{};U32 id{},proc{},userdata{};
    std::array<U32,12> create{};bool dead{};
    struct Paint {PAINTSTRUCT native{};U32 pointer{},dc{};};
    std::optional<Paint> paint;
};
struct GuiFrame;
#endif
struct GuiState {
    std::thread::id owner=std::this_thread::get_id();
    std::uint64_t windows_created{},callbacks{},dispatched{},paints{};
    bool shutting_down{};
#ifdef _WIN32
    enum class Kind {window,icon,cursor,brush,dc,region};
    struct Handle {Kind kind;std::uintptr_t native;bool owned{};};
    U32 next_handle{0xb0000000},next_atom{0xa000};
    std::map<U32,Handle> handles;
    std::map<U32,std::shared_ptr<GuiWindow>> windows;
    std::map<std::string,std::shared_ptr<GuiClass>> classes;
    std::map<std::pair<U32,U32>,U32> shared_icons;
    std::vector<GuiFrame*> frames;
    std::exception_ptr pending;
    int cursor_balance{};
#endif
};
namespace {
[[noreturn]] void unsupported(Process& p,const std::string& text){throw GuestFault(FaultKind::unsupported,p.cpu.eip,text);}
GuiState& gui(Process& p){
    if(!p.options.enable_gui)unsupported(p,"native GUI requires explicit --gui opt-in");
#ifndef _WIN32
    unsupported(p,"native GUI backend requires Windows; no headless success stub is used");
#endif
    if(!p.state().gui)p.state().gui=std::make_shared<GuiState>();
    auto& g=*p.state().gui;
    if(g.owner!=std::this_thread::get_id())unsupported(p,"GUI calls must stay on their owning thread");
    return g;
}
U32 arg(Process& p,unsigned n){return p.argument(n);}
#ifdef _WIN32
using Kind=GuiState::Kind;
thread_local std::vector<GuiWindow*> creating;
int signed32(U32 value){return std::bit_cast<std::int32_t>(value);}
U32 scalar(Process& p,std::uintptr_t value){
    if(value>0xffffffffull && value<0xffffffff80000000ull)unsupported(p,"native pointer-sized message value cannot enter 32-bit guest state");
    return U32(value);
}
U32 token(GuiState& g,Kind kind,std::uintptr_t handle,bool owned=false){
    if(!handle)return 0;
    for(auto& [id,item]:g.handles)if(item.kind==kind && item.native==handle){item.owned|=owned;return id;}
    if(g.next_handle>=0xbf000000u)throw std::runtime_error("GUI handle table exhausted");
    auto id=g.next_handle;g.next_handle+=16;g.handles.emplace(id,GuiState::Handle{kind,handle,owned});return id;
}
template<class T> T native(Process& p,U32 value,Kind kind){
    if(!value)return nullptr;auto& g=gui(p);auto it=g.handles.find(value);
    if(it==g.handles.end() || it->second.kind!=kind)unsupported(p,"invalid or wrong-kind GUI handle "+hex(value));
    return reinterpret_cast<T>(it->second.native);
}
U32 window_token(GuiState& g,HWND h){return token(g,Kind::window,reinterpret_cast<std::uintptr_t>(h));}
std::shared_ptr<GuiWindow> owned_window(Process& p,U32 id){
    auto& g=gui(p);auto it=g.windows.find(id);
    if(it==g.windows.end() || it->second->dead)unsupported(p,"window is stale or not owned by this guest process");
    return it->second;
}
void rethrow(GuiState& g){if(g.pending)std::rethrow_exception(g.pending);}
HWND zorder(Process& p,U32 id){
    if(id==0 || id==1 || id==0xffffffffu || id==0xfffffffeu)return reinterpret_cast<HWND>(std::intptr_t(signed32(id)));
    return native<HWND>(p,id,Kind::window);
}
U32 ztoken(GuiState& g,HWND h){auto n=reinterpret_cast<std::intptr_t>(h);if(n>=-2 && n<=1)return U32(n);return window_token(g,h);}
ResourceName resource_name(Process& p,U32 value){
    if(value<=0xffffu)return value;
    const auto text=p.read_string(value);std::u16string wide;
    // ANSI resource names in this first boundary are ASCII; do not silently
    // reinterpret multi-byte names using the host locale.
    for(unsigned char c:text){if(c>=128)unsupported(p,"non-ASCII ANSI resource names are not yet supported");wide.push_back(c);}
    return wide;
}
U32 load_icon(Process& p){
    auto& g=gui(p);U32 module=arg(p,0),name=arg(p,1);const auto key=std::pair{module,name};
    if(auto it=g.shared_icons.find(key);it!=g.shared_icons.end())return it->second;
    HICON icon{};bool owned=false;
    if(!module){if(name>65535)unsupported(p,"stock icon requires an integer resource id");icon=::LoadIconA(nullptr,MAKEINTRESOURCEA(name));}
    else {
        if(module!=p.image_base)unsupported(p,"icon resource module is not the loaded guest image");
        auto* group=p.state().resources.find(14u,resource_name(p,name));
        if(!group){p.set_error(ERROR_RESOURCE_NAME_NOT_FOUND);return 0;}
        auto bytes=group->bytes;
        auto u16=[&](std::size_t n){return unsigned(bytes.at(n))|(unsigned(bytes.at(n+1))<<8);};
        if(bytes.size()<6 || u16(0)!=0 || u16(2)!=1 || !u16(4) || u16(4)>256 || bytes.size()<6+14*u16(4))unsupported(p,"invalid group-icon resource");
        int id=::LookupIconIdFromDirectoryEx(bytes.data(),TRUE,::GetSystemMetrics(SM_CXICON),::GetSystemMetrics(SM_CYICON),LR_DEFAULTCOLOR);
        auto* image=p.state().resources.find(3u,U32(id),group->language);
        if(!id || !image){p.set_error(ERROR_RESOURCE_DATA_NOT_FOUND);return 0;}
        // Bounded, DWORD-aligned staging; no pointer to guest memory reaches USER32.
        std::vector<DWORD> aligned((image->bytes.size()+3)/4);
        if(image->bytes.empty() || image->bytes.size()>16u*1024*1024)unsupported(p,"invalid icon image resource size");
        std::memcpy(aligned.data(),image->bytes.data(),image->bytes.size());
        icon=::CreateIconFromResourceEx(reinterpret_cast<PBYTE>(aligned.data()),DWORD(image->bytes.size()),TRUE,0x00030000,
             ::GetSystemMetrics(SM_CXICON),::GetSystemMetrics(SM_CYICON),LR_DEFAULTCOLOR);
        owned=true;
    }
    if(!icon){p.set_error(::GetLastError());return 0;}
    auto id=token(g,Kind::icon,reinterpret_cast<std::uintptr_t>(icon),owned);g.shared_icons.emplace(key,id);return id;
}

// Every frame owns temporary 32-bit guest-stack storage. It synchronizes known
// pointer structures in BOTH directions around guest and DefWindowProc calls.
// Nothing can throw through a native WNDPROC: the outer bridge stores failures.
struct GuiFrame {
    Process& p;GuiState& g;GuiWindow& w;UINT message;WPARAM nw;LPARAM nl;
    U32 wp{},lp{},sp;std::vector<std::function<void()>> into_native,into_guest;
    GuiFrame(GuiWindow& window,UINT msg,WPARAM a,LPARAM b):p(*window.process),g(*p.state().gui),w(window),message(msg),nw(a),nl(b),sp(p.cpu.r[ESP]){g.frames.push_back(this);}
    ~GuiFrame(){p.cpu.r[ESP]=sp;g.frames.pop_back();}
    U32 alloc(std::size_t n){
        if(n>65536 || p.cpu.r[ESP]<n+16)unsupported(p,"window marshalling stack bound");
        const U32 at=(p.cpu.r[ESP]-U32(n))&~15u;p.memory.check(at,n,Memory::Write);p.cpu.r[ESP]=at;
        std::vector<std::uint8_t> zero(n);p.memory.copy_in(at,zero);return at;
    }
    void words(U32 at,std::span<const U32> values){for(std::size_t k=0;k<values.size();++k)p.memory.store(at+U32(4*k),values[k],32);}
    U32 raw(void* pointer,std::size_t size){
        const auto at=alloc(size);
        into_guest.push_back([this,pointer,size,at]{p.memory.copy_in(at,std::span(reinterpret_cast<const std::uint8_t*>(pointer),size));});
        into_native.push_back([this,pointer,size,at]{auto bytes=p.memory.copy_out(at,size);std::memcpy(pointer,bytes.data(),size);});
        return at;
    }
    U32 position(WINDOWPOS* v){
        const auto at=alloc(28);
        into_guest.push_back([this,v,at]{std::array<U32,7> a{window_token(g,v->hwnd),ztoken(g,v->hwndInsertAfter),U32(v->x),U32(v->y),U32(v->cx),U32(v->cy),v->flags};words(at,a);});
        into_native.push_back([this,v,at]{if(p.memory.load(at,32)!=window_token(g,v->hwnd))unsupported(p,"WINDOWPOS hwnd mutation is not supported");v->hwndInsertAfter=zorder(p,p.memory.load(at+4,32));v->x=signed32(p.memory.load(at+8,32));v->y=signed32(p.memory.load(at+12,32));v->cx=signed32(p.memory.load(at+16,32));v->cy=signed32(p.memory.load(at+20,32));v->flags=p.memory.load(at+24,32);});
        return at;
    }
    void prepare(){
        if(message==WM_NCPAINT || message==WM_ERASEBKGND || message==WM_SETCURSOR || message==WM_SETFOCUS || message==WM_KILLFOCUS)wp=0;
        else wp=scalar(p,nw);
        switch(message){
        case WM_NCCREATE:case WM_CREATE:{
            auto* c=reinterpret_cast<CREATESTRUCTA*>(nl);lp=alloc(48);
            std::array<U32,12> a{w.create[11],w.create[10],w.create[9],w.create[8],U32(c->cy),U32(c->cx),U32(c->y),U32(c->x),U32(c->style),w.create[2],w.create[1],c->dwExStyle};words(lp,a);break;
        }
        case WM_GETMINMAXINFO:lp=raw(reinterpret_cast<void*>(nl),40);break;
        case WM_WINDOWPOSCHANGING:case WM_WINDOWPOSCHANGED:lp=position(reinterpret_cast<WINDOWPOS*>(nl));break;
        case WM_NCCALCSIZE:
            if(!nw)lp=raw(reinterpret_cast<void*>(nl),16);
            else {auto* c=reinterpret_cast<NCCALCSIZE_PARAMS*>(nl);lp=alloc(52);auto pos=position(c->lppos);
                into_guest.push_back([this,c,pos]{p.memory.copy_in(lp,std::span(reinterpret_cast<const std::uint8_t*>(c->rgrc),48));p.memory.store(lp+48,pos,32);});
                into_native.push_back([this,c,pos]{if(p.memory.load(lp+48,32)!=pos)unsupported(p,"NCCALCSIZE pointer mutation is not supported");auto bytes=p.memory.copy_out(lp,48);std::memcpy(c->rgrc,bytes.data(),48);});}
            break;
        case WM_SETTEXT:{auto* text=reinterpret_cast<const char*>(nl);std::size_t length{};if(text){while(length<32768 && text[length])++length;if(length==32768)unsupported(p,"oversized native window text");lp=alloc(length+1);p.memory.copy_in(lp,std::span(reinterpret_cast<const std::uint8_t*>(text),length+1));}break;}
        case WM_GETTEXT:{if(nw>32768)unsupported(p,"oversized native window text request");if(nw)std::memset(reinterpret_cast<void*>(nl),0,std::size_t(nw));lp=nw?raw(reinterpret_cast<void*>(nl),std::size_t(nw)):0;break;}
        case WM_SETCURSOR:wp=window_token(g,reinterpret_cast<HWND>(nw));lp=scalar(p,std::uintptr_t(nl));break;
        case WM_SETFOCUS:case WM_KILLFOCUS:wp=window_token(g,reinterpret_cast<HWND>(nw));lp=0;break;
        case WM_ACTIVATE:case WM_CAPTURECHANGED:lp=window_token(g,reinterpret_cast<HWND>(nl));break;
        case WM_NCACTIVATE:lp=(nl==0 || nl==-1)?U32(nl):window_token(g,reinterpret_cast<HWND>(nl));break;
        case WM_ERASEBKGND:wp=token(g,Kind::dc,nw);lp=0;break;
        case WM_NCPAINT:wp=nw==1?1:token(g,Kind::region,nw);lp=0;break;
        case WM_SETICON:lp=token(g,Kind::icon,std::uintptr_t(nl));break;
        default:
            if(!is_scalar(message))unsupported(p,"unmarshalled window message "+hex(message));
            lp=scalar(p,std::uintptr_t(nl));break;
        }
        sync_guest();
    }
    static bool is_scalar(UINT m){
        if(m>=WM_APP && m<=0xbfff)return true;
        if(m>=WM_KEYFIRST && m<=WM_KEYLAST)return true;
        if(m>=WM_MOUSEFIRST && m<=WM_MOUSELAST)return true;
        switch(m){
        case WM_NULL:case WM_DESTROY:case WM_MOVE:case WM_SIZE:case WM_ENABLE:case WM_SETREDRAW:
        case WM_PAINT:case WM_CLOSE:case WM_QUIT:case WM_QUERYOPEN:case WM_SHOWWINDOW:
        case WM_ACTIVATEAPP:case WM_CANCELMODE:case WM_GETTEXTLENGTH:case WM_NCDESTROY:
        case WM_NCHITTEST:case WM_NCACTIVATE:case WM_GETICON:case WM_SYSCOMMAND:
        case WM_ENTERSIZEMOVE:case WM_EXITSIZEMOVE:
        case WM_THEMECHANGED:case 0x031f:case WM_IME_SETCONTEXT:case WM_IME_NOTIFY:
            return m!=WM_CAPTURECHANGED;
        default:return false;
        }
    }
    void sync_native(){for(auto& f:into_native)f();}
    void sync_guest(){for(auto& f:into_guest)f();}
    U32 guest_result(LRESULT value){if(message==WM_GETICON || message==WM_SETICON)return token(g,Kind::icon,std::uintptr_t(value));return scalar(p,std::uintptr_t(value));}
    LRESULT native_result(U32 value){if(message==WM_GETICON || message==WM_SETICON)return reinterpret_cast<LRESULT>(native<HICON>(p,value,Kind::icon));return LRESULT(signed32(value));}
};
LRESULT CALLBACK bridge(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) noexcept {
    auto* w=reinterpret_cast<GuiWindow*>(::GetWindowLongPtrA(hwnd,GWLP_USERDATA));
    if(!w && !creating.empty()){
        auto* candidate=creating.back();
        if(ATOM(::GetClassLongPtrA(hwnd,GCW_ATOM))==candidate->cls->atom){w=candidate;w->hwnd=hwnd;::SetWindowLongPtrA(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(w));}
    }
    if(!w)return ::DefWindowProcA(hwnd,message,wp,lp);
    auto& p=*w->process;auto& g=*p.state().gui;
    if(g.shutting_down || g.pending || p.exited()){if(message==WM_NCDESTROY){w->dead=true;::SetWindowLongPtrA(hwnd,GWLP_USERDATA,0);}return message==WM_CREATE?-1:0;}
    try {
        if(g.owner!=std::this_thread::get_id())unsupported(p,"cross-thread native window callback is unsupported");
        g.handles.at(w->id).native=reinterpret_cast<std::uintptr_t>(hwnd);
        GuiFrame frame(*w,message,wp,lp);frame.prepare();++g.callbacks;
        const std::array<U32,4> args{w->id,message,frame.wp,frame.lp};
        auto value=p.callback(w->proc,args);frame.sync_native();auto result=frame.native_result(value);
        if(message==WM_NCDESTROY){w->dead=true;::SetWindowLongPtrA(hwnd,GWLP_USERDATA,0);}
        return result;
    }catch(...){if(!g.pending)g.pending=std::current_exception();return message==WM_CREATE?-1:0;}
}
std::shared_ptr<GuiClass> find_class(Process& p,U32 name){
    auto& g=gui(p);
    if(name<=65535){for(auto& [text,c]:g.classes)if(c->guest_atom==name)return c;}
    else {auto key=p.read_string(name);auto it=g.classes.find(key);if(it!=g.classes.end())return it->second;}
    return {};
}
U32 register_class(Process& p,bool extended){
    auto& g=gui(p);auto at=arg(p,0);p.memory.check(at,extended?48:40,Memory::Read);
    if(extended && p.memory.load(at,32)!=48){p.set_error(ERROR_INVALID_PARAMETER);return 0;}
    auto base=at+(extended?4:0);auto load=[&](unsigned n){return p.memory.load(base+n*4,32);};
    if(load(2) || load(3) || load(8))unsupported(p,"class extra bytes and class menus are not yet supported");
    if(load(4)!=p.image_base)unsupported(p,"window class instance must be the guest image");
    const auto name=p.read_string(load(9),256);if(name.empty()){p.set_error(ERROR_INVALID_PARAMETER);return 0;}
    if(g.classes.contains(name)){p.set_error(ERROR_CLASS_ALREADY_EXISTS);return 0;}
    p.memory.check(load(1),1,Memory::Execute);
    auto c=std::make_shared<GuiClass>();c->guest_name=name;c->guest_atom=g.next_atom++;c->proc=load(1);c->instance=load(4);c->style=load(0);
    static std::atomic<unsigned> serial{0};c->native_name="WinRecomp_"+std::to_string(::GetCurrentProcessId())+"_"+std::to_string(++serial);
    WNDCLASSEXA wc{};wc.cbSize=sizeof(wc);wc.style=c->style;wc.lpfnWndProc=bridge;wc.hInstance=::GetModuleHandleW(nullptr);wc.lpszClassName=c->native_name.c_str();
    wc.hIcon=native<HICON>(p,load(5),Kind::icon);wc.hCursor=native<HCURSOR>(p,load(6),Kind::cursor);
    auto brush=load(7);wc.hbrBackground=brush<=COLOR_MENUBAR+1?reinterpret_cast<HBRUSH>(std::uintptr_t(brush)):native<HBRUSH>(p,brush,Kind::brush);
    if(extended)wc.hIconSm=native<HICON>(p,p.memory.load(at+44,32),Kind::icon);
    c->atom=::RegisterClassExA(&wc);if(!c->atom){p.set_error(::GetLastError());return 0;}
    g.classes.emplace(name,c);return c->guest_atom;
}
U32 create_window(Process& p){
    auto& g=gui(p);auto c=find_class(p,arg(p,1));if(!c){p.set_error(ERROR_CANNOT_FIND_WND_CLASS);return 0;}
    if(arg(p,10)!=c->instance)unsupported(p,"CreateWindowExA instance differs from its registered guest class");
    if(arg(p,8) || arg(p,9))unsupported(p,"child/owned windows and menus are not yet implemented");
    auto w=std::make_shared<GuiWindow>();w->process=&p;w->cls=c;w->proc=c->proc;
    for(unsigned n=0;n<12;++n)w->create[n]=arg(p,n);
    const auto text=arg(p,2)?p.read_string(arg(p,2)):std::string();
    w->id=g.next_handle;g.next_handle+=16;g.handles.emplace(w->id,GuiState::Handle{Kind::window,0,false});g.windows.emplace(w->id,w);
    creating.push_back(w.get());
    const auto hwnd=::CreateWindowExA(arg(p,0),c->native_name.c_str(),text.c_str(),arg(p,3),signed32(arg(p,4)),signed32(arg(p,5)),signed32(arg(p,6)),signed32(arg(p,7)),nullptr,nullptr,::GetModuleHandleW(nullptr),w.get());
    const auto error=::GetLastError();creating.pop_back();
    if(!hwnd){w->dead=true;rethrow(g);p.set_error(error);return 0;}
    w->hwnd=hwnd;g.handles.at(w->id).native=reinterpret_cast<std::uintptr_t>(hwnd);++g.windows_created;rethrow(g);return w->id;
}
U32 default_proc(Process& p){
    auto& g=gui(p);const auto window=owned_window(p,arg(p,0));auto msg=arg(p,1),wp=arg(p,2),lp=arg(p,3);
    for(auto it=g.frames.rbegin();it!=g.frames.rend();++it){auto& f=**it;if(f.w.id==window->id && f.message==msg && f.wp==wp && f.lp==lp){f.sync_native();auto result=::DefWindowProcA(window->hwnd,msg,f.nw,f.nl);rethrow(g);f.sync_guest();return f.guest_result(result);}}
    if(!GuiFrame::is_scalar(msg))unsupported(p,"DefWindowProcA pointer message outside its native marshalling frame");
    auto result=::DefWindowProcA(window->hwnd,msg,wp,LPARAM(signed32(lp)));rethrow(g);return scalar(p,std::uintptr_t(result));
}
void store_msg(Process& p,U32 at,const MSG& m){
    auto& g=gui(p);if(!GuiFrame::is_scalar(m.message))unsupported(p,"unmarshalled queued window message "+hex(m.message));
    const std::array<U32,7> values{window_token(g,m.hwnd),m.message,scalar(p,m.wParam),scalar(p,std::uintptr_t(m.lParam)),m.time,U32(m.pt.x),U32(m.pt.y)};
    for(unsigned n=0;n<7;++n)p.memory.store(at+4*n,values[n],32);
}
MSG read_msg(Process& p,U32 at){
    p.memory.check(at,28,Memory::Read);auto read=[&](unsigned n){return p.memory.load(at+4*n,32);};
    const auto message=read(1);if(!GuiFrame::is_scalar(message))unsupported(p,"unmarshalled guest MSG message "+hex(message));
    MSG m{};m.hwnd=read(0)?owned_window(p,read(0))->hwnd:nullptr;m.message=message;m.wParam=read(2);m.lParam=LPARAM(signed32(read(3)));m.time=read(4);m.pt={signed32(read(5)),signed32(read(6))};return m;
}
#endif
}

void install_gui(Process& p){
    using Fn=std::function<U32(Process&)>;
    auto add=[&](const std::string& name,unsigned n,Fn fn){p.register_api("user32.dll",name,n,[fn=std::move(fn)](Process& q)->U32{auto& g=gui(q);
#ifdef _WIN32
        rethrow(g);auto value=fn(q);rethrow(g);return value;
#else
        (void)g;(void)fn;return 0; // unreachable: gui() throws on non-Windows
#endif
    });};
#ifdef _WIN32
    add("LoadIconA",2,load_icon);
    add("LoadCursorA",2,[](auto& q)->U32{auto& g=gui(q);if(arg(q,0) || arg(q,1)>65535)unsupported(q,"only system integer cursor resources are supported");auto h=::LoadCursorA(nullptr,MAKEINTRESOURCEA(arg(q,1)));if(!h){q.set_error(::GetLastError());return 0;}return token(g,Kind::cursor,reinterpret_cast<std::uintptr_t>(h));});
    add("RegisterClassExA",1,[](auto& q){return register_class(q,true);});
    add("RegisterClassA",1,[](auto& q){return register_class(q,false);});
    add("UnregisterClassA",2,[](auto& q)->U32{auto& g=gui(q);auto c=find_class(q,arg(q,0));if(!c){q.set_error(ERROR_CLASS_DOES_NOT_EXIST);return 0;}if(arg(q,1)!=c->instance){q.set_error(ERROR_INVALID_PARAMETER);return 0;}auto value=::UnregisterClassA(c->native_name.c_str(),::GetModuleHandleW(nullptr));if(!value){q.set_error(::GetLastError());return 0;}g.classes.erase(c->guest_name);return 1;});
    add("CreateWindowExA",12,create_window);
    add("DefWindowProcA",4,default_proc);
    add("DestroyWindow",1,[](auto& q)->U32{auto w=owned_window(q,arg(q,0));auto value=::DestroyWindow(w->hwnd);if(!value)q.set_error(::GetLastError());return U32(value!=FALSE);});
    add("IsWindow",1,[](auto& q)->U32{auto& g=gui(q);auto it=g.windows.find(arg(q,0));return it!=g.windows.end() && !it->second->dead && ::IsWindow(it->second->hwnd);});
    add("ShowWindow",2,[](auto& q)->U32{return ::ShowWindow(owned_window(q,arg(q,0))->hwnd,signed32(arg(q,1)))!=FALSE;});
    add("UpdateWindow",1,[](auto& q)->U32{return ::UpdateWindow(owned_window(q,arg(q,0))->hwnd)!=FALSE;});
    add("EnableWindow",2,[](auto& q)->U32{return ::EnableWindow(owned_window(q,arg(q,0))->hwnd,arg(q,1)!=0)!=FALSE;});
    add("PostQuitMessage",1,[](auto& q)->U32{::PostQuitMessage(signed32(arg(q,0)));return 0;});
    add("PostMessageA",4,[](auto& q)->U32{auto msg=arg(q,1);if(!GuiFrame::is_scalar(msg) || msg==WM_QUIT)unsupported(q,"PostMessageA requires a supported scalar message (use PostQuitMessage for quit)");auto h=arg(q,0)?owned_window(q,arg(q,0))->hwnd:nullptr;auto result=::PostMessageA(h,msg,arg(q,2),LPARAM(signed32(arg(q,3))));if(!result)q.set_error(::GetLastError());return result!=FALSE;});
    add("SendMessageA",4,[](auto& q)->U32{auto msg=arg(q,1);if(!GuiFrame::is_scalar(msg))unsupported(q,"SendMessageA requires a supported scalar message");auto result=::SendMessageA(owned_window(q,arg(q,0))->hwnd,msg,arg(q,2),LPARAM(signed32(arg(q,3))));rethrow(gui(q));return scalar(q,std::uintptr_t(result));});
    for(bool peek:{false,true})add(peek?"PeekMessageA":"GetMessageA",peek?5:4,[peek](auto& q)->U32{auto at=arg(q,0);q.memory.check(at,28,Memory::Write);auto h=arg(q,1)?owned_window(q,arg(q,1))->hwnd:nullptr;MSG msg{};const auto result=peek?::PeekMessageA(&msg,h,arg(q,2),arg(q,3),arg(q,4)) : ::GetMessageA(&msg,h,arg(q,2),arg(q,3));rethrow(gui(q));if(result==-1){q.set_error(::GetLastError());return 0xffffffffu;}if(result || !peek)store_msg(q,at,msg);return U32(result);});
    add("TranslateMessage",1,[](auto& q)->U32{auto msg=read_msg(q,arg(q,0));return ::TranslateMessage(&msg)!=FALSE;});
    add("DispatchMessageA",1,[](auto& q)->U32{auto msg=read_msg(q,arg(q,0));++gui(q).dispatched;auto result=::DispatchMessageA(&msg);rethrow(gui(q));return scalar(q,std::uintptr_t(result));});
    for(bool client:{false,true})add(client?"GetClientRect":"GetWindowRect",2,[client](auto& q)->U32{auto out=arg(q,1);q.memory.check(out,16,Memory::Write);auto h=owned_window(q,arg(q,0))->hwnd;RECT r{};auto result=client?::GetClientRect(h,&r) : ::GetWindowRect(h,&r);if(!result){q.set_error(::GetLastError());return 0;}q.memory.copy_in(out,std::span(reinterpret_cast<const std::uint8_t*>(&r),16));return 1;});
    add("InvalidateRect",3,[](auto& q)->U32{RECT r{};if(arg(q,1)){auto bytes=q.memory.copy_out(arg(q,1),16);std::memcpy(&r,bytes.data(),16);}auto result=::InvalidateRect(owned_window(q,arg(q,0))->hwnd,arg(q,1)?&r:nullptr,arg(q,2)!=0);if(!result)q.set_error(::GetLastError());return result!=FALSE;});
    add("BeginPaint",2,[](auto& q)->U32{auto w=owned_window(q,arg(q,0));auto out=arg(q,1);q.memory.check(out,64,Memory::Write);if(w->paint)unsupported(q,"nested BeginPaint on the same window");GuiWindow::Paint paint;paint.pointer=out;auto dc=::BeginPaint(w->hwnd,&paint.native);if(!dc){q.set_error(::GetLastError());return 0;}paint.dc=token(gui(q),Kind::dc,reinterpret_cast<std::uintptr_t>(dc));w->paint=paint;rethrow(gui(q));std::vector<std::uint8_t> zero(64);q.memory.copy_in(out,zero);q.memory.store(out,paint.dc,32);q.memory.store(out+4,U32(paint.native.fErase),32);q.memory.copy_in(out+8,std::span(reinterpret_cast<const std::uint8_t*>(&paint.native.rcPaint),16));q.memory.store(out+24,U32(paint.native.fRestore),32);q.memory.store(out+28,U32(paint.native.fIncUpdate),32);q.memory.copy_in(out+32,std::span(paint.native.rgbReserved,32));++gui(q).paints;return paint.dc;});
    add("EndPaint",2,[](auto& q)->U32{auto w=owned_window(q,arg(q,0));if(!w->paint || w->paint->pointer!=arg(q,1))unsupported(q,"EndPaint does not match an active BeginPaint");if(q.memory.load(arg(q,1),32)!=w->paint->dc)unsupported(q,"modified PAINTSTRUCT HDC");auto value=::EndPaint(w->hwnd,&w->paint->native);w->paint.reset();return value!=FALSE;});
    add("SetWindowTextA",2,[](auto& q)->U32{const auto text=q.read_string(arg(q,1));return ::SetWindowTextA(owned_window(q,arg(q,0))->hwnd,text.c_str())!=FALSE;});
    add("GetWindowTextA",3,[](auto& q)->U32{auto cap=arg(q,2);if(!cap)return 0;if(cap>32768)unsupported(q,"GetWindowText buffer bound");q.memory.check(arg(q,1),cap,Memory::Write);std::vector<char> b(cap);auto n=::GetWindowTextA(owned_window(q,arg(q,0))->hwnd,b.data(),int(cap));rethrow(gui(q));q.memory.copy_in(arg(q,1),std::span(reinterpret_cast<const std::uint8_t*>(b.data()),std::size_t(n)+1));return U32(n);});
    for(bool set:{false,true})add(set?"SetWindowLongA":"GetWindowLongA",set?3:2,[set](auto& q)->U32{auto w=owned_window(q,arg(q,0));auto index=signed32(arg(q,1));U32* at=index==GWL_USERDATA?&w->userdata:index==GWL_WNDPROC?&w->proc:nullptr;if(!at)unsupported(q,"unsupported guest window-long field");auto old=*at;if(set){if(index==GWL_WNDPROC)q.memory.check(arg(q,2),1,Memory::Execute);*at=arg(q,2);}return old;});
    add("SetWindowPos",7,[](auto& q)->U32{auto result=::SetWindowPos(owned_window(q,arg(q,0))->hwnd,zorder(q,arg(q,1)),signed32(arg(q,2)),signed32(arg(q,3)),signed32(arg(q,4)),signed32(arg(q,5)),arg(q,6));if(!result)q.set_error(::GetLastError());return result!=FALSE;});
    add("GetSystemMetrics",1,[](auto& q)->U32{return U32(::GetSystemMetrics(signed32(arg(q,0))));});
    add("GetKeyState",1,[](auto& q)->U32{return U32(std::int32_t(::GetKeyState(signed32(arg(q,0)))));});
    add("GetAsyncKeyState",1,[](auto& q)->U32{return U32(std::int32_t(::GetAsyncKeyState(signed32(arg(q,0)))));});
    add("ShowCursor",1,[](auto& q)->U32{bool show=arg(q,0)!=0;auto result=::ShowCursor(show);gui(q).cursor_balance+=show?1:-1;return U32(result);});
    add("SetCursor",1,[](auto& q)->U32{auto result=::SetCursor(native<HCURSOR>(q,arg(q,0),Kind::cursor));return token(gui(q),Kind::cursor,reinterpret_cast<std::uintptr_t>(result));});
    add("GetCursorPos",1,[](auto& q)->U32{q.memory.check(arg(q,0),8,Memory::Write);POINT point{};auto result=::GetCursorPos(&point);if(!result){q.set_error(::GetLastError());return 0;}q.memory.copy_in(arg(q,0),std::span(reinterpret_cast<const std::uint8_t*>(&point),8));return 1;});
    add("SetFocus",1,[](auto& q)->U32{auto h=arg(q,0)?owned_window(q,arg(q,0))->hwnd:nullptr;auto prior=::SetFocus(h);rethrow(gui(q));return window_token(gui(q),prior);});
    add("GetActiveWindow",0,[](auto& q)->U32{return window_token(gui(q),::GetActiveWindow());});
    add("GetForegroundWindow",0,[](auto& q)->U32{return window_token(gui(q),::GetForegroundWindow());});
    p.register_api("gdi32.dll","GetStockObject",1,[](auto& q)->U32{auto& g=gui(q);auto which=arg(q,0);if(which>5 && which!=18)unsupported(q,"only stock brushes are currently exposed");auto h=::GetStockObject(signed32(which));if(!h){q.set_error(::GetLastError());return 0;}return token(g,Kind::brush,reinterpret_cast<std::uintptr_t>(h));});
#else
    // The non-Windows backend stays explicitly unsupported, not a fake desktop.
    for(auto [name,count]:{std::pair{"LoadIconA",2u},{"LoadCursorA",2u},{"RegisterClassExA",1u},{"RegisterClassA",1u},{"UnregisterClassA",2u},{"CreateWindowExA",12u},{"DefWindowProcA",4u},{"DestroyWindow",1u},{"IsWindow",1u},{"ShowWindow",2u},{"UpdateWindow",1u},{"EnableWindow",2u},{"PostQuitMessage",1u},{"PostMessageA",4u},{"SendMessageA",4u},{"PeekMessageA",5u},{"GetMessageA",4u},{"TranslateMessage",1u},{"DispatchMessageA",1u},{"GetClientRect",2u},{"GetWindowRect",2u},{"InvalidateRect",3u},{"BeginPaint",2u},{"EndPaint",2u},{"SetWindowTextA",2u},{"GetWindowTextA",3u},{"SetWindowLongA",3u},{"GetWindowLongA",2u},{"SetWindowPos",7u},{"GetSystemMetrics",1u},{"GetKeyState",1u},{"GetAsyncKeyState",1u},{"ShowCursor",1u},{"SetCursor",1u},{"GetCursorPos",1u},{"SetFocus",1u},{"GetActiveWindow",0u},{"GetForegroundWindow",0u}})add(name,count,[](auto&)->U32{return 0;});
#endif
}
void shutdown_gui(Process& p) noexcept {
    if(!p.state().gui)return;
    auto& g=*p.state().gui;g.shutting_down=true;
#ifdef _WIN32
    for(auto& [id,w]:g.windows){(void)id;if(w->hwnd && !w->dead){if(w->paint)::EndPaint(w->hwnd,&w->paint->native);::DestroyWindow(w->hwnd);w->dead=true;}}
    for(auto& [name,c]:g.classes){(void)name;::UnregisterClassA(c->native_name.c_str(),::GetModuleHandleW(nullptr));}
    for(auto& [id,h]:g.handles){(void)id;if(h.owned && h.kind==Kind::icon)::DestroyIcon(reinterpret_cast<HICON>(h.native));}
    while(g.cursor_balance>0){::ShowCursor(FALSE);--g.cursor_balance;}while(g.cursor_balance<0){::ShowCursor(TRUE);++g.cursor_balance;}
#endif
}
std::string gui_report(const Process& p){
    const auto& g=p.state().gui;std::ostringstream out;
    out<<"{\"enabled\":"<<(p.options.enable_gui?"true":"false")<<",\"native_windows_created\":"<<(g?g->windows_created:0)
       <<",\"guest_window_callbacks\":"<<(g?g->callbacks:0)<<",\"dispatched_messages\":"<<(g?g->dispatched:0)<<",\"paint_cycles\":"<<(g?g->paints:0)<<",\"rendered_game_frames\":0,\"playable_verified\":false}";
    return out.str();
}
}
