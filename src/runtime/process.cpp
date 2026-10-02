#include "winrecomp/process.hpp"
#include "winrecomp/hash.hpp"
#include "winrecomp/win32_state.hpp"
#include <cctype>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
namespace wr {
namespace {
std::string canonical(std::string s) {for(auto& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;}
U32 page_size(U32 size) {
    const auto n=(std::uint64_t(size)+4095)&~4095ull;
    if(!size || n>512ull*1024*1024)throw std::runtime_error("invalid guest allocation size");return U32(n);
}
unsigned protection(U32 value) {
    switch(value){case 0x01:return 0;case 0x02:return Memory::Read;case 0x04:return Memory::Read|Memory::Write;
    case 0x10:return Memory::Execute;case 0x20:return Memory::Execute|Memory::Read;case 0x40:return Memory::Execute|Memory::Read|Memory::Write;
    default:throw std::runtime_error("unsupported page protection (guard/copy-on-write/cache modifiers not implemented)");}
}
U32 native_protection(unsigned value){switch(value){case 0:return 1;case 1:return 2;case 3:return 4;case 4:return 0x10;case 5:return 0x20;case 7:return 0x40;default:throw std::runtime_error("invalid guest protection state");}}
constexpr U32 PROCESS_RETURN=0xffffffe0,CALLBACK_RETURN=0xffffffd0;
}
Process::Process(StepFunction step,ProcessOptions opts):win32_(std::make_unique<Win32State>()),step_(step),options(std::move(opts)),budget(options.instruction_budget) {
    if(!step_)throw std::runtime_error("missing compiled dispatch function");
    if(!budget)throw std::runtime_error("instruction budget must be nonzero");
    options.data_root=std::filesystem::canonical(options.data_root);
    install_win32(*this);
}
Process::~Process()=default;
Win32State& Process::state(){return *win32_;}
void Process::load(const Image& image) {
    if(loaded_)throw std::runtime_error("process already has an image");
    if(image.base%4096 || image.size%4096)throw std::runtime_error("runtime requires page-aligned image base and size");
    if(image.directories[13].rva)throw std::runtime_error("delay imports not supported by this loader");
    if(image.directories[14].rva)throw std::runtime_error("managed PE runtime not supported");
    if(image.directories[9].rva)throw std::runtime_error("static PE TLS loading is not yet implemented; refusing partial initialization");
    // Image bytes, .bss, gaps and header protection are explicit. The full image
    // is reserved first; page permissions are the union of resident sections.
    memory.map(image.base,image.size,0);auto mapped=image.mapped();memory.initialize(image.base,mapped);
    std::vector<unsigned> permissions(image.size/4096,0);
    for(std::uint64_t p=0;p<image.headers_size;p+=4096)permissions.at(std::size_t(p/4096))|=Memory::Read;
    for(const auto& section:image.sections)if(section.span()) {
        const unsigned access=((section.characteristics&0x40000000u)?Memory::Read:0u)|((section.characteristics&0x80000000u)?Memory::Write:0u)|((section.characteristics&0x20000000u)?Memory::Execute:0u);
        for(std::uint64_t p=section.rva/4096;p<=(std::uint64_t(section.rva)+section.span()-1)/4096;++p)permissions.at(std::size_t(p))|=access;
    }
    for(std::size_t n=0;n<permissions.size();++n)memory.protect(image.base+U32(n*4096),4096,permissions[n]);
    image_base=image.base;image_size=image.size;entry=image.entry;input_sha256=sha256(image.bytes);
    if(!options.stack_size || options.stack_base%4096 || options.stack_size%4096 || std::uint64_t(options.stack_base)+options.stack_size>0x100000000ull)throw std::runtime_error("invalid guest stack");
    memory.map(options.stack_base,options.stack_size,Memory::Read|Memory::Write);
    memory.protect(options.stack_base,4096,0); // fixed guard; no silent stack growth
    if(options.teb%4096)throw std::runtime_error("TEB must be page aligned");
    memory.map(options.teb,4096,Memory::Read|Memory::Write);cpu.fs_base=options.teb;cpu.fs_valid=true;
    memory.store(options.teb,0xffffffffu,32);memory.store(options.teb+4,options.stack_base+options.stack_size,32);
    memory.store(options.teb+8,options.stack_base+4096,32);memory.store(options.teb+0x18,options.teb,32);
    memory.store(options.teb+0x20,1,32);memory.store(options.teb+0x24,1,32);
    cpu.eip=entry;cpu.r[ESP]=options.stack_base+options.stack_size-16;push(cpu,memory,PROCESS_RETURN);
    for(const auto& item:image.imports) {
        module(item.dll,true);const std::string name=item.ordinal?"#"+std::to_string(*item.ordinal):item.name;
        const auto thunk=resolve(item.dll,name,true);
        const std::array<std::uint8_t,4> bytes{std::uint8_t(thunk),std::uint8_t(thunk>>8),std::uint8_t(thunk>>16),std::uint8_t(thunk>>24)};
        memory.initialize(item.iat,bytes);
    }
    loaded_=true;set_error(0);
}
void Process::register_api(const std::string& dll,const std::string& name,unsigned arguments,std::function<U32(Process&)> fn,bool caller_cleans_stack) {
    if(arguments>64 || !fn)throw std::runtime_error("invalid API registration");
    const auto key=canonical(dll)+"!"+name;
    if(api_names_.contains(key))throw std::runtime_error("duplicate API registration: "+key);
    if(next_thunk_>=0xf1000000)throw std::runtime_error("thunk space exhausted");
    const auto at=next_thunk_;next_thunk_+=16;api_names_[key]=at;apis_.emplace(at,Api{canonical(dll),name,arguments,caller_cleans_stack,std::move(fn)});
}
U32 Process::resolve(const std::string& dll,const std::string& name,bool allow_unimplemented) {
    const auto key=canonical(dll)+"!"+name;auto found=api_names_.find(key);
    if(found!=api_names_.end())return found->second;
    if(!allow_unimplemented){set_error(127);return 0;}
    if(next_thunk_>=0xf1000000)throw std::runtime_error("thunk space exhausted");
    const auto at=next_thunk_;next_thunk_+=16;api_names_[key]=at;apis_.emplace(at,Api{canonical(dll),name,0,false,{}});return at;
}
U32 Process::module(const std::string& dll,bool create) {
    auto key=canonical(dll);if(key.empty())return image_base;
    const auto path=key.find_last_of("/\\");if(path!=std::string::npos)key=key.substr(path+1);
    if(key.find('.')==std::string::npos)key+=".dll";
    if(auto it=modules_.find(key);it!=modules_.end())return it->second;
    if(!create){set_error(126);return 0;}
    const auto value=next_module_;next_module_+=0x10000;modules_[key]=value;return value;
}
std::string Process::module_name(U32 handle) const {for(const auto& [name,value]:modules_)if(value==handle)return name;return {};}
U32 Process::argument(unsigned index) {
    const auto at=std::uint64_t(cpu.r[ESP])+4+std::uint64_t(index)*4;
    if(at+4>0x100000000ull)throw GuestFault(FaultKind::memory,cpu.r[ESP],"API argument stack wraps");return memory.load(U32(at),32);
}
bool Process::dispatch_api() {
    const auto it=apis_.find(cpu.eip);if(it==apis_.end())return false;const auto& api=it->second;
    const auto return_address=memory.load(cpu.r[ESP],32);
    std::vector<U32> arguments;for(unsigned n=0;n<api.arguments;++n)arguments.push_back(argument(n));
    std::string detail;
    if(!arguments.empty() && (api.name=="CreateFileA" || api.name=="FindFirstFileA" || api.name=="CreateDirectoryA" || api.name=="CreateMutexA")) {
        try{const auto pointer=api.name=="CreateMutexA"?arguments.at(2):arguments[0];if(pointer)detail=read_string(pointer,1024);}catch(const std::exception&){detail="<unreadable bounded guest name>";}
    }
    recent_transfers.push_back({cpu.eip,return_address,api.dll+"!"+api.name,std::move(arguments),detail});if(recent_transfers.size()>32)recent_transfers.pop_front();
    if(!api.function)throw GuestFault(FaultKind::unsupported,cpu.eip,"unimplemented Win32 import "+api.dll+"!"+api.name);
    memory.check(cpu.r[ESP],(api.arguments+1)*4,Memory::Read);
    const auto stack=cpu.r[ESP];++api_calls;const auto value=api.function(*this);
    if(cpu.r[ESP]!=stack)throw GuestFault(FaultKind::unsupported,cpu.eip,"host API corrupted guest stack state");
    cpu.r[EAX]=value;cpu.r[ESP]+=4+(api.caller_cleans_stack?0:api.arguments*4);cpu.eip=return_address;return true;
}
U32 Process::callback(U32 address,std::span<const U32> args,bool stdcall) {
    if(args.size()>64)throw std::runtime_error("too many callback arguments");
    if(state().callback_depth>=64)throw GuestFault(FaultKind::budget,address,"guest callback nesting limit");
    const auto saved=cpu;++state().callback_depth;
    try {
        memory.check(saved.r[ESP]-U32((args.size()+1)*4),(args.size()+1)*4,Memory::Write);
        for(auto it=args.rbegin();it!=args.rend();++it)push(cpu,memory,*it);push(cpu,memory,CALLBACK_RETURN);cpu.eip=address;
        while(cpu.eip!=CALLBACK_RETURN && !exited_) {
            tick(budget,cpu.eip);if(dispatch_api())continue;
            if(!step_(cpu,memory,budget))throw GuestFault(FaultKind::untranslated,cpu.eip,"untranslated callback instruction");++executed_instructions;
        }
        if(!exited_ && cpu.r[ESP]!=saved.r[ESP]-(stdcall?0u:U32(args.size()*4)))throw GuestFault(FaultKind::unsupported,cpu.eip,"callback ABI stack cleanup mismatch");
        if(!exited_)for(auto r:{EBX,ESI,EDI,EBP})if(cpu.r[r]!=saved.r[r])throw GuestFault(FaultKind::unsupported,cpu.eip,"callback did not preserve a nonvolatile register");
        const auto result=cpu.r[EAX];auto fp=cpu.fp;cpu=saved;cpu.fp=fp;--state().callback_depth;return result;
    }catch(...){--state().callback_depth;throw;}
}
U32 Process::run() {
    if(!loaded_)throw std::runtime_error("no loaded image");
    while(!exited_ && cpu.eip!=PROCESS_RETURN) {
        tick(budget,cpu.eip);if(dispatch_api())continue;
        if(!step_(cpu,memory,budget))throw GuestFault(FaultKind::untranslated,cpu.eip,"untranslated instruction target");++executed_instructions;
    }
    if(!exited_)exit(cpu.r[EAX]);return exit_code_;
}
void Process::set_error(U32 value){last_error=value;if(cpu.fs_valid)memory.store(cpu.fs_base+0x34,value,32);}
U32 Process::virtual_alloc(U32 at,U32 size,U32 allocation,U32 protect) {
    try {
        if(allocation&~0x3000u || !(allocation&0x3000u)){set_error(87);return 0;}
        const auto perm=protection(protect);const bool reserve=(allocation&0x2000u)!=0 || !at;
        const auto alignment=reserve?65536u:4096u;const auto base=at&~(alignment-1);
        const auto expanded=std::uint64_t(size)+(at-base);
        if(!size || expanded>512ull*1024*1024){set_error(87);return 0;}
        const auto length=page_size(U32(expanded));U32 chosen=base;
        if(!at) {
            chosen=0x10000000;
            while(!memory.available(chosen,length)) {if(chosen>0x6fff0000u-length){set_error(8);return 0;}chosen+=65536;}
        }
        if(reserve){memory.reserve(chosen,length);try{if(allocation&0x1000u)memory.commit(chosen,length,perm);state().virtual_reservations.insert(chosen);}catch(...){memory.release(chosen);throw;}}
        else memory.commit(chosen,length,perm);
        return chosen;
    }catch(const std::exception&){set_error(487);return 0;}
}
bool Process::virtual_free(U32 at,U32 size,U32 operation) {
    try {
        if(operation==0x8000 && size==0){if(!state().virtual_reservations.contains(at)){set_error(487);return false;}memory.release(at);state().virtual_reservations.erase(at);return true;}
        if(operation==0x4000){auto region=memory.allocation(at);if(!state().virtual_reservations.contains(region.base)){set_error(487);return false;}if(!size){if(at!=region.base){set_error(87);return false;}memory.decommit(at,region.size);return true;}
            const auto base=at&~4095u;const auto expanded=std::uint64_t(size)+at-base;
            if(expanded>512ull*1024*1024){set_error(87);return false;}memory.decommit(base,page_size(U32(expanded)));return true;}
        set_error(87);return false;
    }catch(const std::exception&){set_error(487);return false;}
}
bool Process::virtual_protect(U32 at,U32 size,U32 protect,U32 old) {
    if(!size){set_error(87);return false;}
    memory.check(old,4,Memory::Write); // caller pointer faults are not allocation errors
    try {const auto base=at&~4095u;const auto expanded=std::uint64_t(size)+at-base;if(expanded>512ull*1024*1024){set_error(87);return false;}
        const auto prior=memory.protect(base,page_size(U32(expanded)),protection(protect));memory.store(old,native_protection(prior),32);return true;
    }catch(const std::exception&){set_error(487);return false;}
}
U32 Process::allocate_bytes(std::size_t size,unsigned permission) {
    if(size>512ull*1024*1024)throw std::runtime_error("oversized host-to-guest copy");
    auto p=virtual_alloc(0,U32(std::max<std::size_t>(size,1)),0x3000,native_protection(permission));if(!p)throw std::runtime_error("guest allocation failed");return p;
}
std::string Process::read_string(U32 p,std::size_t maximum) {
    std::string result;for(std::size_t n=0;n<maximum;++n){if(std::uint64_t(p)+n>0xffffffffull)throw GuestFault(FaultKind::memory,p,"string wraps");auto c=memory.load(p+U32(n),8);if(!c)return result;result+=char(c);}throw GuestFault(FaultKind::memory,p,"unterminated bounded guest string");
}
std::u16string Process::read_wstring(U32 p,std::size_t maximum) {
    std::u16string result;for(std::size_t n=0;n<maximum;++n){if(std::uint64_t(p)+n*2+2>0x100000000ull)throw GuestFault(FaultKind::memory,p,"wide string wraps");auto c=memory.load(p+U32(n*2),16);if(!c)return result;result+=char16_t(c);}throw GuestFault(FaultKind::memory,p,"unterminated bounded guest wide string");
}
U32 Process::put_string(const std::string& string){auto p=allocate_bytes(string.size()+1);memory.copy_in(p,std::span(reinterpret_cast<const std::uint8_t*>(string.c_str()),string.size()+1));return p;}
U32 Process::put_wstring(const std::u16string& string){auto p=allocate_bytes((string.size()+1)*2);for(std::size_t n=0;n<=string.size();++n)memory.store(p+U32(n*2),n==string.size()?0:U32(string[n]),16);return p;}
std::string Process::report() const {
    std::ostringstream out;out<<"{\"schema\":\"winrecomp.process.v1\",\"input_sha256\":"<<quote(input_sha256)<<",\"exited\":"<<(exited_?"true":"false")<<",\"exit_code\":"<<exit_code_<<",\"eip\":"<<cpu.eip<<",\"esp\":"<<cpu.r[ESP]<<",\"native_instructions\":"<<executed_instructions<<",\"api_calls\":"<<api_calls<<",\"budget_left\":"<<budget<<",\"recent_transfers\":[";
    bool first=true;for(const auto& x:recent_transfers){if(!first)out<<',';first=false;out<<"{\"pc\":"<<x.pc<<",\"return\":"<<x.return_address<<",\"api\":"<<quote(x.api)<<",\"arguments\":[";for(std::size_t n=0;n<x.arguments.size();++n){if(n)out<<',';out<<x.arguments[n];}out<<"],\"detail\":"<<quote(x.detail)<<"}";}out<<"]}";return out.str();
}
int run_program(int argc,char** argv,StepFunction step,const char* expected_sha256) {
    std::unique_ptr<Process> process;std::string report_path;
    try {
        if(argc<2)throw std::runtime_error("usage: recompiled_program original.exe [--root data-directory] [--budget N] [--report report.json] [--allow-write]");
        ProcessOptions options;options.image_name=std::filesystem::path(argv[1]).filename().string();options.command_line='"'+std::filesystem::path(argv[1]).filename().string()+'"';
        for(int n=2;n<argc;++n){const std::string arg=argv[n];if(arg=="--allow-write"){options.allow_file_write=true;continue;}
            if(n+1>=argc)throw std::runtime_error("missing option value");const std::string value=argv[++n];
            if(arg=="--root")options.data_root=value;else if(arg=="--report")report_path=value;else if(arg=="--command-line")options.command_line=value;
            else if(arg=="--budget"){std::size_t used{};options.instruction_budget=std::stoull(value,&used);if(used!=value.size() || value.empty() || value[0]=='-')throw std::runtime_error("invalid budget");}
            else throw std::runtime_error("unknown runtime option: "+arg);
        }
        if(!report_path.empty()) {
            const auto input=std::filesystem::weakly_canonical(argv[1]),output=std::filesystem::weakly_canonical(report_path);
            if(input==output || (std::filesystem::exists(input) && std::filesystem::exists(output) && std::filesystem::equivalent(input,output))){report_path.clear();throw std::runtime_error("report must not overwrite the input executable");}
        }
        const auto image=Image::load(argv[1]);if(sha256(image.bytes)!=expected_sha256)throw std::runtime_error("input SHA-256 does not match statically translated image");
        process=std::make_unique<Process>(step,options);process->load(image);const auto result=process->run();
        const auto report=process->report();if(!report_path.empty())write_file(report_path,report+"\n");std::cout<<report<<'\n';return int(result&255u);
    }catch(const std::exception& e) {
        // Even failures during option parsing must never turn the diagnostic
        // path into permission to overwrite the input (including hard links).
        try { if(argc>1 && !report_path.empty()) {
            const auto input=std::filesystem::weakly_canonical(argv[1]);
            const auto output=std::filesystem::weakly_canonical(report_path);
            if(input==output || (std::filesystem::exists(input) && std::filesystem::exists(output) && std::filesystem::equivalent(input,output)))report_path.clear();
        }}catch(...){report_path.clear();}
        const auto report=std::string("{\"error\":")+quote(e.what())+",\"process\":"+(process?process->report():"null")+"}\n";
        try{if(!report_path.empty())write_file(report_path,report);}catch(const std::exception& report_error){std::cerr<<report_error.what()<<'\n';}
        std::cerr<<report;return 2;
    }
}
}
