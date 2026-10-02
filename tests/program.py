"""Build and run an input-bound generated native executable, not a slice DLL."""
from __future__ import annotations
import argparse
import hashlib
import json
import shutil
import subprocess
import struct
from pathlib import Path
import conformance as c
from fixtures import make_pe,put32

def imports(code,names):
    data=bytearray(4096);base=0x2000
    struct.pack_into('<IIIII',data,0,base+0x80,0,0,base+0x60,base+0x180)
    data[0x60:0x6d]=b'KERNEL32.dll\0';pos=0x300
    for k,name in enumerate(names):
        encoded=b'\0\0'+name.encode()+b'\0';data[pos:pos+len(encoded)]=encoded
        struct.pack_into('<I',data,0x80+4*k,base+pos);struct.pack_into('<I',data,0x180+4*k,base+pos);pos=(pos+len(encoded)+1)&~1
    result=make_pe(code,data);put32(result,0x98+104,0x2000);put32(result,0x98+108,40);return result

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--tool',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--cxx',default='c++');a=ap.parse_args();a.tool=a.tool.resolve();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=True)
    # Allocate 8 KiB, preserve it in EBX, write/read a dword, free it, exit 0.
    # Every call goes through the real parsed IAT and the runtime's stdcall bridge.
    code=bytearray.fromhex('6a04680030000068002000006a00ff158021400089c385c0')
    failures=[]
    def fail_jump():
        code.extend(bytes.fromhex('0f84'));failures.append(len(code));code.extend(bytes(4))
    fail_jump()
    code.extend(bytes.fromhex('c70378563412813b78563412'))
    code.extend(bytes.fromhex('0f85'));failures.append(len(code));code.extend(bytes(4))
    code.extend(bytes.fromhex('68008000006a0053ff15842140006a00ff1588214000c3'))
    failure=len(code);code.extend(bytes.fromhex('6a63ff1588214000c3'))
    for position in failures:struct.pack_into('<i',code,position,failure-position-4)

    pe=a.out/'input.exe';pe.write_bytes(imports(code,['VirtualAlloc','VirtualFree','ExitProcess']))
    cfg=json.loads(c.run([str(a.tool),'cfg',str(pe)]).stdout);assert not cfg['diagnostics']
    project=a.out/'project';shutil.rmtree(project,ignore_errors=True)
    c.run([str(a.tool),'project',str(pe),str(project)])
    manifest=json.loads((project/'manifest.json').read_text());assert manifest['sha256']==hashlib.sha256(pe.read_bytes()).hexdigest()
    c.run(['cmake','-S',str(project),'-B',str(project/'build'),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',f'-DWINRECOMP_SOURCE={c.ROOT}',f'-DCMAKE_CXX_COMPILER={a.cxx}'])
    c.run(['cmake','--build',str(project/'build'),'--target','recompiled_program','--parallel','2'])
    executable=project/'build'/('recompiled_program.exe' if c.os.name=='nt' else 'recompiled_program');report=a.out/'report.json'
    result=c.run([str(executable),str(pe),'--root',str(a.out),'--report',str(report)])
    evidence=json.loads(report.read_text());assert evidence['exited'] and evidence['exit_code']==0 and evidence['api_calls']==3,evidence
    altered=a.out/'wrong.exe';raw=bytearray(pe.read_bytes());raw[-1]^=1;altered.write_bytes(raw)
    failure=subprocess.run([str(executable),str(altered)],capture_output=True,text=True,timeout=30);assert failure.returncode==2 and 'SHA-256' in failure.stderr
    failure=subprocess.run([str(a.tool),'project',str(pe),str(project)],capture_output=True,text=True,timeout=30);assert failure.returncode and 'already exists' in failure.stderr
    # Reports must never clobber their input, including on argument errors.
    original=pe.read_bytes()
    for suffix in [[],['--unknown']]:
        failure=subprocess.run([str(executable),str(pe),'--report',str(pe),*suffix],capture_output=True,text=True,timeout=30)
        assert failure.returncode==2 and pe.read_bytes()==original
    alias=a.out/'input-hardlink.exe'
    if alias.exists():alias.unlink()
    c.os.link(pe,alias)
    failure=subprocess.run([str(executable),str(pe),'--report',str(alias)],capture_output=True,text=True,timeout=30)
    assert failure.returncode==2 and alias.read_bytes()==original
    alias.unlink()
    (a.out/'acceptance.json').write_text(json.dumps({'schema':'winrecomp.program-test.v1','native_pointer_bits':c.ct.sizeof(c.ct.c_void_p)*8,'program':evidence,'wrong_input_rejected':True,'existing_output_preserved':True,'input_report_collision_rejected':True},indent=2)+'\n')
    print('Generated native EXE: allocation/write/read/free/exit passed; wrong input refused')
if __name__=='__main__':main()
