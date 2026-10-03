#include "winrecomp/gui.hpp"
#include "winrecomp/process.hpp"
#include "winrecomp/resources.hpp"
#include <array>
#include <bit>
#include <exception>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#endif
namespace wr {
#ifndef _WIN32
namespace {class UnavailableGui final:public GuiBackend {
public:void shutdown() noexcept override{}std::string report() const override{return "{\"backend\":\"unavailable\",\"windows_created\":0,\"swaps\":0}";}
};}
std::unique_ptr<GuiBackend> install_gui(Process&){return std::make_unique<UnavailableGui>();}
#else
namespace {
using Args=std::span<const U32>;
int signed32(U32 n){return std::bit_cast<std::int32_t>(n);}
LPARAM signed_param(U32 n){return LPARAM(std::bit_cast<std::int32_t>(n));}
U32 result32(LRESULT n){return U32(std::uintptr_t(n));}
float real32(U32 n){return std::bit_cast<float>(n);}
std::vector<std::uint8_t> read_bytes(Process& p,U32 at,std::size_t n){std::vector<std::uint8_t> bytes(n);p.memory.copy_out(at,bytes);return bytes;}
U32 scalar(std::uintptr_t n){
    if(n>0xffffffffull && n<0xffffffff80000000ull)throw std::runtime_error("native window parameter is not a 32-bit scalar");
    return U32(n);
}
enum class Kind {window,foreign_window,icon,cursor,brush,gdi,dc,paint_dc,borrowed_dc,region,keyboard_layout,gl};
struct Handle {Kind kind;std::uintptr_t native;U32 owner{};bool owned{};unsigned references{1};};
struct InvalidHandle:std::runtime_error {U32 error;explicit InvalidHandle(U32 e):std::runtime_error("invalid GUI handle"),error(e){}};
class Gui;
struct WindowClass {std::string logical,native;ATOM atom{};U32 procedure{},instance{},style{},extra{};};
struct Window {Gui* gui{};std::shared_ptr<WindowClass> klass;HWND native{};U32 id{},procedure{},user_data{};bool alive{},counted{};std::array<U32,12> create{};std::string title;};
struct Frame;
thread_local std::map<HWND,std::weak_ptr<Window>> window_owners;
thread_local std::vector<std::shared_ptr<Window>> creating;
thread_local Gui* active_gui=nullptr;
// Temporary guest addresses have a bounded lifetime; Windows pointers are never
// put into these buffers. Nested callbacks get separate scratch reservations.
struct Scratch {
    Process& p;U32 base{},used{};
    explicit Scratch(Process& process):p(process),base(p.allocate_bytes(65536)){}
    ~Scratch(){try{p.memory.release(base);}catch(...){}}
    U32 alloc(std::size_t n){const auto start=(used+3u)&~3u;if(n>65536u-start)throw GuestFault(FaultKind::unsupported,p.cpu.eip,"window callback scratch limit");used=start+U32(n);return base+start;}
    U32 string(const std::string& s){const auto a=alloc(s.size()+1);p.memory.copy_in(a,std::span(reinterpret_cast<const std::uint8_t*>(s.c_str()),s.size()+1));return a;}
};
class Gui final:public GuiBackend {
public:
    Process& p;
    const DWORD thread=GetCurrentThreadId();
    bool stopping{},quit_posted{};
    std::exception_ptr failure;
    std::map<U32,Handle> handles;
    std::map<std::string,std::shared_ptr<WindowClass>> classes;
    std::map<U32,std::shared_ptr<Window>> windows;
    struct Paint {HWND native;PAINTSTRUCT ps;U32 dc;};
    std::map<std::pair<U32,U32>,Paint> paints;
    std::map<std::pair<U32,U32>,U32> strings;
    std::map<std::pair<U32,ResourceName>,U32> icons;
    std::vector<Frame*> frames;
    U32 next{0xb0000000},current_gl{},current_dc{};
    std::uint64_t created{},destroyed{},callbacks{},dispatched{},contexts{},swaps{},host_only{};
    std::set<UINT> host_only_ids;
    explicit Gui(Process& process):p(process){install();}
    ~Gui() override{shutdown();}
    [[noreturn]] void unsupported(const std::string& text){throw GuestFault(FaultKind::unsupported,p.cpu.eip,text);}
    void enter(){if(GetCurrentThreadId()!=thread)unsupported("GUI called from a different host thread");if(active_gui && active_gui!=this)unsupported("one guest GUI per host thread is supported");active_gui=this;check();}
    void check(){if(failure)std::rethrow_exception(failure);}
    void api(const char* dll,const char* name,unsigned n,std::function<U32(Args)> fn,U32 bad=0) {
        p.register_api(dll,name,n,[this,n,fn=std::move(fn),bad](Process& q){
            enter();std::array<U32,16> args{};for(unsigned i=0;i<n;++i)args[i]=q.argument(i);
            try{const auto value=fn(std::span(args).first(n));check();return value;}
            catch(const InvalidHandle& e){check();q.set_error(e.error);return bad;}
        });
    }
    U32 token(Kind kind,std::uintptr_t native,U32 owner=0,bool owned=false,bool reuse=true){
        if(!native)return 0;
        if(reuse)for(const auto& [id,h]:handles)if(h.kind==kind && h.native==native && h.owner==owner)return id;
        if(next>=0xb1000000 || handles.size()>=8192)unsupported("GUI handle budget exceeded");
        const auto id=next;next+=16;handles.emplace(id,Handle{kind,native,owner,owned,1});return id;
    }
    template<class T>T get(U32 id,Kind kind,bool null_ok=false){
        if(!id && null_ok)return nullptr;const auto it=handles.find(id);
        if(it==handles.end() || it->second.kind!=kind)throw InvalidHandle{kind==Kind::window?1400u:6u};
        return reinterpret_cast<T>(it->second.native);
    }
    HWND hwnd(U32 id,bool null_ok=false){return get<HWND>(id,Kind::window,null_ok);}
    U32 window_token(HWND w){
        if(!w)return 0;
        if(auto it=window_owners.find(w);it!=window_owners.end())if(auto owned=it->second.lock())if(owned->gui==this)return owned->id;
        return token(Kind::foreign_window,reinterpret_cast<std::uintptr_t>(w));
    }
    HWND message_window(U32 id){if(!id)return nullptr;auto it=handles.find(id);if(it==handles.end() || (it->second.kind!=Kind::window && it->second.kind!=Kind::foreign_window))throw InvalidHandle{1400};return reinterpret_cast<HWND>(it->second.native);}
    HWND zorder(U32 id){if(id<=1 || id>=0xfffffffeu)return reinterpret_cast<HWND>(signed_param(id));return message_window(id);}
    U32 ztoken(HWND h){const auto value=reinterpret_cast<std::uintptr_t>(h);if(value<=1 || value>=0xfffffffffffffffeull)return U32(value);return window_token(h);}
    HBRUSH brush(U32 id){if(id<=COLOR_MENUBAR+1)return reinterpret_cast<HBRUSH>(std::uintptr_t(id));return get<HBRUSH>(id,Kind::brush);}
    HDC dc(U32 id){auto it=handles.find(id);if(it==handles.end() || (it->second.kind!=Kind::dc && it->second.kind!=Kind::paint_dc && it->second.kind!=Kind::borrowed_dc))throw InvalidHandle{6};return reinterpret_cast<HDC>(it->second.native);}
    U32 native_bool(BOOL value){if(!value)p.set_error(GetLastError());return U32(value);}
    void ensure_gl(){if(!current_gl || wglGetCurrentContext()!=get<HGLRC>(current_gl,Kind::gl))unsupported("OpenGL call without the guest's current context");}
    std::shared_ptr<WindowClass> klass(U32 value){
        if(value<65536){for(const auto& [name,c]:classes)if(c->atom==value)return c;}
        else {auto name=p.read_string(value,256);if(auto it=classes.find(name);it!=classes.end())return it->second;}
        throw InvalidHandle{1407};
    }
    void install();
    U32 register_class(Args);
    U32 create_window(Args);
    U32 load_icon(Args);
    U32 default_window(Args);
    U32 send_message(Args,bool post);
    U32 peek_message(Args,bool blocking);
    MSG read_message(U32);
    U32 paint_begin(Args);
    U32 paint_end(Args);
    void write_message(U32,const MSG&);
    static LRESULT CALLBACK procedure(HWND,UINT,WPARAM,LPARAM) noexcept;
    void shutdown() noexcept override;
    std::string report() const override {
        std::ostringstream s;s<<"{\"backend\":\"native-win32\",\"windows_created\":"<<created<<",\"windows_destroyed\":"<<destroyed<<",\"window_callbacks\":"<<callbacks<<",\"messages_dispatched\":"<<dispatched<<",\"gl_contexts_created\":"<<contexts<<",\"swaps\":"<<swaps<<",\"host_only_notifications\":"<<host_only<<",\"host_only_message_ids\":[";
        bool first=true;for(auto id:host_only_ids){if(!first)s<<',';first=false;s<<id;}s<<"]}";return s.str();
    }
};
#include "gui_messages.hpp"
#include "gui_callbacks.hpp"
#include "gui_lifecycle.hpp"
#include "gui_apis.hpp"
}
std::unique_ptr<GuiBackend> install_gui(Process& process){return std::make_unique<Gui>(process);}
#endif
}
