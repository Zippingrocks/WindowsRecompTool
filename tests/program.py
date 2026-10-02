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
    # This PE is also executed by the real Windows loader on Windows CI. It
    # verifies allocation/heap ownership, stdcall cleanup and file I/O end-to-end.
    # It does not depend on any WinRecomp implementation to produce its original
    # Windows result, and does not embed game code.
    names=['VirtualAlloc','VirtualFree','HeapCreate','HeapAlloc','HeapReAlloc',
           'HeapSize','HeapFree','HeapDestroy','SetLastError','GetLastError',
           'CreateFileA','WriteFile','GetFileSize','CloseHandle','ExitProcess']
    code=bytearray();failures=[]
    def emit(text):code.extend(bytes.fromhex(text))
    def push(value):code.extend(b'\x68'+struct.pack('<I',value))
    def call(name):code.extend(b'\xff\x15'+struct.pack('<I',0x402180+4*names.index(name)))
    def fail(condition='84'):
        emit('0f'+condition);failures.append(len(code));code.extend(bytes(4))
    def check_bool():emit('85c0');fail()
    push(4);push(0x3000);push(8192);push(0);call('VirtualAlloc');check_bool();emit('89c3')
    emit('c70357575231')  # [EBX] = four-byte record signature
    push(0);push(0);push(0);call('HeapCreate');check_bool();emit('89c6')
    push(16);push(8);emit('56');call('HeapAlloc');check_bool();emit('89c7')
    emit('833f00');fail('85');emit('c70778563412')
    push(64);emit('57');push(8);emit('56');call('HeapReAlloc');check_bool();emit('89c7')
    emit('813f78563412');fail('85');emit('837f2000');fail('85')
    emit('57');push(0);emit('56');call('HeapSize');emit('83f840');fail('82')
    emit('8b07894304')  # payload read from the reallocated heap block
    emit('57');push(0);emit('56');call('HeapFree');check_bool()
    emit('56');call('HeapDestroy');check_bool()
    push(0x1234);call('SetLastError');call('GetLastError');emit('3d34120000');fail('85');emit('89430c')
    push(0);push(0x80);push(2);push(0);push(0);push(0x40000000);push(0x402900);call('CreateFileA')
    emit('83f8ff');fail();emit('89c5')
    push(0);push(0x402940);push(16);emit('5355');call('WriteFile');check_bool()
    emit('833d4029400010');fail('85')
    push(0);emit('55');call('GetFileSize');emit('83f810');fail('85')
    emit('55');call('CloseHandle');check_bool()
    push(0x8000);push(0);emit('53');call('VirtualFree');check_bool()
    push(0);call('ExitProcess');emit('c3')
    failure=len(code);push(99);call('ExitProcess');emit('c3')
    for position in failures:struct.pack_into('<i',code,position,failure-position-4)
    raw=imports(code,names)
    # Complete OS/subsystem and stack/heap fields for the native Windows loader.
    for offset,value in [(4,(len(code)+511)&~511),(8,4096),(20,0x1000),(24,0x2000),
                         (40,5),(48,5),(72,0x100000),(76,0x1000),(80,0x100000),(84,0x1000)]:
        put32(raw,0x98+offset,value)
    data_offset=struct.unpack_from('<I',raw,0x98+224+40+20)[0]
    raw[data_offset+0x900:data_offset+0x90b]=b'result.bin\0'
    pe=a.out/'input.exe';pe.write_bytes(raw)
    expected=struct.pack('<IIII',0x31525757,0x12345678,0,0x1234)
    native_original={'status':'not_run','reason':'requires Windows native PE32 loader'}
    native_root=a.out/'original';native_root.mkdir(exist_ok=True)
    if c.os.name=='nt':
        native=subprocess.run([str(pe)],cwd=native_root,capture_output=True,timeout=30)
        if native.returncode!=0:raise RuntimeError(f'Original Windows PE32 failed: {native.returncode}')
        if (native_root/'result.bin').read_bytes()!=expected:raise RuntimeError('Original Windows result differs')
        native_original={'status':'passed','exit_code':native.returncode,'output_sha256':hashlib.sha256(expected).hexdigest(),
                         'oracle':'original PE32 executed by Windows, not Unicorn or the WinRecomp runtime'}
    cfg=json.loads(c.run([str(a.tool),'cfg',str(pe)]).stdout);assert not cfg['diagnostics']
    project=a.out/'project';shutil.rmtree(project,ignore_errors=True)
    c.run([str(a.tool),'project',str(pe),str(project)])
    manifest=json.loads((project/'manifest.json').read_text());assert manifest['sha256']==hashlib.sha256(pe.read_bytes()).hexdigest()
    c.run(['cmake','-S',str(project),'-B',str(project/'build'),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',f'-DWINRECOMP_SOURCE={c.ROOT}',f'-DCMAKE_CXX_COMPILER={a.cxx}'])
    c.run(['cmake','--build',str(project/'build'),'--target','recompiled_program','--parallel','2'])
    executable=project/'build'/('recompiled_program.exe' if c.os.name=='nt' else 'recompiled_program');report=a.out/'report.json'
    guest_root=a.out/'generated';guest_root.mkdir(exist_ok=True)
    result=c.run([str(executable),str(pe),'--root',str(guest_root),'--allow-write','--report',str(report)])
    assert (guest_root/'result.bin').read_bytes()==expected
    if c.os.name=='nt':assert (guest_root/'result.bin').read_bytes()==(native_root/'result.bin').read_bytes()
    evidence=json.loads(report.read_text());assert evidence['exited'] and evidence['exit_code']==0 and evidence['api_calls']==15,evidence
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
    (a.out/'acceptance.json').write_text(json.dumps({'schema':'winrecomp.program-test.v2','native_original':native_original,'output_sha256':hashlib.sha256(expected).hexdigest(),'native_pointer_bits':c.ct.sizeof(c.ct.c_void_p)*8,'program':evidence,'wrong_input_rejected':True,'existing_output_preserved':True,'input_report_collision_rejected':True},indent=2)+'\n')
    print(f'Generated native EXE: heap, memory, last-error, file record and exit passed; native Windows comparison: {native_original["status"]}')
if __name__=='__main__':main()
