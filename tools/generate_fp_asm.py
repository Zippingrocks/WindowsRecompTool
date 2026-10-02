"""Emit fixed x64 native x87 helpers for GAS and MASM.

These are build-time instruction helpers, not a runtime decoder/JIT. The lifter
whitelists decoded x87 operations before referencing a helper. Reserved encodings
are never exposed by the public runtime API. Host x87/XMM/MXCSR state is saved and
restored around each operation. The caller supplies checked bounce-buffer data.
"""
from pathlib import Path
import argparse
from sse_recipes import recipes

def helper(name,code,masm):
    # All working GPRs are volatile under BOTH Windows x64 and SysV AMD64.
    args=['mov r10, rcx','mov r11, rdx','mov r9, r8'] if masm else ['mov r10, rdi','mov r11, rsi','mov r9, rdx']
    def db(values):return ('db '+', '.join('0'+f'{v:02X}'+'h' for v in values)) if masm else '.byte '+', '.join(hex(v) for v in values)
    lines=[f'PUBLIC {name}',f'{name} PROC FRAME'] if masm else [f'.globl {name}',f'.type {name}, @function',f'{name}:']
    lines+=args+['sub rsp, 520']
    if masm:lines+=['.allocstack 520','.endprolog']
    lines += [db([0x48,0x0f,0xae,0x04,0x24]), # FXSAVE64 [RSP]
              db([0x49,0x0f,0xae,0x0a]),      # FXRSTOR64 [R10]
              'pushfq','pop rax','and eax, 0FFFFF72Ah' if masm else 'and eax, 0xfffff72a',
              'mov ecx, DWORD PTR [r9]','and ecx, 08D5h' if masm else 'and ecx, 0x8d5',
              'or eax, ecx','push rax','popfq',db(code),'pushfq','pop rax','mov DWORD PTR [r9], eax',
              db([0x49,0x0f,0xae,0x02]),      # FXSAVE64 [R10]
              db([0x48,0x0f,0xae,0x0c,0x24]), # FXRSTOR64 [RSP]
              'add rsp, 520','ret']
    lines += [f'{name} ENDP'] if masm else [f'.size {name}, .-{name}']
    return '\n'.join(lines)+'\n'

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,required=True);a=ap.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    for masm,ext in [(False,'S'),(True,'asm')]:
        out='option casemap:none\n.code\n' if masm else '.intel_syntax noprefix\n.text\n'
        for opcode in range(0xd8,0xe0):
            for digit in range(8):out+=helper(f'wr_x87_{opcode}_m{digit}',[0x41,opcode,digit*8+3],masm)
            for modrm in range(0xc0,0x100):out+=helper(f'wr_x87_{opcode}_r{modrm}',[opcode,modrm],masm)
        for prefix,opcode in recipes():
            lead=[prefix] if prefix else []
            for dest in range(8):
                for source in range(8):
                    out+=helper(f'wr_sse_{prefix}_{opcode}_r{dest*8+source}',lead+[0x0f,opcode,0xc0+dest*8+source],masm)
                out+=helper(f'wr_sse_{prefix}_{opcode}_m{dest}',lead+[0x41,0x0f,opcode,dest*8+3],masm)
        out+='END\n' if masm else '.section .note.GNU-stack,"",@progbits\n'
        (a.out/f'x87_helpers.{ext}').write_text(out)
if __name__=='__main__':main()
