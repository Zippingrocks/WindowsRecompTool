"""Run emitted x86 D3D7 callbacks against a real host COM implementation.

Uses only author-written fixture bytes. The independent C++ side calls native
IDirect3D7 directly and compares every defined descriptor byte plus names.
--runner labels a Wine diagnostic, never a native Windows acceptance result.
"""
from pathlib import Path
import argparse
import json
import shutil
import struct
import subprocess
import sys
import uuid
from window_program import Assembler
from fixtures import make_pe, put32
ROOT=Path(__file__).resolve().parents[1]
DATA=0x402000
DRAW,D3D,IID,COUNT,REPLY,CONTEXT=DATA+0x900,DATA+0x904,DATA+0x920,DATA+0x940,DATA+0x944,0xa13579bc
RECORDS=DATA+0x1000

def fixture():
    b=Assembler()
    def com(obj,slot,*args):
        for value in reversed(((obj,),)+args):
            if isinstance(value,tuple):b.memory_push(value[0])
            else:b.push(value)
        b.emit('a1');b.word(obj);b.emit('8b00ff90');b.word(slot*4)
    b.invoke('DDRAW.dll','DirectDrawCreate',0,DRAW,0);b.eax_is(0,'create')
    com(DRAW,0,IID,D3D);b.eax_is(0,'query D3D7')
    callback_at=len(b.code)+6
    com(D3D,3,0,CONTEXT);b.eax_is(0,'enumeration')
    com(D3D,2);com(DRAW,2);b.emit('31c0c3')
    b.label('callback');b.emit('558bec535657')
    b.emit('817d14');b.word(CONTEXT);b.fail('callback context','0f85')
    b.emit('a1');b.word(COUNT);b.emit('83f810');b.fail('callback row budget','0f83')
    b.emit('c1e00805');b.word(RECORDS);b.emit('89c7')
    b.emit('8b450889078b450c894704') # store both guest string pointers
    b.emit('8b751083c708b937000000fcf3a5') # copy 55 defined DWORDs; reserved tail stays zero
    b.increment(COUNT)
    b.emit('a1');b.word(REPLY);b.emit('5f5e5bc9c21000')
    b.finish();entry=0x401000+b.labels['callback']
    assert b.code[callback_at-1]==0x68
    struct.pack_into('<I',b.code,callback_at,entry)
    data=bytearray(0x3000);by_dll={}
    for _,dll,name in b.calls:
        names=by_dll.setdefault(dll,[])
        if name not in names:names.append(name)
    at=0x100;symbols={}
    for index,(dll,names) in enumerate(by_dll.items()):
        text=dll.encode()+b'\0';dll_at=at;data[at:at+len(text)]=text;at=(at+len(text)+3)&~3
        lookup=at;at+=4*(len(names)+1);iat=at;at+=4*(len(names)+1)
        struct.pack_into('<IIIII',data,index*20,0x2000+lookup,0,0,0x2000+dll_at,0x2000+iat)
        for n,name in enumerate(names):
            text=b'\0\0'+name.encode()+b'\0';data[at:at+len(text)]=text
            struct.pack_into('<I',data,lookup+n*4,0x2000+at);struct.pack_into('<I',data,iat+n*4,0x2000+at)
            symbols[dll,name]=DATA+iat+n*4;at=(at+len(text)+1)&~1
    for pos,dll,name in b.calls:struct.pack_into('<I',b.code,pos,symbols[dll,name])
    data[IID-DATA:IID-DATA+16]=uuid.UUID('f5049e77-4861-11d2-a407-00a0c90629a8').bytes_le
    raw=make_pe(b.code,data);put32(raw,0x98+104,0x2000);put32(raw,0x98+108,20*(len(by_dll)+1))
    return raw,entry

def run(cmd,**kw):
    value=subprocess.run([str(x) for x in cmd],capture_output=True,text=True,timeout=180,**kw)
    if value.returncode:raise RuntimeError(f'{cmd}\n{value.stdout}\n{value.stderr}')
    return value

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--tool',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--toolchain',type=Path);ap.add_argument('--runner',type=Path);a=ap.parse_args()
    if sys.platform!='win32' and not (a.runner and a.toolchain):raise RuntimeError('Needs Windows, or an explicitly labeled Windows-target/Wine diagnostic')
    out=a.out.resolve();out.mkdir(parents=True,exist_ok=True);raw,callback=fixture();pe=out/'fixture.exe';pe.write_bytes(raw)
    project=out/'project';shutil.rmtree(project,ignore_errors=True);run([a.tool.resolve(),'project',pe,project,'--seed',hex(callback)])
    (out/'check.cpp').write_text(r'''#include <winrecomp/process.hpp>
#include <array>
#include <cstring>
#include <iostream>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#define DIRECTDRAW_VERSION 0x0700
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
bool compiled_step(wr::Cpu&,wr::Memory&,std::uint64_t&);
struct Row{std::string description,name;std::array<std::uint8_t,220> bytes;};
struct Rows{std::vector<Row> rows;HRESULT reply;};
HRESULT CALLBACK receive(char* desc,char* name,D3DDEVICEDESC7* caps,void* state){auto& ctx=*static_cast<Rows*>(state);Row r{desc,name,{}};static_assert(sizeof(*caps)==236);std::memcpy(r.bytes.data(),caps,220);ctx.rows.push_back(r);return ctx.reply;}
struct Owned{IDirectDraw* dd{};IDirect3D7* d3d{};~Owned(){if(d3d)d3d->Release();if(dd)dd->Release();}};
int main(int argc,char** argv){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);try{
if(argc!=2)throw std::runtime_error("fixture required");auto image=wr::Image::load(argv[1]);Owned native;
if(FAILED(DirectDrawCreate(nullptr,&native.dd,nullptr)) || FAILED(native.dd->QueryInterface(IID_IDirect3D7,reinterpret_cast<void**>(&native.d3d))))throw std::runtime_error("real host Direct3D7 unavailable");
unsigned total=0;
for(unsigned n=0;n<32;++n){
Rows expected{{},HRESULT(n%2)};if(FAILED(native.d3d->EnumDevices(receive,&expected)) || expected.rows.empty())throw std::runtime_error("native device enumeration failed");
wr::Process p(compiled_step);p.load(image);p.memory.store(0x402944,n%2,32);
if(p.run()!=0 || !p.exited() || p.memory.load(0x402940,32)!=expected.rows.size())throw std::runtime_error("compiled enumeration count/result differs");
for(unsigned i=0;i<expected.rows.size();++i){auto at=0x403000+i*256;auto& row=expected.rows[i];std::array<std::uint8_t,220> bytes{};p.memory.copy_out(at+8,bytes);
if(bytes!=row.bytes || p.read_string(p.memory.load(at,32))!=row.description || p.read_string(p.memory.load(at+4,32))!=row.name)throw std::runtime_error("native/compiled caps or names differ");
for(unsigned k=228;k<256;k+=4)if(p.memory.load(at+k,32))throw std::runtime_error("output canary corrupted");++total;}
}
std::cout<<"{\"runs\":32,\"rows_compared\":"<<total<<",\"all_passed\":true}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
''')
    (out/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(D3D7Execution LANGUAGES C CXX)\nset(CMAKE_CXX_STANDARD 20)\nset(BUILD_TESTING OFF CACHE BOOL "" FORCE)\nadd_subdirectory("'+ROOT.as_posix()+'" winrecomp)\nfile(GLOB GENERATED "${CMAKE_CURRENT_SOURCE_DIR}/project/code_page_*.cpp")\nadd_executable(d3d7_execution check.cpp project/dispatch.cpp ${GENERATED})\ntarget_link_libraries(d3d7_execution PRIVATE winrecomp_guest)\n')
    command=['cmake','-S',out,'-B',out/'build','-G','Ninja','-DCMAKE_BUILD_TYPE=Release']
    if a.toolchain:command+=['-DCMAKE_TOOLCHAIN_FILE='+str(a.toolchain.resolve()),'-DCMAKE_EXE_LINKER_FLAGS=-static -static-libgcc -static-libstdc++','-DCMAKE_CXX_FLAGS_RELEASE=-O1 -DNDEBUG']
    run(command);run(['cmake','--build',out/'build','--target','d3d7_execution','--parallel',3])
    executable=out/'build/d3d7_execution.exe'
    command=[a.runner.resolve(),executable,'Z:'+str(pe).replace('/','\\')] if a.runner else [executable,pe]
    result=run(command);print(result.stdout);stats=json.loads(result.stdout)
    evidence={'schema':'winrecomp.d3d7-execution.v1','host':'Wine on Linux (not Windows)' if a.runner else 'Windows','oracle':'Direct native host IDirect3D7 enumeration, not an original PE32 process','compiled_guest':True,'descriptor_bytes_compared_per_row':220,'reserved_excluded':True,'strings_compared':True,**stats}
    (out/'report.json').write_text(json.dumps(evidence,indent=2)+'\n')
if __name__=='__main__':main()
