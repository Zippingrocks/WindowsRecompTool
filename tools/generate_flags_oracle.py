"""Author-written userspace FLAGS oracle. Only tests; never product code.

Original POPF/POPFQ and PUSHFQ execute uninterrupted with controlled inputs.
The caller excludes TF/NT/AC to avoid host traps and restores host flags before
returning to C++. The long-mode 32-bit comparison is an EFLAGS/CPL3 oracle,
not evidence of running a whole original PE32 game.
"""
from pathlib import Path
import argparse
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--windows',action='store_true');a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
gas=['.text'];masm=['.code']
for width in (16,32):
    name=f'wr_flags_oracle_{width}'
    # Preserve flags in R8. MOV instructions leave flags unchanged. Initial
    # argument register use depends on the actual host calling convention.
    old,new=('ecx','edx') if a.windows else ('edi','esi')
    ins=['pushfq','pop r8',f'mov eax,{old}',f'mov edx,{new}' if new!='edx' else 'mov edx,edx','push rax','popfq']
    ins+=['push dx','popfw'] if width==16 else ['push rdx','popfq']
    ins+=['pushfq','pop rax','push r8','popfq','ret']
    gas+=['.intel_syntax noprefix',f'.globl {name}',f'{name}:']+ins+['.att_syntax prefix']
    mins=['pushfq','pop r8','mov eax,ecx','mov edx,edx','push rax','popfq']
    mins+=['push dx','db 66h,9dh'] if width==16 else ['push rdx','popfq']
    mins+=['pushfq','pop rax','push r8','popfq','ret']
    masm+=[f'PUBLIC {name}',f'{name} PROC']+mins+[f'{name} ENDP']
if not a.windows:gas+=['.section .note.GNU-stack,"",@progbits']
masm+=['END']
(a.out/'flags_oracle.S').write_text('\n'.join(gas)+'\n');(a.out/'flags_oracle.asm').write_text('\n'.join(masm)+'\n')
