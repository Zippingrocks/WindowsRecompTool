"""Generated C++ x87 vs uninterrupted original FP instructions on native x64.

This is a hardware x87 oracle, not an IA-32 whole-program oracle. Only our own
FP-only, dual-mode instruction fixtures are accepted. Pointer-bearing environment
fields and undefined empty-register payloads are excluded, explicitly. The main
integer suite independently compares against Unicorn and native i386 on CI.
"""
from __future__ import annotations
import argparse
import ctypes as ct
import json
import os
from pathlib import Path
import random
import struct
import time
import conformance as c
from fixtures import make_pe
from fp_cases import cases


def original_asm(name,code,masm):
    def db(v):return 'db '+', '.join('0'+f'{b:02X}'+'h' for b in v) if masm else '.byte '+', '.join(hex(b) for b in v)
    x=[f'PUBLIC {name}',f'{name} PROC FRAME'] if masm else [f'.globl {name}',f'.type {name}, @function',f'{name}:']
    x+=['push rsi']+(['.pushreg rsi'] if masm else [])+['push rdi']+(['.pushreg rdi'] if masm else [])
    x+=['mov r10, rcx','mov rsi, rdx','mov rdi, r8','mov r11, r9'] if masm else ['mov r10, rdi','mov r11, rcx','mov rdi, rdx']
    x+=['sub rsp, 520']+(['.allocstack 520','.endprolog'] if masm else [])
    x += [db([0x48,0xf,0xae,4,0x24]),db([0x49,0xf,0xae,0xa]),'pushfq','pop rax',
          'and eax, 0FFFFF72Ah' if masm else 'and eax, 0xfffff72a',
          'mov ecx, DWORD PTR [r11+4]','and ecx, 08D5h' if masm else 'and ecx, 0x8d5',
          'or eax, ecx','push rax','popfq','mov eax, DWORD PTR [r11]',db(bytes.fromhex(code)[:-1]),
          'mov DWORD PTR [r11], eax','pushfq','pop rax','mov DWORD PTR [r11+4], eax',
          db([0x49,0xf,0xae,2]),db([0x48,0xf,0xae,0xc,0x24]),'add rsp, 520','pop rdi','pop rsi','ret']
    x+=[f'{name} ENDP'] if masm else [f'.size {name}, .-{name}']
    return '\n'.join(x)+'\n'


def inputs(fmt,k,rng):
    b=bytearray(128)
    if fmt.startswith('simd'):
        if fmt=='simd_integer':
            b[:]=rng.randbytes(128)
            # Include both useful and out-of-range packed shift counts.
            struct.pack_into('<Q',b,16,[0,1,15,16,31,32,63,64,255][k%9])
        elif fmt=='simd32':
            edges=[0,0x80000000,0x3f800000,0xbf800000,1,0x7fffff,0x800000,0x7f7fffff,0x7f800000,0xff800000,0x7fc12345,0x7fa12345]
            for n in range(32):struct.pack_into('<I',b,n*4,edges[(k+n*5)%len(edges)] if k<48 else rng.getrandbits(32))
        else:
            edges=[0,1<<63,0x3ff0000000000000,0xbff0000000000000,1,0xfffffffffffff,0x10000000000000,0x7fefffffffffffff,0x7ff0000000000000,0xfff0000000000000,0x7ff8123456789abc,0x7ff0123456789abc]
            for n in range(16):struct.pack_into('<Q',b,n*8,edges[(k+n*5)%len(edges)] if k<48 else rng.getrandbits(64))
    elif fmt=='mxcsr':struct.pack_into('<I',b,0,0x1f80|((k%4)<<13)|((k%2)<<6)|((k%2)<<15))
    elif fmt=='f32':
        edges=[0x00000000,0x80000000,0x3f800000,0xbf800000,0x00000001,0x007fffff,0x00800000,0x7f7fffff,0x7f800000,0xff800000,0x7fc12345,0x7fa12345]
        if k< len(edges)*2:
            struct.pack_into('<II',b,0,edges[k%len(edges)],edges[(k*7+2)%len(edges)])
        else:struct.pack_into('<ff',b,0,rng.uniform(-0.999,0.999),rng.uniform(0.01,0.999))
    elif fmt=='f64':struct.pack_into('<dd',b,0,rng.uniform(-1e120,1e120),rng.uniform(1e-100,1e100))
    elif fmt in ('i16','i32','i64'):
        size=int(fmt[1:]);v=rng.getrandbits(size);b[:size//8]=v.to_bytes(size//8,'little')
    elif fmt=='ext80':
        sig=rng.getrandbits(63)|(1<<63);exp=rng.randrange(1,0x7fff)|(rng.randrange(2)<<15)
        struct.pack_into('<QH',b,0,sig,exp)
    return b


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--tool',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--native-lib',type=Path,required=True);ap.add_argument('--cxx',default='c++');ap.add_argument('--vectors',type=int,default=192)
    ap.add_argument('--simd',action='store_true');a=ap.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=True);a.tool=a.tool.resolve();a.native_lib=a.native_lib.resolve()
    started=time.perf_counter()
    if a.simd:
        from simd_cases import cases as simd_cases
        items=simd_cases()
    else:items=cases()
    names=[];images=[];assembly='option casemap:none\n.code\n' if os.name=='nt' else '.intel_syntax noprefix\n.text\n'
    for n,case in enumerate(items):
        name=f'fp_case_{n}';path=a.out/(name+'.exe');path.write_bytes(make_pe(bytes.fromhex(case['code'])))
        c.run([str(a.tool),'lift',str(path),str(a.out/(name+'.cpp')),'--name',name])
        mapped=a.out/(name+'.bin');c.run([str(a.tool),'dump-image',str(path),str(mapped)])
        names.append(name);images.append(mapped.read_bytes());assembly+=original_asm('original_'+name,case['code'],os.name=='nt')
    assembly+='END\n' if os.name=='nt' else '.section .note.GNU-stack,"",@progbits\n'
    asm=a.out/('original.asm' if os.name=='nt' else 'original.S');asm.write_text(assembly)
    obj=a.out/('original.obj' if os.name=='nt' else 'original.o')
    if os.name=='nt':c.run(['ml64','/nologo','/c',f'/Fo{obj}',str(asm)],cwd=a.out)
    else:c.run([a.cxx,'-c',str(asm),'-o',str(obj)],cwd=a.out)
    wrapper='\n'.join(f'#include "{n}.cpp"\nextern "C" void original_{n}(void*,void*,void*,wr::U32*);' for n in names)+r'''
#if defined(_WIN32)
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C" __attribute__((visibility("default")))
#endif
static thread_local std::string error;
EXPORT const char* fp_error(){return error.c_str();}
EXPORT int fp_run(unsigned id,unsigned char* fp,unsigned char* source,unsigned char* destination,wr::U32* integer,const unsigned char* image,std::size_t size,bool original){
try {
  wr::Cpu s;std::copy_n(fp,512,s.fp.bytes.begin());s.r[wr::EAX]=integer[0];s.flags=integer[1];s.eip=0x401000;
  alignas(16) std::array<unsigned char,128> aligned_source{},aligned_destination{};
  std::copy_n(source,128,aligned_source.begin());std::copy_n(destination,128,aligned_destination.begin());
  if(original){switch(id){
'''+''.join(f'case {k}:original_{n}(s.fp.bytes.data(),aligned_source.data(),aligned_destination.data(),integer);break;\n' for k,n in enumerate(names))+r'''
  default:throw std::runtime_error("bad original fixture id");}}
  else {
    wr::Memory m;m.map(0x400000,size,wr::Memory::Read);m.initialize(0x400000,std::span(image,size));
    m.map(0x07000000,65536,wr::Memory::Read|wr::Memory::Write);s.r[wr::ESI]=0x07000400;s.r[wr::EDI]=0x07000800;s.r[wr::ESP]=0x07008000;
    m.initialize(s.r[wr::ESI],std::span(source,128));m.initialize(s.r[wr::EDI],std::span(destination,128));m.store(s.r[wr::ESP],0x0badf00d,32);
    switch(id){
'''+''.join(f'case {k}:{n}(s,m,0x0badf00d,10000);break;\n' for k,n in enumerate(names))+r'''
    default:throw std::runtime_error("bad compiled fixture id");}
    integer[0]=s.r[wr::EAX];integer[1]=s.flags;
    m.copy_out(0x07000800,std::span(destination,128));
  }
  if(original)std::copy(aligned_destination.begin(),aligned_destination.end(),destination);
  std::copy(s.fp.bytes.begin(),s.fp.bytes.end(),fp);return 0;
}catch(const std::exception& e){error=e.what();return 1;}}
'''
    cpp=a.out/'wrapper.cpp';cpp.write_text(wrapper)
    lib=a.out/('fp_cases.dll' if os.name=='nt' else 'fp_cases.so')
    if Path(a.cxx).name.lower() in ('cl','cl.exe'):
        c.run([a.cxx,'/nologo','/std:c++20','/EHsc','/O2','/LD',f'/I{c.ROOT/"include"}',str(cpp),str(obj),str(a.native_lib),f'/Fe:{lib}'],cwd=a.out)
    else:c.run([a.cxx,'-std=c++20','-O2','-shared','-fPIC','-I',str(c.ROOT/'include'),str(cpp),str(obj),str(a.native_lib),'-o',str(lib)],cwd=a.out)
    dll=ct.CDLL(str(lib));P=ct.POINTER(ct.c_ubyte);dll.fp_run.argtypes=[ct.c_uint,P,P,P,ct.POINTER(ct.c_uint32),P,ct.c_size_t,ct.c_bool];dll.fp_run.restype=ct.c_int;dll.fp_error.restype=ct.c_char_p
    results=[]
    for k,(case,image) in enumerate(zip(items,images)):
        rng=random.Random(k+28101);img=(ct.c_ubyte*len(image)).from_buffer_copy(image)
        for v in range(a.vectors):
            fp=bytearray(512);cw=[0x37f,0x27f,0x7f][v%3]|((v//3%4)<<10);struct.pack_into('<H',fp,0,cw);struct.pack_into('<I',fp,24,0x1f80)
            fp[160:288]=rng.randbytes(128)
            if a.simd:struct.pack_into('<I',fp,24,0x1f80|((v%4)<<13)|(((v//4)%2)<<6)|(((v//8)%2)<<15)|(rng.getrandbits(6)))
            src=inputs(case['format'],v,rng);ints=[rng.getrandbits(32),0x202|(rng.getrandbits(12)&c.STATUS)]
            values=[]
            for original in [False,True]:
                state=(ct.c_ubyte*512).from_buffer_copy(fp);source=(ct.c_ubyte*128).from_buffer_copy(src);dest=(ct.c_ubyte*128)();integers=(ct.c_uint32*2)(*ints)
                status=dll.fp_run(k,state,source,dest,integers,img,len(image),original)
                if status:raise RuntimeError(f'{case["name"]}/{v}: '+dll.fp_error().decode())
                values.append((bytes(state),bytes(dest),list(integers)))
            actual,expected=values
            if actual[0][24:28]!=expected[0][24:28]:raise RuntimeError(f'{case["name"]}/{v} MXCSR mismatch {actual[0][24:28].hex()} != {expected[0][24:28].hex()}')
            if actual[0][160:288]!=expected[0][160:288]:raise RuntimeError(f'{case["name"]}/{v} XMM register mismatch {actual[0][160:288].hex()} != {expected[0][160:288].hex()}')
            if actual[0][:5]!=expected[0][:5]:raise RuntimeError(f'{case["name"]}/{v} CW/SW/tag mismatch {actual[0][:5].hex()} != {expected[0][:5].hex()}')
            top=(struct.unpack_from('<H',expected[0],2)[0]>>11)&7;tag=expected[0][4]
            for r in range(8):
                off=32+r*16
                if tag&(1<<((top+r)&7)) and actual[0][off:off+10]!=expected[0][off:off+10]:
                    raise RuntimeError(f'{case["name"]}/{v} ST({r}) mismatch {actual[0][off:off+10].hex()} != {expected[0][off:off+10].hex()}')
            if actual[2][0]!=expected[2][0] or ((actual[2][1]^expected[2][1])&case.get("flag_mask",c.STATUS)):raise RuntimeError(f'{case["name"]}/{v} GPR/flags mismatch {actual[2]} != {expected[2]}')
            am,em=bytearray(actual[1]),bytearray(expected[1])
            if case['environment']:
                # Original native pointers cannot equal the generated guest pointers.
                am[12:24]=em[12:24]=bytes(12)
                # Payload of empty x87 registers is undefined after INIT/stack faults.
                if case['name']=='save_restore':
                    tagword=struct.unpack_from('<H',em,8)[0];saved_top=(struct.unpack_from('<H',em,4)[0]>>11)&7
                    for r in range(8):
                        if ((tagword>>(((saved_top+r)&7)*2))&3)==3:am[28+r*10:38+r*10]=em[28+r*10:38+r*10]=bytes(10)
            if am!=em:
                off=next(n for n,(x,y) in enumerate(zip(am,em)) if x!=y)
                raise RuntimeError(f'{case["name"]}/{v} memory mismatch at {off}: {am.hex()} != {em.hex()}')
        results.append({'name':case['name'],'vectors':a.vectors,'result':'pass'});print('PASS native '+('SSE2' if a.simd else 'x87'),case['name'],a.vectors,flush=True)
    report={'schema':'winrecomp.'+('sse2' if a.simd else 'x87')+'-conformance.v1','oracle':'native x86-64 hardware, uninterrupted original '+('SSE/SSE2' if a.simd else 'x87')+' byte fixtures in long mode','native_pointer_bits':ct.sizeof(ct.c_void_p)*8,'cases':results,'total_vectors':len(results)*a.vectors,'control_modes':16 if a.simd else 12,'elapsed_seconds':round(time.perf_counter()-started,3),'exclusions':['FIP/FDP/FOP host-versus-guest environment fields','empty-register payloads'],'not_a_native_i386_whole_program_oracle':True}
    (a.out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='cases'},indent=2))
if __name__=='__main__':main()
