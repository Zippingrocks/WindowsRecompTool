"""Actual decoded bytes -> generated C++ -> compiled x64, versus independent x86 execution.

Unicorn is an optional external GPL test process/library, NOT linked to WinRecomp
or to generated programs. Defined status flags are compared; undefined flags
are explicitly excluded. Fixtures are our own, and real game bytes stay local.
"""
from __future__ import annotations
import argparse
import ctypes as ct
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import shutil
import struct
import subprocess
import sys
import time
import unicorn as uc
from unicorn import x86_const as x86
from fixtures import make_pe

ROOT=Path(__file__).resolve().parents[1]
STACK=0x07000000
STACK_SIZE=0x10000
ESP=STACK+0x8000
STOP=0x0badf00d
STATUS=0x8d5
REGS=[x86.UC_X86_REG_EAX,x86.UC_X86_REG_ECX,x86.UC_X86_REG_EDX,x86.UC_X86_REG_EBX,x86.UC_X86_REG_ESP,x86.UC_X86_REG_EBP,x86.UC_X86_REG_ESI,x86.UC_X86_REG_EDI]
# Byte fixtures deliberately exercise operand widths, subregisters, aliases and flags.
CASES={
 'add32':'01d8c3', 'adc32':'11d8c3', 'sub32':'29d8c3', 'sbb32':'19d8c3',
 'add16':'6601d8c3','adc16':'6611d8c3','sub16':'6629d8c3','sbb16':'6619d8c3',
 'add8':'00d8c3','adc8':'10d8c3','sub8':'28d8c3','sbb8':'18d8c3',
 'high8':'10dcc3','subregisters':'b47f6689d8b0aac3',
 'logic':'21d809d031c8c3','logic16':'6621d86609d0c3','logic8':'20d808ccc3',
 'cmp32':'39d8c3','cmp16':'6639d8c3','cmp8':'38d8c3','test':'85d8c3',
 'neg32':'f7d8c3','neg16':'66f7d8c3','neg8':'f6d8c3','not':'f7d0c3',
 'inc_dec':'40484b43c3','inc16':'66ffc0c3','dec8':'feccc3',
 'stack':'505b515ac3','push_imm8':'6aff58c3','push16':'6650665bc3',
 'prologue':'5589e583ec108945fc8b4dfc89c8c9c3',
 'stack_memory':'8b4424040144240803442408c3',
 'nop':'90c3',
 'movzx':'0fb6c40fb7d8c3','movsx':'0fbec4660fbed8c3',
 'lea':'8d447304c3','lea16':'678d407fc3',
 'flags':'f8f5f9f5c3','direct_call':'e801000000c301d8c3',
 'bounded_loop':'b905000000014424044975f9c3',
 'ret_imm':'c20800',
}
# Each branch has two observably different paths; MOV preserves the tested flags.
for opcode in range(0x70,0x80):
    CASES[f'jcc_{opcode:02x}']=f'{opcode:02x}06b811111111c3b822222222c3'
CASES['jecxz']='e306b811111111c3b822222222c3'
CASES['jcxz']='67e306b811111111c3b822222222c3'


def run(args: list[str], **kw) -> subprocess.CompletedProcess:
    p=subprocess.run(args,capture_output=True,text=True,timeout=120,**kw)
    if p.returncode:
        raise RuntimeError(f"command failed ({p.returncode}): {args}\n{p.stdout}\n{p.stderr}")
    return p


def compile_library(directory: Path, names: list[str], cxx: str) -> ct.CDLL:
    includes='\n'.join(f'#include "{name}.cpp"' for name in names)
    dispatch='\n'.join(f'case {i}: {name}(s,m,stop,10000);break;' for i,name in enumerate(names))
    wrapper=includes+r'''
#include <string>
#if defined(_WIN32)
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C" __attribute__((visibility("default")))
#endif
static thread_local std::string last_error;
EXPORT const char* wr_last_error(){return last_error.c_str();}
EXPORT int wr_execute(unsigned id,wr::U32* state,unsigned char* stack,std::size_t stack_size,
 const unsigned char* image,std::size_t image_size,wr::U32 image_base,wr::U32 stop) {
 try {
  wr::Cpu s;for(unsigned i=0;i<8;++i)s.r[i]=state[i];
  s.eip=state[8];s.flags=state[9];s.defined_flags=state[10];
  wr::Memory m;m.map(image_base,image_size,wr::Memory::Read);
  m.initialize(image_base,std::span(image,image_size));
  m.map(0x07000000u,stack_size,wr::Memory::Read|wr::Memory::Write);
  m.initialize(0x07000000u,std::span(stack,stack_size));
  switch(id){
'''+dispatch+r'''
  default:throw std::runtime_error("unknown fixture");
  }
  for(unsigned i=0;i<8;++i)state[i]=s.r[i];state[8]=s.eip;state[9]=s.flags;state[10]=s.defined_flags;
  m.copy_out(0x07000000u,std::span(stack,stack_size));return 0;
 }catch(const std::exception& e){last_error=e.what();return 1;}
}
'''
    (directory/'wrapper.cpp').write_text(wrapper,encoding='utf-8')
    if Path(cxx).name.lower() in ('cl','cl.exe'):
        library=directory/'cases.dll'
        run([cxx,'/nologo','/std:c++20','/EHsc','/O2','/LD',f'/I{ROOT / "include"}',str(directory/'wrapper.cpp'),f'/Fe:{library}'],cwd=directory)
    else:
        library=directory/('cases.dll' if os.name=='nt' else 'cases.so')
        run([cxx,'-std=c++20','-O2','-shared','-fPIC','-I',str(ROOT/'include'),str(directory/'wrapper.cpp'),'-o',str(library)],cwd=directory)
    dll=ct.CDLL(str(library))
    dll.wr_execute.argtypes=[ct.c_uint,ct.POINTER(ct.c_uint32),ct.POINTER(ct.c_ubyte),ct.c_size_t,ct.POINTER(ct.c_ubyte),ct.c_size_t,ct.c_uint32,ct.c_uint32]
    dll.wr_execute.restype=ct.c_int
    dll.wr_last_error.restype=ct.c_char_p
    return dll


def vectors(count: int, seed: int):
    rng=random.Random(seed)
    edges=[0,1,0xffffffff,0x7fffffff,0x80000000,0xff,0x80,0x7f,0xffff,0x8000,0x7fff,0xffffff80]
    for k in range(count):
        regs=[rng.getrandbits(32) for _ in range(8)]
        if k<len(edges)*len(edges):regs[0]=edges[k%len(edges)];regs[3]=edges[k//len(edges)]
        regs[4]=ESP;regs[5]=ESP+0x100;regs[6]=STACK+0x400;regs[7]=STACK+0x600
        if k%5==0:regs[1]=0
        flags=0x202 | (rng.getrandbits(12)&STATUS)
        memory=bytearray(STACK_SIZE)
        for pos in range(0x7f00,0x8200,4):struct.pack_into('<I',memory,pos,rng.getrandbits(32))
        struct.pack_into('<I',memory,ESP-STACK,STOP)
        yield regs,flags,memory


def check_case(dll,case_id:int,name:str,mapped:bytes,base:int,entry:int,count:int):
    machine=uc.Uc(uc.UC_ARCH_X86,uc.UC_MODE_32)
    machine.mem_map(base,(len(mapped)+4095)&~4095)
    machine.mem_write(base,mapped)
    machine.mem_map(STACK,STACK_SIZE)
    original=machine.context_save()
    image_array=(ct.c_ubyte*len(mapped)).from_buffer_copy(mapped)
    case_seed=int.from_bytes(hashlib.sha256(name.encode()).digest()[:4],'little')
    for k,(regs,flags,memory) in enumerate(vectors(count,case_seed)):
        machine.context_restore(original)
        machine.mem_write(STACK,bytes(memory))
        for r,value in zip(REGS,regs):machine.reg_write(r,value)
        machine.reg_write(x86.UC_X86_REG_EFLAGS,flags)
        try:machine.emu_start(entry,STOP,count=100000)
        except uc.UcError as e:raise RuntimeError(f'{name} vector {k}: oracle {e}, eip={machine.reg_read(x86.UC_X86_REG_EIP):#x}') from e
        if machine.reg_read(x86.UC_X86_REG_EIP)!=STOP:raise RuntimeError(f'{name}: oracle budget exhausted')
        expected=[machine.reg_read(r) for r in REGS]+[STOP,machine.reg_read(x86.UC_X86_REG_EFLAGS)]
        state=(ct.c_uint32*11)(*regs,entry,flags,STATUS)
        native_memory=(ct.c_ubyte*len(memory)).from_buffer_copy(memory)
        status=dll.wr_execute(case_id,state,native_memory,len(memory),image_array,len(mapped),base,STOP)
        if status:raise RuntimeError(f'{name} vector {k}: native {dll.wr_last_error().decode()}')
        actual=list(state)
        if actual[:9]!=expected[:9] or ((actual[9]^expected[9])&actual[10]&STATUS):
            raise RuntimeError(f'{name} vector {k}: mismatch\ninput={regs} flags={flags:#x}\nactual={actual}\nexpected={expected}')
        oracle_memory=bytes(machine.mem_read(STACK,STACK_SIZE))
        if bytes(native_memory)!=oracle_memory:
            first=next(i for i,(a,b) in enumerate(zip(native_memory,oracle_memory)) if a!=b)
            raise RuntimeError(f'{name} vector {k}: memory mismatch at {STACK+first:#x}')
    return {'name':name,'entry':hex(entry),'vectors':count,'result':'pass'}


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--tool',required=True,type=Path)
    ap.add_argument('--cxx',default=os.environ.get('CXX','c++'))
    ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--vectors',type=int,default=256)
    ap.add_argument('--target',type=Path)
    ap.add_argument('--entry',type=lambda x:int(x,0),action='append')
    args=ap.parse_args()
    if args.vectors<1:ap.error('vectors must be positive')
    args.tool=args.tool.resolve();args.out=args.out.resolve();args.out.mkdir(parents=True,exist_ok=True)
    names=[];images=[];started=time.perf_counter()
    if args.target:
        if not args.entry:ap.error('--target requires --entry')
        pe=json.loads(run([str(args.tool),'analyze',str(args.target)]).stdout)
        path=args.out/'target-mapped.bin';run([str(args.tool),'dump-image',str(args.target),str(path)])
        mapped=path.read_bytes()
        for i,address in enumerate(args.entry):
            name=f'fixture_{i}'
            run([str(args.tool),'lift',str(args.target),str(args.out/(name+'.cpp')),'--entry',hex(address),'--name',name])
            names.append(name);images.append((f'target_{address:08x}',mapped,pe['image_base'],address))
    else:
        for i,(label,code) in enumerate(CASES.items()):
            name=f'fixture_{i}';p=args.out/(name+'.exe');p.write_bytes(make_pe(bytes.fromhex(code)))
            run([str(args.tool),'lift',str(p),str(args.out/(name+'.cpp')),'--name',name])
            image_path=args.out/(name+'.bin');run([str(args.tool),'dump-image',str(p),str(image_path)])
            names.append(name);images.append((label,image_path.read_bytes(),0x400000,0x401000))
    library=compile_library(args.out,names,args.cxx)
    results=[]
    for i,(label,image,base,entry) in enumerate(images):
        results.append(check_case(library,i,label,image,base,entry,args.vectors))
        print(f'PASS {label}: {args.vectors} vectors',flush=True)
    report={'schema':'winrecomp.conformance.v1','oracle':'Unicorn '+uc.__version__,'native_pointer_bits':ct.sizeof(ct.c_void_p)*8,
            'platform':platform.platform(),'compiler':args.cxx,'cases':results,'total_vectors':sum(x['vectors'] for x in results),
            'compared':'8 GPRs, EIP, defined status flags, complete 64 KiB stack','elapsed_seconds':round(time.perf_counter()-started,3)}
    if args.target:report['target_sha256']=hashlib.sha256(args.target.read_bytes()).hexdigest()
    (args.out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='cases'},indent=2))

if __name__=='__main__':main()
