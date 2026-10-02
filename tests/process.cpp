#include "winrecomp/process.hpp"
#include "winrecomp/win32_state.hpp"
#include "winrecomp/hash.hpp"
#include <iostream>
#include <random>
#include <fstream>
#include <stdexcept>
#include <cstdlib>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
#define CHECK(x) do{if(!(x))throw std::runtime_error("check failed: " #x);}while(0)
namespace {
wr::Image image(){wr::Image x;x.base=0x400000;x.entry=0x401000;x.size=0x4000;x.headers_size=512;x.bytes.resize(1536);x.sections={{".text",0x1000,512,512,512,0x60000020},{".data",0x2000,0x1800,1024,512,0xc0000040}};x.bytes[512]=0xc3;return x;}
bool step(wr::Cpu& cpu,wr::Memory& memory,std::uint64_t&) {if(cpu.eip==0x401000){wr::check_code(memory,cpu.eip,{0xc3});cpu.eip=wr::pop(cpu,memory);return true;}return false;}
wr::U32 call(wr::Process& p,const std::string& name,std::initializer_list<wr::U32> args={}) {
    // Flush the API boundary before calling host code so native CI faults retain
    // their last known operation rather than leaving an empty test log.
    std::cerr<<"process API: "<<name<<'\n';
    const auto sp=p.cpu.r[wr::ESP],pc=p.cpu.eip;auto nonvolatile=p.cpu.r;
    for(auto it=args.end();it!=args.begin();)wr::push(p.cpu,p.memory,*--it);wr::push(p.cpu,p.memory,pc);p.cpu.eip=p.resolve("kernel32.dll",name);
    CHECK(p.cpu.eip!=0);CHECK(p.dispatch_api());CHECK(p.cpu.r[wr::ESP]==sp);CHECK(p.cpu.eip==pc);
    for(auto r:{wr::EBX,wr::ESI,wr::EDI,wr::EBP})CHECK(p.cpu.r[r]==nonvolatile[r]);return p.cpu.r[wr::EAX];
}
template<class F> void fault(F&& f,wr::FaultKind kind){bool caught=false;try{f();}catch(const wr::GuestFault& e){caught=true;CHECK(e.kind==kind);}CHECK(caught);}
}
int main(){
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    _set_thread_local_invalid_parameter_handler([](const wchar_t*,const wchar_t*,const wchar_t*,unsigned,uintptr_t){
        std::cerr<<"CRT rejected a native argument in process fixture\n";std::abort();
    });
#endif
try{
    std::cerr<<"process fixture started\n";
    CHECK(wr::sha256({})=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const std::string abc="abc";CHECK(wr::sha256(std::span(reinterpret_cast<const std::uint8_t*>(abc.data()),abc.size()))=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::vector<std::uint8_t> million(1000000,'a');CHECK(wr::sha256(million)=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    wr::Process p(step);p.load(image());CHECK(p.cpu.fs_valid);CHECK(p.memory.load(p.cpu.fs_base,32)==0xffffffffu);
    CHECK(p.memory.load(0x403700,32)==0);fault([&]{p.memory.store(0x401000,0,8);},wr::FaultKind::memory);
    CHECK(p.memory.permissions(0x401000)==(wr::Memory::Read|wr::Memory::Execute));fault([&]{p.memory.store(p.options.stack_base,0,8);},wr::FaultKind::memory);
    call(p,"SetLastError",{123});CHECK(call(p,"GetLastError")==123);CHECK(p.memory.load(p.cpu.fs_base+0x34,32)==123);
    auto base=call(p,"VirtualAlloc",{0,12345,0x2000,4});CHECK(base && base%65536==0);fault([&]{p.memory.load(base,8);},wr::FaultKind::memory);
    CHECK(call(p,"VirtualAlloc",{base+4097,4096,0x1000,4})==base+4096);CHECK(p.memory.load(base+8192,32)==0);
    p.memory.store(base+4100,0xabcdef12,32);CHECK(call(p,"VirtualAlloc",{base+4096,4096,0x1000,2})==base+4096);CHECK(p.memory.load(base+4100,32)==0xabcdef12);p.memory.store(base+4100,1,32);
    auto output=p.allocate_bytes(4096);CHECK(call(p,"VirtualProtect",{base+4097,1,2,output})==1);CHECK(p.memory.load(output,32)==4);
    fault([&]{p.memory.store(base+4100,3,8);},wr::FaultKind::memory);CHECK(call(p,"VirtualFree",{base+4096,1,0x4000})==1);
    CHECK(call(p,"VirtualAlloc",{base+4096,4096,0x1000,4})==base+4096);CHECK(p.memory.load(base+4100,32)==0);
    CHECK(call(p,"VirtualFree",{base,0,0x8000})==1);CHECK(call(p,"VirtualFree",{base,0,0x8000})==0);
    CHECK(call(p,"VirtualAlloc",{0,0,0x3000,4})==0);CHECK(call(p,"VirtualAlloc",{0,4096,0x3000,0x100})==0);
    // Invalid output pointer cannot partially change protection.
    auto allocation=call(p,"VirtualAlloc",{0,4096,0x3000,4});
    fault([&]{p.virtual_protect(allocation,4096,2,1);},wr::FaultKind::memory);CHECK(p.memory.permissions(allocation)==3);
    auto heap=call(p,"HeapCreate",{0,0,0});CHECK(heap);auto block=call(p,"HeapAlloc",{heap,8,31});CHECK(block);CHECK(call(p,"HeapSize",{heap,0,block})==31);
    for(unsigned n=0;n<31;++n)CHECK(p.memory.load(block+n,8)==0);p.memory.store(block,0x12345678,32);
    auto grow=call(p,"HeapReAlloc",{heap,8,block,9000});CHECK(grow);CHECK(p.memory.load(grow,32)==0x12345678);CHECK(p.memory.load(grow+8500,32)==0);
    CHECK(call(p,"HeapFree",{heap,0,grow})==1);CHECK(call(p,"HeapFree",{heap,0,grow})==0);CHECK(call(p,"HeapDestroy",{heap})==1);
    // Internal/API-owned pages must not acquire public VirtualFree ownership.
    CHECK(!p.state().virtual_reservations.contains(output));
    auto bounded=call(p,"HeapCreate",{0,0,4095});CHECK(bounded);
    CHECK(p.state().heaps.at(bounded).maximum==4096);
    auto ba=call(p,"HeapAlloc",{bounded,8,1536}),bb=call(p,"HeapAlloc",{bounded,8,1536});CHECK(ba && bb);
    p.memory.store(ba,0x24681357,32);
    CHECK(call(p,"HeapReAlloc",{bounded,8,ba,3000})==0);
    CHECK(call(p,"HeapSize",{bounded,0,ba})==1536 && p.memory.load(ba,32)==0x24681357);
    CHECK(call(p,"VirtualFree",{ba,0,0x8000})==0);
    CHECK(call(p,"VirtualFree",{ba,4096,0x4000})==0);
    CHECK(!p.state().virtual_reservations.contains(ba));
    CHECK(call(p,"HeapDestroy",{bounded})==1);
    CHECK(!p.state().virtual_reservations.contains(ba));
    auto fixed=call(p,"GlobalAlloc",{0,32});p.memory.store(fixed,0x12345678,32);
    CHECK(!p.state().virtual_reservations.contains(fixed));CHECK(call(p,"VirtualFree",{fixed,0,0x8000})==0);
    CHECK(call(p,"GlobalReAlloc",{fixed,64,0x40})==fixed);CHECK(p.memory.load(fixed+32,32)==0);
    CHECK(call(p,"GlobalReAlloc",{fixed,8192,0})==0);CHECK(p.memory.load(fixed,32)==0x12345678);
    auto moved=call(p,"GlobalReAlloc",{fixed,8192,0x42});CHECK(moved && moved!=fixed);CHECK(p.memory.load(moved,32)==0x12345678);CHECK(p.memory.load(moved+7000,32)==0);CHECK(call(p,"GlobalFree",{moved})==0);
    CHECK(call(p,"VirtualFree",{p.image_base,0,0x8000})==0);CHECK(call(p,"VirtualFree",{p.options.stack_base,0,0x4000})==0);CHECK(p.memory.load(p.image_base+0x1000,8)==0xc3);
    auto kh=p.module("kernel32.dll",true);auto feature=p.put_string("IsProcessorFeaturePresent");CHECK(call(p,"GetProcAddress",{kh,feature})==p.resolve("kernel32.dll","IsProcessorFeaturePresent"));CHECK(call(p,"IsProcessorFeaturePresent",{0})==0);
    {wr::Process unsupported(step);unsupported.load(image());auto module=unsupported.module("kernel32.dll",true);auto missing=unsupported.put_string("DefinitelyNotAnImplementedExport");fault([&]{call(unsupported,"GetProcAddress",{module,missing});},wr::FaultKind::unsupported);}
    auto tls=call(p,"TlsAlloc");CHECK(tls!=0xffffffffu);CHECK(call(p,"TlsSetValue",{tls,0xabcdef})==1);call(p,"SetLastError",{99});CHECK(call(p,"TlsGetValue",{tls})==0xabcdef);CHECK(call(p,"GetLastError")==0);CHECK(call(p,"TlsFree",{tls})==1);CHECK(call(p,"TlsAlloc")==tls);CHECK(call(p,"TlsGetValue",{tls})==0);
    auto mutex_name=p.put_string("Local\\WinRecomp-test-"+std::to_string(std::random_device{}()));
    auto mutex=call(p,"CreateMutexA",{0,0,mutex_name});CHECK(mutex);auto same=call(p,"CreateMutexA",{0,1,mutex_name});CHECK(same && same!=mutex);CHECK(call(p,"GetLastError")==183);
    CHECK(call(p,"WaitForSingleObject",{mutex,0})==0);CHECK(call(p,"ReleaseMutex",{same})==1);CHECK(call(p,"ReleaseMutex",{mutex})==0);CHECK(call(p,"GetLastError")==288);
    CHECK(call(p,"CloseHandle",{mutex})==1);CHECK(call(p,"CloseHandle",{same})==1);
    auto cs=p.allocate_bytes(24);call(p,"InitializeCriticalSection",{cs});call(p,"EnterCriticalSection",{cs});call(p,"EnterCriticalSection",{cs});CHECK(p.memory.load(cs+8,32)==2);call(p,"LeaveCriticalSection",{cs});call(p,"LeaveCriticalSection",{cs});CHECK(p.memory.load(cs+4,32)==0xffffffffu);call(p,"DeleteCriticalSection",{cs});
    auto env=call(p,"GetEnvironmentStringsW");CHECK(p.memory.load(env,32)==0);CHECK(call(p,"FreeEnvironmentStringsA",{env})==0);CHECK(call(p,"FreeEnvironmentStringsW",{env})==1);
    auto name=p.put_string("ALPHA"),value=p.put_string("beta");CHECK(call(p,"SetEnvironmentVariableA",{name,value})==1);env=call(p,"GetEnvironmentStringsA");CHECK(p.read_string(env)=="ALPHA=beta");CHECK(call(p,"FreeEnvironmentStringsA",{env})==1);
    auto source=p.put_string("Hello"),destination=p.allocate_bytes(64);CHECK(call(p,"MultiByteToWideChar",{1252,0,source,0xffffffffu,0,0})==6);
    CHECK(call(p,"MultiByteToWideChar",{1252,0,source,0xffffffffu,destination,5})==0);CHECK(call(p,"GetLastError")==122);
    CHECK(call(p,"MultiByteToWideChar",{1252,0,source,0xffffffffu,destination,6})==6);CHECK(p.read_wstring(destination)==u"Hello");
    CHECK(call(p,"WideCharToMultiByte",{1252,0,destination,0xffffffffu,output,64,0,0})==6);CHECK(p.read_string(output)=="Hello");

    auto drive=p.put_string("C:\\"),volume=p.allocate_bytes(1024);
    CHECK(call(p,"GetLogicalDrives")==4);CHECK(call(p,"GetDriveTypeA",{drive})>=2);
    CHECK(call(p,"GetVolumeInformationA",{drive,volume,256,volume+512,volume+516,volume+520,volume+256,256})==1);CHECK(p.memory.load(volume+516,32)>0);CHECK(!p.read_string(volume+256).empty());
    auto message=p.put_string("WinRecomp plain message");CHECK(call(p,"FormatMessageA",{0x600,message,0,0,volume,256,0})==23);CHECK(p.read_string(volume)=="WinRecomp plain message");
    CHECK(call(p,"FormatMessageA",{0x600,message,0,0,volume,1,0})==0);CHECK(call(p,"GetLastError")==122);
    CHECK(call(p,"FormatMessageA",{0x700,message,0,0,volume,256,0})==23);auto allocated_message=p.memory.load(volume,32);CHECK(p.read_string(allocated_message)=="WinRecomp plain message");CHECK(call(p,"LocalFree",{allocated_message})==0);
    {auto saved=p.cpu;auto name_buf=p.allocate_bytes(512);p.memory.store(name_buf,1,32);const auto user=p.resolve("advapi32.dll","GetUserNameA");
     for(auto capacity:{1u,256u}){p.memory.store(name_buf,capacity,32);wr::push(p.cpu,p.memory,name_buf);wr::push(p.cpu,p.memory,name_buf+8);wr::push(p.cpu,p.memory,saved.eip);p.cpu.eip=user;CHECK(p.dispatch_api());CHECK(p.memory.load(name_buf,32)==p.options.user_name.size()+1);CHECK(p.cpu.r[wr::EAX]==(capacity>1?1u:0u));CHECK(p.cpu.r[wr::ESP]==saved.r[wr::ESP]);}
     CHECK(p.read_string(name_buf+8)==p.options.user_name);}
#ifdef _WIN32
    auto a=p.put_string("alpha"),b=p.put_string("ALPHA");auto aw=p.put_wstring(u"alpha"),bw=p.put_wstring(u"ALPHA");
    for(auto flags:{0u,1u}){CHECK(call(p,"CompareStringA",{0x409,flags,a,0xffffffffu,b,0xffffffffu})==unsigned(::CompareStringA(0x409,flags,"alpha",-1,"ALPHA",-1)));CHECK(call(p,"CompareStringW",{0x409,flags,aw,5,bw,5})==unsigned(::CompareStringW(0x409,flags,L"alpha",5,L"ALPHA",5)));}
    char native_message[512]{};auto native_size=::FormatMessageA(0x1200,nullptr,2,0x409,native_message,512,nullptr);CHECK(native_size);
    CHECK(call(p,"FormatMessageA",{0x1200,0,2,0x409,volume,512,0})==native_size);CHECK(p.read_string(volume)==native_message);
#endif
    auto time_buffer=p.allocate_bytes(256);const std::uint64_t epoch=116444736000000000ull;
    p.memory.store(time_buffer,wr::U32(epoch),32);p.memory.store(time_buffer+4,wr::U32(epoch>>32),32);
    CHECK(call(p,"FileTimeToSystemTime",{time_buffer,time_buffer+32})==1);CHECK(p.memory.load(time_buffer+32,16)==1970);CHECK(p.memory.load(time_buffer+34,16)==1);CHECK(p.memory.load(time_buffer+36,16)==4);CHECK(p.memory.load(time_buffer+38,16)==1);
    CHECK(call(p,"GetTimeZoneInformation",{time_buffer+64})!=0xffffffffu);
    auto path=std::filesystem::temp_directory_path()/("winrecomp-process-"+std::to_string(std::random_device{}())+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(path);{std::ofstream out(path/"sample.bin",std::ios::binary);out<<"ABCDEFGHIJ";}
    wr::ProcessOptions file_options;file_options.data_root=path;wr::Process files(step,file_options);files.load(image());
    const auto filename=files.put_string("sample.bin"),file_buffer=files.allocate_bytes(128),count=files.allocate_bytes(4);
    auto handle=call(files,"CreateFileA",{filename,0x80000000u,3,0,3,0x80,0});CHECK(handle!=0xffffffffu);
    CHECK(call(files,"GetFileSize",{handle,0})==10);CHECK(call(files,"SetFilePointer",{handle,3,0,0})==3);
    CHECK(call(files,"ReadFile",{handle,file_buffer,4,count,0})==1);CHECK(files.memory.load(count,32)==4);CHECK(files.memory.load(file_buffer,32)==0x47464544u);
    CHECK(call(files,"SetFilePointer",{handle,0xfffffffeu,0,2})==8);CHECK(call(files,"ReadFile",{handle,file_buffer,16,count,0})==1);CHECK(files.memory.load(count,32)==2);
    auto info=files.allocate_bytes(512);CHECK(call(files,"GetFileInformationByHandle",{handle,info})==1);CHECK(files.memory.load(info+36,32)==10);
    const auto pattern=files.put_string("*.bin");auto found=call(files,"FindFirstFileA",{pattern,info});CHECK(found!=0xffffffffu);CHECK(files.read_string(info+44)=="sample.bin");CHECK(files.memory.load(info+32,32)==10);CHECK(call(files,"FindNextFileA",{found,info})==0);CHECK(call(files,"GetLastError")==18);CHECK(call(files,"FindClose",{found})==1);CHECK(call(files,"FindClose",{found})==0);
    CHECK(call(files,"WriteFile",{handle,file_buffer,2,count,0})==0);CHECK(call(files,"GetLastError")==5);CHECK(call(files,"CloseHandle",{handle})==1);
    CHECK(call(files,"CreateFileA",{filename,0x40000000u,3,0,2,0x80,0})==0xffffffffu);CHECK(std::filesystem::file_size(path/"sample.bin")==10);
    file_options.allow_file_write=true;wr::Process writer(step,file_options);writer.load(image());const auto wn=writer.put_string("sample.bin"),wd=writer.put_string("xyz"),wc=writer.allocate_bytes(4);
    handle=call(writer,"CreateFileA",{wn,0xc0000000u,3,0,3,0x80,0});CHECK(handle!=0xffffffffu);CHECK(call(writer,"WriteFile",{handle,wd,3,wc,0})==1);
    CHECK(call(writer,"FlushFileBuffers",{handle})==1);CHECK(call(writer,"SetEndOfFile",{handle})==1);CHECK(call(writer,"GetFileSize",{handle,0})==3);CHECK(call(writer,"CloseHandle",{handle})==1);
    std::ifstream file_read(path/"sample.bin",std::ios::binary);std::string file_text((std::istreambuf_iterator<char>(file_read)),{});CHECK(file_text=="xyz");file_read.close();
    auto renamed=writer.put_string("renamed.bin");CHECK(call(writer,"MoveFileA",{wn,renamed})==1);CHECK(!std::filesystem::exists(path/"sample.bin"));
    {std::ofstream collision(path/"sample.bin");collision<<"preserve";}CHECK(call(writer,"MoveFileA",{renamed,wn})==0);CHECK(std::filesystem::file_size(path/"sample.bin")==8);CHECK(call(writer,"DeleteFileA",{wn})==1);CHECK(call(writer,"MoveFileA",{renamed,wn})==1);
    CHECK(call(writer,"SetFileAttributesA",{wn,1})==1);CHECK(call(writer,"GetFileAttributesA",{wn})&1u);CHECK(call(writer,"SetFileAttributesA",{wn,0x80})==1);CHECK(!(call(writer,"GetFileAttributesA",{wn})&1u));
    auto folder=writer.put_string("directory");CHECK(call(writer,"CreateDirectoryA",{folder,0})==1);CHECK(call(writer,"CreateDirectoryA",{folder,0})==0);CHECK(call(writer,"GetLastError")==183);CHECK(call(writer,"RemoveDirectoryA",{folder})==1);
    auto exclusive=call(writer,"CreateFileA",{wn,0x80000000u,0,0,3,0x80,0});CHECK(exclusive!=0xffffffffu);CHECK(call(writer,"CreateFileA",{wn,0x80000000u,3,0,3,0x80,0})==0xffffffffu);CHECK(call(writer,"GetLastError")==32);CHECK(call(writer,"DeleteFileA",{wn})==0);CHECK(call(writer,"GetLastError")==32);CHECK(call(writer,"CloseHandle",{exclusive})==1);CHECK(call(writer,"DeleteFileA",{wn})==1);
    std::filesystem::remove_all(path);
    // Existing code can execute; modified code and unregistered targets cannot.
    auto saved=p.cpu;p.cpu.r[wr::EAX]=7;CHECK(p.run()==7);
    wr::Process q(step);q.load(image());q.memory.protect(0x401000,4096,7);q.memory.store(0x401000,0x90,8);fault([&]{q.run();},wr::FaultKind::unsupported);
    wr::Process u(step);u.load(image());u.cpu.eip=u.resolve("unknown.dll","Missing",true);fault([&]{u.run();},wr::FaultKind::unsupported);
    auto with_tls=image();with_tls.directories[9]={0x2000,24};wr::Process t(step);bool rejected=false;try{t.load(with_tls);}catch(const std::exception&){rejected=true;}CHECK(rejected);
    std::cout<<"process, image, virtual-memory, heap, TLS-slot, ABI and text checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
