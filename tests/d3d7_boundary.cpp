// Compare real native Direct3D7 enumeration against the guest COM/ABI bridge.
// No game data, replacement caps, or mocked host Direct3D implementation.
#include "winrecomp/process.hpp"
#include <array>
#include <cstring>
#include <exception>
#include <iostream>
#include <thread>
#include <vector>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define DIRECTDRAW_VERSION 0x0700
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
namespace {
unsigned checks{};
#define CHECK(x) do {++checks;if(!(x))throw std::runtime_error("Direct3D7 assertion: " #x);} while(0)
using U32=wr::U32;
constexpr U32 Entry=0x401000,DeviceCallback=0x401010,ZCallback=0x401020,Context=0xa15379bc;
struct Row {std::string description,name;std::array<std::uint8_t,236> caps{};bool operator==(const Row&)const=default;};
using ZRow=std::array<U32,8>;
Row native_row(char* description,char* name,const D3DDEVICEDESC7& caps){
    Row row;row.description=description;row.name=name;
    // Independent layout check: native structure with reserved tail excluded.
    // All public DX7 fields are fixed-width, so compare their actual byte values.
    static_assert(sizeof(caps)==236);std::memcpy(row.caps.data(),&caps,220);return row;
}
ZRow native_z(const DDPIXELFORMAT& d){return {32,d.dwFlags,0,d.dwZBufferBitDepth,(d.dwFlags&DDPF_STENCILBUFFER)?d.dwStencilBitDepth:0,d.dwZBitMask,(d.dwFlags&DDPF_STENCILBUFFER)?d.dwStencilBitMask:0,0};}
struct Original {std::vector<Row> rows;std::vector<ZRow> z;HRESULT reply=D3DENUMRET_OK;};
HRESULT CALLBACK original_device(char* d,char* n,D3DDEVICEDESC7* caps,void* ctx){auto& out=*static_cast<Original*>(ctx);out.rows.push_back(native_row(d,n,*caps));return out.reply;}
HRESULT CALLBACK original_z(DDPIXELFORMAT* caps,void* ctx){auto& out=*static_cast<Original*>(ctx);out.z.push_back(native_z(*caps));return out.reply;}
struct Native {IDirectDraw* draw{};IDirect3D7* d3d{};Native(){CHECK(DirectDrawCreate(nullptr,&draw,nullptr)==DD_OK);CHECK(draw->QueryInterface(IID_IDirect3D7,reinterpret_cast<void**>(&d3d))==D3D_OK);}~Native(){if(d3d)d3d->Release();if(draw)draw->Release();}};
wr::Image image(){wr::Image im;im.base=0x400000;im.entry=Entry;im.size=0x3000;im.headers_size=512;im.bytes.resize(1536);im.sections={{".text",0x1000,512,512,512,0x60000020},{".data",0x2000,512,1024,512,0xc0000040}};for(auto off:{512,528,544})im.bytes[off]=0xc3;return im;}
U32 invoke(wr::Process& p,U32 target,std::initializer_list<U32> args){auto saved=p.cpu;try{for(auto it=args.end();it!=args.begin();)wr::push(p.cpu,p.memory,*--it);wr::push(p.cpu,p.memory,saved.eip);p.cpu.eip=target;CHECK(target && p.dispatch_api());CHECK(p.cpu.eip==saved.eip && p.cpu.r[wr::ESP]==saved.r[wr::ESP]);for(auto r:{wr::EBX,wr::EBP,wr::ESI,wr::EDI})CHECK(p.cpu.r[r]==saved.r[r]);return p.cpu.r[wr::EAX];}catch(...){p.cpu=saved;throw;}}
U32 method(wr::Process& p,U32 object,unsigned slot,std::initializer_list<U32> args){return invoke(p,p.memory.load(p.memory.load(object,32)+slot*4,32),args);}
template<class F>void fault(F&& fn,wr::FaultKind kind){bool caught=false;try{fn();}catch(const wr::GuestFault& e){CHECK(e.kind==kind);caught=true;}CHECK(caught);}
void guid(wr::Process& p,U32 at,const GUID& id){p.memory.copy_in(at,std::span(reinterpret_cast<const std::uint8_t*>(&id),16));}
struct State {wr::Process* process{};std::vector<Row> rows;std::vector<ZRow> z;std::vector<std::pair<U32,std::string>> strings;U32 expired{},object{},reply=D3DENUMRET_OK;bool throw_callback{},write_callback{},write_name{},exit_callback{},release_callback{},nest{},in_nested{};} state;
bool step(wr::Cpu& cpu,wr::Memory& memory,std::uint64_t&){
    if(cpu.eip==Entry){cpu.eip=wr::pop(cpu,memory);return true;}
    if(cpu.eip!=DeviceCallback && cpu.eip!=ZCallback)return false;
    auto& p=*state.process;const bool device=cpu.eip==DeviceCallback;
    auto at=memory.load(cpu.r[wr::ESP]+(device?12:4),32);state.expired=at;
    CHECK(memory.load(cpu.r[wr::ESP]+(device?16:8),32)==Context);
    if(device){Row row;auto desc=memory.load(cpu.r[wr::ESP]+4,32),name=memory.load(cpu.r[wr::ESP]+8,32);row.description=p.read_string(desc);row.name=p.read_string(name);memory.copy_out(at,row.caps);CHECK(memory.load(at+4,32)==56 && memory.load(at+60,32)==56);for(unsigned n=220;n<236;n+=4)CHECK(memory.load(at+n,32)==0);state.rows.push_back(row);state.strings.emplace_back(desc,row.description);state.strings.emplace_back(name,row.name);if(state.write_name)memory.store(name,0,8);}
    else {ZRow row{};for(unsigned n=0;n<8;++n)row[n]=memory.load(at+n*4,32);CHECK(row[0]==32 && row[2]==0 && row[7]==0);state.z.push_back(row);}
    if(state.throw_callback)throw wr::GuestFault(wr::FaultKind::unsupported,cpu.eip,"intentional D3D callback fault");
    if(state.write_callback)memory.store(at,0,32);
    if(state.nest && !state.in_nested && device){auto before=state.rows.back().caps;state.in_nested=true;CHECK(method(p,state.object,3,{state.object,DeviceCallback,Context})==D3D_OK);state.in_nested=false;std::array<std::uint8_t,236> after{};memory.copy_out(at,after);CHECK(before==after);state.expired=at;}
    if(state.release_callback){state.release_callback=false;method(p,state.object,2,{state.object});}
    if(state.exit_callback)p.exit(17);
    cpu.r[wr::EAX]=state.reply;cpu.eip=wr::pop(cpu,memory);cpu.r[wr::ESP]+=device?16:8;return true;
}
U32 create(wr::Process& p,U32 data,U32& draw){CHECK(invoke(p,p.resolve("ddraw.dll","DirectDrawCreate"),{0,data,0})==DD_OK);draw=p.memory.load(data,32);guid(p,data+16,IID_IDirect3D7);CHECK(method(p,draw,0,{draw,data+16,data+64})==D3D_OK);return p.memory.load(data+64,32);}
void run(){
    Native native;wr::Process p(step);p.load(image());state={};state.process=&p;auto data=p.allocate_bytes(8192);U32 draw{};auto d3d=create(p,data,draw);state.object=d3d;CHECK(d3d && d3d!=draw);
    auto enumerate=[&]{return method(p,d3d,3,{d3d,DeviceCallback,Context});};
    // Native identity, reference tracking, and null output before host calls.
    auto old=d3d;CHECK(method(p,d3d,0,{d3d,data+16,data+64})==D3D_OK);CHECK(p.memory.load(data+64,32)==old);method(p,d3d,2,{d3d});
    guid(p,data+16,IID_IUnknown);CHECK(method(p,d3d,0,{d3d,data+16,data+64})==S_OK);auto identity=p.memory.load(data+64,32);
    CHECK(method(p,draw,0,{draw,data+16,data+68})==S_OK);CHECK(p.memory.load(data+68,32)==identity);method(p,identity,2,{identity});method(p,identity,2,{identity});
    guid(p,data+16,GUID{0xf19251f1,0x1,0x2,{1,2,3,4,5,6,7,8}});p.memory.store(data+64,0xdeadbeef,32);
    CHECK(method(p,d3d,0,{d3d,data+16,data+64})==U32(E_NOINTERFACE));CHECK(p.memory.load(data+64,32)==0);
    fault([&]{method(p,d3d,0,{d3d,data+16,0});},wr::FaultKind::memory);
    fault([&]{p.memory.store(d3d,0,32);},wr::FaultKind::memory);fault([&]{p.memory.store(p.memory.load(d3d,32),0,32);},wr::FaultKind::memory);
    auto device_target=p.memory.load(p.memory.load(d3d,32)+12,32);fault([&]{invoke(p,device_target,{draw,DeviceCallback,Context});},wr::FaultKind::unsupported);
    Original expected;CHECK(native.d3d->EnumDevices(original_device,&expected)==D3D_OK);CHECK(!expected.rows.empty());CHECK(enumerate()==D3D_OK);CHECK(state.rows==expected.rows);
    fault([&]{p.memory.load(state.expired,32);},wr::FaultKind::memory);
    auto strings=state.strings;for(auto& [at,text]:strings)CHECK(p.read_string(at)==text);
    state.rows.clear();state.strings.clear();CHECK(enumerate()==D3D_OK);CHECK(state.rows==expected.rows);CHECK(state.strings==strings);
    const auto snapshot=expected.rows;expected.rows.clear();expected.reply=D3DENUMRET_CANCEL;
    CHECK(native.d3d->EnumDevices(original_device,&expected)==D3D_OK);CHECK(!expected.rows.empty());state.rows.clear();state.reply=D3DENUMRET_CANCEL;CHECK(enumerate()==D3D_OK);CHECK(state.rows==expected.rows);
    auto cancel=expected.rows;state.reply=D3DENUMRET_OK;
    const auto hr=native.d3d->EnumDevices(nullptr,nullptr);CHECK(method(p,d3d,3,{d3d,0,Context})==U32(hr));
    fault([&]{method(p,d3d,3,{d3d,data,Context});},wr::FaultKind::memory);
    for(int mode=0;mode<4;++mode){state.throw_callback=mode==0;state.write_callback=mode==1;state.write_name=mode==2;state.reply=mode==3?2:D3DENUMRET_OK;fault([&]{enumerate();},(mode==1 || mode==2)?wr::FaultKind::memory:wr::FaultKind::unsupported);fault([&]{p.memory.load(state.expired,32);},wr::FaultKind::memory);}
    state.throw_callback=state.write_callback=state.write_name=false;state.reply=D3DENUMRET_CANCEL;
    state.rows.clear();state.nest=true;CHECK(enumerate()==D3D_OK);CHECK(state.rows.size()==cancel.size()*2);state.nest=false;
    // Depth formats are queried for an actual enumerated GUID, never guessed.
    GUID device{};std::memcpy(&device,snapshot.front().caps.data()+196,16);guid(p,data+16,device);
    expected.z.clear();expected.reply=D3DENUMRET_OK;CHECK(native.d3d->EnumZBufferFormats(device,original_z,&expected)==D3D_OK);CHECK(!expected.z.empty());
    state.reply=D3DENUMRET_OK;CHECK(method(p,d3d,6,{d3d,data+16,ZCallback,Context})==D3D_OK);CHECK(state.z==expected.z);fault([&]{p.memory.load(state.expired,32);},wr::FaultKind::memory);
    expected.z.clear();expected.reply=D3DENUMRET_CANCEL;CHECK(native.d3d->EnumZBufferFormats(device,original_z,&expected)==D3D_OK);state.z.clear();state.reply=D3DENUMRET_CANCEL;CHECK(method(p,d3d,6,{d3d,data+16,ZCallback,Context})==D3D_OK);CHECK(state.z==expected.z);
    fault([&]{method(p,d3d,6,{d3d,0,ZCallback,Context});},wr::FaultKind::memory);
    for(int mode=0;mode<3;++mode){state.throw_callback=mode==0;state.write_callback=mode==1;state.reply=mode==2?2:D3DENUMRET_OK;fault([&]{method(p,d3d,6,{d3d,data+16,ZCallback,Context});},mode==1?wr::FaultKind::memory:wr::FaultKind::unsupported);fault([&]{p.memory.load(state.expired,32);},wr::FaultKind::memory);}
    state.throw_callback=state.write_callback=false;state.reply=D3DENUMRET_CANCEL;
    CHECK(method(p,d3d,7,{d3d})==U32(native.d3d->EvictManagedTextures()));
    // Invalid output is rejected before device creation; vertex buffers remain unsupported.
    fault([&]{method(p,d3d,4,{d3d,data+16,0,0});},wr::FaultKind::memory);
    fault([&]{method(p,d3d,5,{d3d,0,data+64,0});},wr::FaultKind::unsupported);
    bool rejected=false;std::thread foreign([&]{try{enumerate();}catch(const wr::GuestFault& e){rejected=e.kind==wr::FaultKind::unsupported;}});foreign.join();CHECK(rejected);
    // Native enumeration survives release of both original guest references.
    method(p,draw,2,{draw});state.release_callback=true;state.rows.clear();CHECK(enumerate()==D3D_OK);CHECK(state.rows==cancel);fault([&]{enumerate();},wr::FaultKind::unsupported);
    for(auto& [at,text]:strings)CHECK(p.read_string(at)==text);
    // Process exit must not trigger any additional guest callbacks.
    wr::Process exiting(step);exiting.load(image());state={};state.process=&exiting;auto out=exiting.allocate_bytes(128);auto final=create(exiting,out,draw);state.object=final;state.exit_callback=true;
    CHECK(method(exiting,final,3,{final,DeviceCallback,Context})==D3D_OK);CHECK(exiting.exited() && state.rows.size()==1);
    fault([&]{exiting.memory.load(state.expired,32);},wr::FaultKind::memory);
    std::cout<<"Native device rows="<<snapshot.size()<<", reserved words normalized; bounded COM/ABI tests passed\n";
}
}
int main(){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);try{run();std::cout<<checks<<" Direct3D7 native-versus-guest assertions passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
