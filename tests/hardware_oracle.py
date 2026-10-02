"""Optional *native i386* oracle. Linux x86-64 with IA32 execution enabled only.

Builds freestanding ELF32 harnesses from our own PE fixtures. No 32-bit C library
is needed. Exit code 77 means unsupported host, never a passing conformance run.
Game binaries are deliberately not accepted by this hardware test runner.
"""
from __future__ import annotations
import argparse
import ctypes as ct
import json
from pathlib import Path
import platform
import struct
import subprocess
import time
import conformance as c
from fixtures import make_pe


def assemble_oracle(folder: Path, image: bytes, vectors: bytes, count: int) -> Path:
    (folder/'image.bin').write_bytes(image)
    (folder/'vectors.bin').write_bytes(vectors)
    source=r'''
.section .resume,"ax"
.space 13,0x90
resume:
 mov %eax, output+0
 mov %ecx, output+4
 mov %edx, output+8
 mov %ebx, output+12
 mov %esp, output+16
 mov %ebp, output+20
 mov %esi, output+24
 mov %edi, output+28
 mov $0x0badf00d, %eax
 mov %eax, output+32
 mov $host_stack_top, %esp
 pushfl
 popl output+36
 mov $output,%ecx
 mov $40,%edx
 call write_all
 mov $guest_stack,%ecx
 mov $65536,%edx
 call write_all
 incl iteration
 cmpl $VECTOR_COUNT,iteration
 je exit_success
 jmp next_vector
.section .text
.global _start
_start:
 mov $host_stack_top,%esp
next_vector:
 cld
 mov cursor,%esi
 mov $input,%edi
 mov $9,%ecx
 rep movsl
 mov $guest_stack,%edi
 mov $16384,%ecx
 rep movsl
 mov %esi,cursor
 pushl input+32
 popfl
 mov input+0,%eax
 mov input+4,%ecx
 mov input+8,%edx
 mov input+12,%ebx
 mov input+20,%ebp
 mov input+24,%esi
 mov input+28,%edi
 mov input+16,%esp
 jmp guest_image+0x1000
write_all:
 test %edx,%edx
 jz written
 mov $4,%eax
 mov $1,%ebx
 int $0x80
 test %eax,%eax
 jle io_error
 add %eax,%ecx
 sub %eax,%edx
 jmp write_all
written:
 ret
io_error:
 mov $2,%ebx
 jmp exit
exit_success:
 xor %ebx,%ebx
exit:
 mov $1,%eax
 int $0x80
.section .data
cursor: .long vectors
iteration: .long 0
input: .space 36
output: .space 40
.section .bss
.space 4096
host_stack_top:
.section .guest_stack,"aw",@nobits
.align 4096
guest_stack: .space 65536
.section .guest,"ax"
guest_image: .incbin "image.bin"
.section .vectors,"a"
vectors: .incbin "vectors.bin"
.section .note.GNU-stack,"",@progbits
'''.replace('VECTOR_COUNT',str(count))
    (folder/'oracle.s').write_text(source)
    c.run(['as','--32','oracle.s','-o','oracle.o'],cwd=folder)
    c.run(['ld','-m','elf_i386','-Ttext=0x08049000','-Tdata=0x08050000',
           '--section-start=.guest=0x00400000','--section-start=.guest_stack=0x07000000',
           '--section-start=.vectors=0x09000000','--section-start=.resume=0x0badf000',
           'oracle.o','-o','oracle'],cwd=folder)
    return folder/'oracle'


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--tool',required=True,type=Path)
    ap.add_argument('--out',required=True,type=Path)
    ap.add_argument('--cxx',default='c++')
    ap.add_argument('--vectors',type=int,default=32)
    a=ap.parse_args()
    if platform.system()!='Linux' or platform.machine() not in ('x86_64','amd64'):
        print('SKIP: native i386 oracle requires Linux x86-64');return 77
    a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=True);a.tool=a.tool.resolve()
    started=time.perf_counter();names=[];images=[]
    for i,(label,code) in enumerate(c.CASES.items()):
        name=f'fixture_{i}';p=a.out/(name+'.exe');p.write_bytes(make_pe(bytes.fromhex(code)))
        c.run([str(a.tool),'lift',str(p),str(a.out/(name+'.cpp')),'--name',name])
        mapped=a.out/(name+'.bin');c.run([str(a.tool),'dump-image',str(p),str(mapped)])
        names.append(name);images.append((label,mapped.read_bytes()))
    dll=c.compile_library(a.out,names,a.cxx);reports=[]
    for case_id,(label,image) in enumerate(images):
        rows=list(c.vectors(a.vectors,case_id+8675309))
        raw=b''.join(struct.pack('<9I',*regs,flags)+bytes(mem) for regs,flags,mem in rows)
        oracle=assemble_oracle(a.out,image,raw,len(rows))
        try:
            process=subprocess.run([str(oracle)],capture_output=True,timeout=30)
        except OSError as e:
            if e.errno==8:
                print('SKIP: this kernel disallows ELF32 execution');return 77
            raise
        if process.returncode:raise RuntimeError(f'{label}: original i386 oracle exited {process.returncode}')
        width=40+c.STACK_SIZE
        if len(process.stdout)!=len(rows)*width:raise RuntimeError('truncated oracle results')
        img=(ct.c_ubyte*len(image)).from_buffer_copy(image)
        for n,(regs,flags,mem) in enumerate(rows):
            result=process.stdout[n*width:(n+1)*width]
            expected=struct.unpack('<10I',result[:40])
            state=(ct.c_uint32*11)(*regs,0x401000,flags,c.STATUS)
            memory=(ct.c_ubyte*len(mem)).from_buffer_copy(mem)
            status=dll.wr_execute(case_id,state,memory,len(mem),img,len(image),0x400000,c.STOP)
            if status:raise RuntimeError(dll.wr_last_error().decode())
            actual=list(state)
            if actual[:9]!=list(expected[:9]) or ((actual[9]^expected[9])&actual[10]&c.STATUS):
                raise RuntimeError(f'{label} vector {n}: real hardware mismatch: {actual} vs {expected}')
            if bytes(memory)!=result[40:]:raise RuntimeError(f'{label} vector {n}: hardware memory mismatch')
        reports.append({'name':label,'vectors':len(rows),'result':'pass'})
        print(f'PASS native i386 {label}: {len(rows)} vectors',flush=True)
    report={'oracle':'native i386 ELF32 execution, not emulation','native_output_bits':ct.sizeof(ct.c_void_p)*8,
            'cases':reports,'total_vectors':sum(x['vectors'] for x in reports),'elapsed_seconds':round(time.perf_counter()-started,3)}
    (a.out/'hardware-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='cases'},indent=2));return 0

if __name__=='__main__':raise SystemExit(main())
