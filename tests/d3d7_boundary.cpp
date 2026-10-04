// Real Direct3D7 enumeration vs the checked guest ABI. No game data.
#include "winrecomp/process.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <vector>
#include <utility>
#include <tuple>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d.h>
namespace {
unsigned checks{};
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("D3D7 assertion: " #x);}while(0)
constexpr wr::U32 Entry=0x401000,DeviceCallback=0x401010,ZCallback=0x401020,ModeCallback=0x401030,Context=0xe1357246;
struct Row {std::string description,name;std::array<std::uint8_t,236> caps{};bool operator==(const Row&) const=default;};
struct Rows {std::vector<Row> devices;std::vector<std::array<wr::U32,8>> formats;std::vector<std::array<wr::U32,31>> modes;wr::U32 reply{D3DENUMRET_OK};};
Rows guest;
wr::Process* current{};wr::U32 expired{},expired_string{},release_receiver{};
bool write_callback{},throw_callback{},exit_callback{};
wr::U32 invoke(wr::Process& p,wr::U32 target,std::initializer_list<wr::U32> args){
    const auto saved=p.cpu;
    try{
        for(auto it=args.end();it!=args.begin();)wr::push(p.cpu,p.memory,*--it);
        wr::push(p.cpu,p.memory,saved.eip);p.cpu.eip=target;CHECK(target && p.dispatch_api());
        const auto result=p.cpu.r[wr::EAX];
        CHECK(p.cpu.r[wr::ESP]==saved.r[wr::ESP] && p.cpu.eip==saved.eip);
        for(auto r:{wr::EBX,wr::ESI,wr::EDI,wr::EBP})CHECK(p.cpu.r[r]==saved.r[r]);
        return result;
    }catch(...){p.cpu=saved;throw;}
}
wr::U32 method(wr::Process& p,wr::U32 obj,unsigned slot,std::initializer_list<wr::U32> args){return invoke(p,p.memory.load(p.memory.load(obj,32)+4*slot,32),args);}
bool step(wr::Cpu& cpu,wr::Memory& m,std::uint64_t&){
    if(cpu.eip==Entry){cpu.eip=wr::pop(cpu,m);return true;}
    const bool devices=cpu.eip==DeviceCallback;if(!devices && cpu.eip!=ZCallback && cpu.eip!=ModeCallback)return false;
    const auto stack=cpu.r[wr::ESP];const auto at=m.load(stack+(devices?12:4),32);expired=at;
    CHECK(m.load(stack+(devices?16:8),32)==Context);
    if(devices){
        Row row;const auto a=m.load(stack+4,32),b=m.load(stack+8,32);expired_string=a;
        row.description=current->read_string(a);row.name=current->read_string(b);m.copy_out(at,row.caps);
        CHECK(!row.name.empty() && !row.description.empty());
        for(unsigned n=220;n<236;++n)CHECK(row.caps[n]==0); // reserved bytes
        guest.devices.push_back(row);
    }else if(cpu.eip==ModeCallback){
        std::array<wr::U32,31> row{};for(unsigned n=0;n<31;++n)row[n]=m.load(at+4*n,32);
        CHECK(row[0]==124 && row[8]==0 && row[9]==0);guest.modes.push_back(row);
    }else{
        std::array<wr::U32,8> row{};for(unsigned n=0;n<8;++n)row[n]=m.load(at+4*n,32);
        CHECK(row[0]==32 && (row[1]&DDPF_ZBUFFER));CHECK(row[2]==0 && row[7]==0);guest.formats.push_back(row);
    }
    if(write_callback)m.store(at,0,32);
    if(throw_callback)throw wr::GuestFault(wr::FaultKind::unsupported,cpu.eip,"D3D7 callback error");
    if(release_receiver){const auto obj=std::exchange(release_receiver,0u);method(*current,obj,2,{obj});}
    if(exit_callback)current->exit(23);
    cpu.r[wr::EAX]=guest.reply;cpu.eip=wr::pop(cpu,m);cpu.r[wr::ESP]+=devices?16:8;return true;
}
wr::Image image(){wr::Image im;im.base=0x400000;im.entry=Entry;im.size=0x3000;im.headers_size=512;im.bytes.resize(1536);im.sections={{".text",0x1000,512,512,512,0x60000020},{".data",0x2000,512,1024,512,0xc0000040}};im.bytes[512]=im.bytes[528]=im.bytes[544]=im.bytes[560]=0xc3;return im;}
HRESULT CALLBACK native_device(LPSTR desc,LPSTR name,D3DDEVICEDESC7* caps,void* ctx){
    auto& out=*static_cast<Rows*>(ctx);Row row;row.description=desc;row.name=name;
    auto normalized=*caps;normalized.dwReserved1=normalized.dwReserved2=normalized.dwReserved3=normalized.dwReserved4=0;
    static_assert(sizeof(normalized)==236);std::memcpy(row.caps.data(),&normalized,236);
    out.devices.push_back(row);return HRESULT(out.reply);
}
HRESULT CALLBACK native_zformat(DDPIXELFORMAT* format,void* ctx){
    auto& out=*static_cast<Rows*>(ctx);std::array<wr::U32,8> row{};
    row[0]=format->dwSize;row[1]=format->dwFlags;row[3]=format->dwZBufferBitDepth;row[5]=format->dwZBitMask;
    if(format->dwFlags&DDPF_STENCILBUFFER){row[4]=format->dwStencilBitDepth;row[6]=format->dwStencilBitMask;}
    out.formats.push_back(row);return HRESULT(out.reply);
}
HRESULT CALLBACK native_mode(DDSURFACEDESC2* d,void* ctx){
    auto& out=*static_cast<Rows*>(ctx);std::array<wr::U32,31> row{};
    row[0]=124;row[1]=d->dwFlags;
    if(d->dwFlags&DDSD_HEIGHT)row[2]=d->dwHeight;
    if(d->dwFlags&DDSD_WIDTH)row[3]=d->dwWidth;
    if(d->dwFlags&(DDSD_PITCH|DDSD_LINEARSIZE))row[4]=wr::U32(d->lPitch);
    if(d->dwFlags&DDSD_BACKBUFFERCOUNT)row[5]=d->dwBackBufferCount;
    if(d->dwFlags&(DDSD_MIPMAPCOUNT|DDSD_REFRESHRATE))row[6]=d->dwRefreshRate;
    if(d->dwFlags&DDSD_ALPHABITDEPTH)row[7]=d->dwAlphaBitDepth;
    CHECK(!d->lpSurface && !(d->dwFlags&DDSD_LPSURFACE));
    for(auto color:{std::tuple{DDSD_CKDESTOVERLAY,10u,&d->ddckCKDestOverlay},
        std::tuple{DDSD_CKDESTBLT,12u,&d->ddckCKDestBlt},std::tuple{DDSD_CKSRCOVERLAY,14u,&d->ddckCKSrcOverlay},
        std::tuple{DDSD_CKSRCBLT,16u,&d->ddckCKSrcBlt}})
        if(d->dwFlags&std::get<0>(color))std::memcpy(row.data()+std::get<1>(color),std::get<2>(color),8);
    if(d->dwFlags&DDSD_PIXELFORMAT)std::memcpy(row.data()+18,&d->ddpfPixelFormat,32);
    if(d->dwFlags&DDSD_CAPS)std::memcpy(row.data()+26,&d->ddsCaps,16);
    if(d->dwFlags&DDSD_TEXTURESTAGE)row[30]=d->dwTextureStage;
    out.modes.push_back(row);return HRESULT(out.reply);
}
struct Native {IDirectDraw7* dd{};IDirect3D7* d3d{};~Native(){if(d3d)d3d->Release();if(dd)dd->Release();}};
template<class F>void fault(F fn,wr::FaultKind kind){bool caught=false;try{fn();}catch(const wr::GuestFault& e){CHECK(e.kind==kind);caught=true;}CHECK(caught);}
void put_guid(wr::Process& p,wr::U32 at,const GUID& id){p.memory.copy_in(at,std::span(reinterpret_cast<const std::uint8_t*>(&id),16));}
struct GuestSetup{
    wr::Process p{step};wr::U32 data{},dd{},d3d{};
    GuestSetup(){current=&p;p.load(image());data=p.allocate_bytes(8192);
        put_guid(p,data,IID_IDirectDraw7);put_guid(p,data+16,IID_IDirect3D7);put_guid(p,data+32,IID_IUnknown);
        CHECK(invoke(p,p.resolve("ddraw.dll","DirectDrawCreateEx"),{0,data+64,data,0})==DD_OK);dd=p.memory.load(data+64,32);
        p.memory.store(data+72,0xcafebabe,32);CHECK(method(p,dd,0,{dd,data+16,data+68})==DD_OK);d3d=p.memory.load(data+68,32);
        CHECK(d3d && d3d!=dd && p.memory.load(data+72,32)==0xcafebabe);
        CHECK(method(p,d3d,0,{d3d,data+16,data+76})==DD_OK);CHECK(p.memory.load(data+76,32)==d3d);method(p,d3d,2,{d3d});
    }
};
void run(){
    Native native;CHECK(DirectDrawCreateEx(nullptr,reinterpret_cast<void**>(&native.dd),IID_IDirectDraw7,nullptr)==DD_OK);
    CHECK(native.dd->QueryInterface(IID_IDirect3D7,reinterpret_cast<void**>(&native.d3d))==DD_OK);
    GuestSetup s;auto& p=s.p;const auto obj=s.d3d,data=s.data;
    Rows all;CHECK(native.d3d->EnumDevices(native_device,&all)==D3D_OK);CHECK(!all.devices.empty());
    for(const auto reply:{D3DENUMRET_OK,D3DENUMRET_CANCEL}){
        Rows expected;expected.reply=reply;CHECK(native.d3d->EnumDevices(native_device,&expected)==D3D_OK);
        guest={};guest.reply=reply;const auto regions=p.memory.regions().size();
        CHECK(method(p,obj,3,{obj,DeviceCallback,Context})==D3D_OK);CHECK(guest.devices==expected.devices);
        CHECK(p.memory.regions().size()==regions);fault([&]{p.memory.load(expired,32);},wr::FaultKind::memory);
        fault([&]{p.read_string(expired_string);},wr::FaultKind::memory);
    }
    // Identity must round-trip through IUnknown, not produce host addresses.
    CHECK(method(p,obj,0,{obj,data+32,data+80})==S_OK);const auto unknown=p.memory.load(data+80,32);
    CHECK(method(p,s.dd,0,{s.dd,data+32,data+84})==S_OK);CHECK(p.memory.load(data+84,32)==unknown);
    CHECK(method(p,unknown,0,{unknown,data+16,data+88})==S_OK);CHECK(p.memory.load(data+88,32)==obj);
    method(p,obj,2,{obj});method(p,unknown,2,{unknown});method(p,unknown,2,{unknown});
    // Named field/extent comparison with the real native driver. Native x64
    // alignment padding is outside the 1068-byte guest output contract.
    const auto idout=data+512;
    for(auto flags:{0u,wr::U32(DDGDI_GETHOSTIDENTIFIER),0x80000000u}){
        DDDEVICEIDENTIFIER2 native_id{};const auto hr=native.dd->GetDeviceIdentifier(&native_id,flags);
        p.memory.store(idout+1068,0xdecafbad,32);
        CHECK(method(p,s.dd,27,{s.dd,idout,flags})==wr::U32(hr));
        CHECK(p.memory.load(idout+1068,32)==0xdecafbad);
        if(SUCCEEDED(hr)){
            CHECK(p.read_string(idout)==native_id.szDriver && p.read_string(idout+512)==native_id.szDescription);
            CHECK(p.memory.load(idout+1024,32)==native_id.liDriverVersion.LowPart);
            CHECK(p.memory.load(idout+1028,32)==wr::U32(native_id.liDriverVersion.HighPart));
            CHECK(p.memory.load(idout+1032,32)==native_id.dwVendorId);
            CHECK(p.memory.load(idout+1036,32)==native_id.dwDeviceId);
            CHECK(p.memory.load(idout+1040,32)==native_id.dwSubSysId);
            CHECK(p.memory.load(idout+1044,32)==native_id.dwRevision);
            CHECK(p.memory.load(idout+1064,32)==native_id.dwWHQLLevel);
            GUID copy{};p.memory.copy_out(idout+1048,std::span(reinterpret_cast<std::uint8_t*>(&copy),16));
            CHECK(IsEqualGUID(copy,native_id.guidDeviceIdentifier));
        }
    }
    fault([&]{method(p,s.dd,27,{s.dd,data+8192-1064,0});},wr::FaultKind::memory);
    p.memory.protect(data+4096,4096,wr::Memory::Read);
    fault([&]{method(p,s.dd,27,{s.dd,data+4096,0});},wr::FaultKind::memory);
    p.memory.protect(data+4096,4096,wr::Memory::Read|wr::Memory::Write);
    // DirectDraw capabilities: both nullable outputs and both validated before
    // writes; compare stable capability fields, not live free-VRAM counters.
    const auto hwout=data+2048,swout=data+2560;
    for(unsigned mask=0;mask<4;++mask){
        DDCAPS hw{},sw{};hw.dwSize=sw.dwSize=380;
        const auto hr=native.dd->GetCaps(mask&1?&hw:nullptr,mask&2?&sw:nullptr);
        p.memory.store(hwout,380,32);p.memory.store(swout,380,32);
        p.memory.store(hwout+380,0xbadf00d,32);p.memory.store(swout+380,0xbadf00d,32);
        CHECK(method(p,s.dd,11,{s.dd,mask&1?hwout:0,mask&2?swout:0})==wr::U32(hr));
        for(auto pair:{std::pair{mask&1?hwout:0,&hw},std::pair{mask&2?swout:0,&sw}}){
            if(!pair.first || FAILED(hr))continue;
            CHECK(p.memory.load(pair.first,32)==380 && p.memory.load(pair.first+380,32)==0xbadf00d);
            CHECK(p.memory.load(pair.first+4,32)==pair.second->dwCaps);
            CHECK(p.memory.load(pair.first+8,32)==pair.second->dwCaps2);
            CHECK(p.memory.load(pair.first+364,32)==pair.second->ddsCaps.dwCaps);
            CHECK(p.memory.load(pair.first+368,32)==pair.second->ddsCaps.dwCaps2);
        }
    }
    p.memory.store(hwout+4,0xbaadcafe,32);
    fault([&]{method(p,s.dd,11,{s.dd,hwout,data+8192-376});},wr::FaultKind::memory);
    CHECK(p.memory.load(hwout+4,32)==0xbaadcafe);
    p.memory.store(swout,1,32);fault([&]{method(p,s.dd,11,{s.dd,hwout,swout});},wr::FaultKind::unsupported);
    CHECK(p.memory.load(hwout+4,32)==0xbaadcafe);
    fault([&]{method(p,s.dd,11,{s.dd,hwout,hwout});},wr::FaultKind::unsupported);
    // DD7 uses the 124-byte x86 description, not the old 108-byte form.
    for(const auto reply:{DDENUMRET_OK,DDENUMRET_CANCEL}){
        Rows expected;expected.reply=reply;
        const auto hr=native.dd->EnumDisplayModes(0,nullptr,&expected,native_mode);
        guest={};guest.reply=reply;const auto regions=p.memory.regions().size();
        CHECK(method(p,s.dd,8,{s.dd,0,0,Context,ModeCallback})==wr::U32(hr));
        CHECK(guest.modes==expected.modes);CHECK(!expected.modes.empty());
        CHECK(regions==p.memory.regions().size());
        fault([&]{p.memory.load(expired,32);},wr::FaultKind::memory);
    }
    DDSURFACEDESC2 filter{};filter.dwSize=sizeof(filter);filter.dwFlags=DDSD_WIDTH;
    filter.dwWidth=guest.modes.front()[3];const auto filter_at=data+4096;
    p.memory.store(filter_at,124,32);p.memory.store(filter_at+4,DDSD_WIDTH,32);
    p.memory.store(filter_at+12,filter.dwWidth,32);
    // Unflagged height is ignored, not copied as though it were a constraint.
    p.memory.store(filter_at+8,0xffffffffu,32);
    Rows expected_modes;auto filter_hr=native.dd->EnumDisplayModes(0,&filter,&expected_modes,native_mode);
    guest={};CHECK(method(p,s.dd,8,{s.dd,0,filter_at,Context,ModeCallback})==wr::U32(filter_hr));
    CHECK(guest.modes==expected_modes.modes);
    p.memory.store(filter_at+4,DDSD_LPSURFACE,32);
    fault([&]{method(p,s.dd,8,{s.dd,0,filter_at,Context,ModeCallback});},wr::FaultKind::unsupported);
    p.memory.store(filter_at+4,0,32);p.memory.store(filter_at+36,0x1234,32);
    fault([&]{method(p,s.dd,8,{s.dd,0,filter_at,Context,ModeCallback});},wr::FaultKind::unsupported);
    fault([&]{method(p,s.dd,8,{s.dd,0,data+8192-120,Context,ModeCallback});},wr::FaultKind::memory);
    for(int mode=0;mode<3;++mode){
        guest={};write_callback=mode==0;throw_callback=mode==1;guest.reply=mode==2?2:DDENUMRET_OK;
        const auto regions=p.memory.regions().size();
        fault([&]{method(p,s.dd,8,{s.dd,0,0,Context,ModeCallback});},mode==0?wr::FaultKind::memory:wr::FaultKind::unsupported);
        CHECK(regions==p.memory.regions().size());
        fault([&]{p.memory.load(expired,32);},wr::FaultKind::memory);
    }
    write_callback=throw_callback=false;guest={};
    // The caller's DirectDraw reference is no longer needed by the D3D object.
    method(p,s.dd,2,{s.dd});
    for(const auto& device:all.devices){
        GUID iid{};std::memcpy(&iid,device.caps.data()+196,16);put_guid(p,data+128,iid);
        for(auto reply:{D3DENUMRET_OK,D3DENUMRET_CANCEL}){
            Rows expected;expected.reply=reply;const auto hr=native.d3d->EnumZBufferFormats(iid,native_zformat,&expected);
            guest={};guest.reply=reply;CHECK(method(p,obj,6,{obj,data+128,ZCallback,Context})==wr::U32(hr));
            CHECK(guest.formats==expected.formats);
            if(!expected.formats.empty())fault([&]{p.memory.load(expired,32);},wr::FaultKind::memory);
        }
    }
    // Unknown IIDs return the actual native result; unsupported implemented
    // interfaces/methods fail explicitly without writing fake output pointers.
    GUID invalid{0x12345678,0x1234,0x5678,{1,2,3,4,5,6,7,8}};put_guid(p,data+144,invalid);
    void* result=nullptr;auto bad=native.d3d->QueryInterface(invalid,&result);CHECK(FAILED(bad));
    p.memory.store(data+100,0xdeadbeef,32);CHECK(method(p,obj,0,{obj,data+144,data+100})==wr::U32(bad));CHECK(p.memory.load(data+100,32)==0);
    fault([&]{method(p,obj,0,{obj,data+16,data+8192-2});},wr::FaultKind::memory);
    fault([&]{method(p,obj,3,{obj,0,Context});},wr::FaultKind::memory);
    fault([&]{method(p,obj,3,{obj,data,Context});},wr::FaultKind::memory);
    fault([&]{method(p,obj,6,{obj,0,ZCallback,Context});},wr::FaultKind::memory);
    p.memory.store(data+100,0xdeadbeef,32);fault([&]{method(p,obj,4,{obj,data+144,0,data+100});},wr::FaultKind::unsupported);
    CHECK(p.memory.load(data+100,32)==0xdeadbeef);
    for(bool z:{false,true})for(int mode=0;mode<3;++mode){
        guest={};write_callback=mode==0;throw_callback=mode==1;guest.reply=mode==2?2:D3DENUMRET_OK;
        const auto regions=p.memory.regions().size();
        fault([&]{if(z)method(p,obj,6,{obj,data+128,ZCallback,Context});else method(p,obj,3,{obj,DeviceCallback,Context});},mode==0?wr::FaultKind::memory:wr::FaultKind::unsupported);
        fault([&]{p.memory.load(expired,32);},wr::FaultKind::memory);CHECK(p.memory.regions().size()==regions);
    }
    write_callback=throw_callback=false;guest={};CHECK(method(p,obj,3,{obj,DeviceCallback,Context})==D3D_OK);CHECK(guest.devices==all.devices);
    CHECK(method(p,obj,7,{obj})==wr::U32(native.d3d->EvictManagedTextures()));
    // Release from a callback must not invalidate native receiver storage before
    // EnumDevices returns. The guest token itself is retired immediately.
    release_receiver=obj;guest={};guest.reply=D3DENUMRET_CANCEL;
    CHECK(method(p,obj,3,{obj,DeviceCallback,Context})==D3D_OK);
    fault([&]{method(p,obj,3,{obj,DeviceCallback,Context});},wr::FaultKind::unsupported);
    fault([&]{p.memory.store(obj,0,32);},wr::FaultKind::memory);
    CHECK(p.directdraw()->report().find("device_callbacks")!=std::string::npos);
}
void exit_test(){GuestSetup s;guest={};exit_callback=true;CHECK(method(s.p,s.d3d,3,{s.d3d,DeviceCallback,Context})==D3D_OK);CHECK(s.p.exited() && guest.devices.size()==1);fault([&]{s.p.memory.load(expired,32);},wr::FaultKind::memory);exit_callback=false;}
}
int main(){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);try{run();exit_test();std::cout<<checks<<" Direct3D7 native/guest enumeration assertions passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
