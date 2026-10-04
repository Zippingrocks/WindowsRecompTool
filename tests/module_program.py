"""Compiled PE32/x64 module fallback and CPU-query comparison.

Only author-written fixtures. On Windows the original runs under the native
PE32 loader. Missing DLL names are random, never the game's libraries.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import uuid
from program import imports
from window_program import Assembler
from fixtures import put32
import conformance as c

RECORD=0x402a00
NAME=0x402900
KERNEL=0x402940
MISSING=0x402980
WRITTEN=0x402a40
HANDLE=0x402a44

def fixture(missing: str):
    b=Assembler();k='KERNEL32.dll'
    # Original and generated code execute the same flag/CPUID instructions.
    # Compare the stable vendor/basic-leaf query, not per-core APIC identifiers.
    b.emit('9c589c5e3500002000509d9c5831f02500002000')
    b.eax_is(0x200000,'ID flag round-trip');b.store(RECORD)
    b.emit('569d31c031c90fa2');b.store(RECORD+4)
    for op,offset in [('891d',8),('890d',12),('8915',16)]:b.emit(op);b.word(RECORD+offset)
    b.invoke(k,'LoadLibraryA',KERNEL);b.nonzero('load known bridge');b.store(HANDLE)
    b.invoke(k,'GetModuleHandleA',KERNEL);b.emit('3b05');b.word(HANDLE);b.fail('same loaded module identity','0f85')
    b.invoke(k,'FreeLibrary',(HANDLE,));b.nonzero('release additional module reference')
    b.invoke(k,'GetModuleHandleA',KERNEL);b.nonzero('static module still exists')
    b.invoke(k,'SetLastError',0)
    b.invoke(k,'LoadLibraryA',MISSING);b.eax_is(0,'missing DLL returns NULL')
    b.invoke(k,'GetLastError');b.eax_is(126,'missing DLL last error');b.store(RECORD+20)
    b.invoke(k,'CreateFileA',NAME,0x40000000,0,0,2,0x80,0)
    b.emit('83f8ff');b.fail('file must open','0f84');b.store(HANDLE)
    b.invoke(k,'WriteFile',(HANDLE,),RECORD,24,WRITTEN,0);b.nonzero('write record');b.compare(WRITTEN,24,'written size')
    b.invoke(k,'CloseHandle',(HANDLE,));b.nonzero('close output')
    b.invoke(k,'ExitProcess',0);b.emit('c3');b.finish()
    names=sorted({name for _,_,name in b.calls})
    for pos,_,name in b.calls:struct.pack_into('<I',b.code,pos,0x402180+4*names.index(name))
    raw=imports(b.code,names)
    for offset,value in [(4,(len(b.code)+511)&~511),(8,4096),(20,0x1000),(24,0x2000),(40,5),(48,5),(72,0x100000),(76,0x1000),(80,0x100000),(84,0x1000)]:put32(raw,0x98+offset,value)
    at=struct.unpack_from('<I',raw,0x98+224+40+20)[0]
    for address,text in [(NAME,'cpu-module.bin'),(KERNEL,'kernel32.dll'),(MISSING,missing)]:
        encoded=text.encode('ascii')+b'\0';raw[at+address-0x402000:at+address-0x402000+len(encoded)]=encoded
    assert len(b.code)<4096
    return raw

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--tool',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--cxx',default='c++');a=ap.parse_args()
    a.out=a.out.resolve();a.tool=a.tool.resolve();a.out.mkdir(parents=True,exist_ok=True)
    name='wr_absent_'+uuid.uuid4().hex+'.dll';pe=a.out/'input.exe';pe.write_bytes(fixture(name))
    project=a.out/'project';shutil.rmtree(project,ignore_errors=True)
    c.run([str(a.tool),'project',str(pe),str(project)])
    c.run(['cmake','-S',str(project),'-B',str(project/'build'),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',f'-DWINRECOMP_SOURCE={c.ROOT}',f'-DCMAKE_CXX_COMPILER={a.cxx}'])
    c.run(['cmake','--build',str(project/'build'),'--target','recompiled_program','--parallel','2'])
    exe=project/'build'/('recompiled_program.exe' if c.os.name=='nt' else 'recompiled_program')
    outputs={};runs={}
    labels=['original','generated'] if c.os.name=='nt' else ['generated']
    for label in labels:
        root=a.out/label;root.mkdir(exist_ok=True);out=root/'cpu-module.bin'
        if out.exists():out.unlink()
        cmd=[str(pe)] if label=='original' else [str(exe),str(pe),'--root',str(root),'--dll-dir','.', '--allow-write','--report',str(a.out/'process.json')]
        r=subprocess.run(cmd,cwd=root,capture_output=True,text=True,timeout=45)
        if r.returncode:raise RuntimeError(f'{label} failed: {r.returncode}\n{r.stdout}\n{r.stderr}')
        outputs[label]=out.read_bytes();assert len(outputs[label])==24
        record=struct.unpack('<6I',outputs[label]);assert record[0]==0x200000 and record[-1]==126 and record[1]>0,record
        runs[label]={'status':'passed','record':list(record),'sha256':hashlib.sha256(outputs[label]).hexdigest()}
    if 'original' in outputs:assert outputs['original']==outputs['generated'],'CPUID/module original-vs-compiled result differs'
    p=json.loads((a.out/'process.json').read_text());assert p['exited'] and p['exit_code']==0
    assert p['module_lookups'][-1]['name']==name and p['module_lookups'][-1]['result']=='absent_in_declared_namespace'
    evidence={'schema':'winrecomp.module-program.v1','platform':c.os.name,'runs':runs,'original_windows_executed':c.os.name=='nt','process':p,'scope':'Authored module/CPUID/flags program; not game execution or full Windows DLL search fidelity'}
    (a.out/'acceptance.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print('Compiled flags, native CPUID and module lookup program passed; original Windows comparison:',c.os.name=='nt')
if __name__=='__main__':main()
