"""Original PE32 vs generated x64: real DirectDraw COM and offscreen pixels.

Only author-created x86 instructions/data. No game input and no fake driver.
The native-original oracle is mandatory on Windows; other hosts may only prepare.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import uuid
from fixtures import make_pe, put32
from window_program import Assembler
import conformance as c
DATA=0x402000
IID7,IIDUNK,IIDBAD=DATA+0x900,DATA+0x910,DATA+0x920
DRAW,UNK,ALIAS,SURFACE,DRAW1=DATA+0xa00,DATA+0xa04,DATA+0xa08,DATA+0xa0c,DATA+0xa10
DESC,LOCK,FX,RECORD=DATA+0xb00,DATA+0xc00,DATA+0xd00,DATA+0xe00
OUTPUT,WRITTEN,NAME=DATA+0x2000,DATA+0xf00,DATA+0xf10
WIDTH,HEIGHT=7,5
MARKER=0x24681357

def fixture():
    b=Assembler();dd='DDRAW.dll';k='KERNEL32.dll'
    def hr(label):b.eax_is(0,label)
    def com(obj,slot,*args):
        for arg in reversed(((obj,),)+args):
            if isinstance(arg,tuple):b.memory_push(arg[0])
            else:b.push(arg)
        b.emit('a1');b.word(obj);b.emit('8b00ff90');b.word(slot*4)
    b.invoke(dd,'DirectDrawEnumerateExA',0,MARKER,0)
    # Exact relocation operand, recorded before any subsequent instruction emission.
    callback_at=11
    assert b.code[10]==0x68
    hr('adapter enumeration');b.compare(RECORD+4,1,'cancel after first adapter')
    b.compare(RECORD+8,1,'callback context');b.compare(RECORD+12,1,'native adapter strings')
    b.invoke(dd,'DirectDrawCreate',2,DRAW1,0);hr('create old DirectDraw interface')
    com(DRAW1,0,IID7,DRAW);hr('old interface QI to DD7');com(DRAW1,2)
    com(DRAW,0,IIDUNK,UNK);hr('IUnknown identity')
    com(UNK,0,IID7,ALIAS);hr('IUnknown QI DD7')
    b.emit('a1');b.word(DRAW);b.emit('3b05');b.word(ALIAS);b.fail('QI canonical DD7 address','0f85')
    com(ALIAS,2);com(DRAW,0,IIDUNK,ALIAS);hr('repeated IUnknown identity')
    b.emit('a1');b.word(UNK);b.emit('3b05');b.word(ALIAS);b.fail('QI canonical IUnknown address','0f85')
    com(ALIAS,2);com(UNK,2)
    com(DRAW,0,IIDBAD,ALIAS);b.eax_is(0x80004002,'unsupported native IID result');b.compare(ALIAS,0,'failed QI null result')
    com(DRAW,1);com(DRAW,2)
    com(DRAW,20,0,8);hr('normal cooperative mode')
    com(DRAW,26);hr('cooperative level status')
    com(DRAW,12,LOCK);hr('display-mode marshalling');b.compare(LOCK,124,'description x86 size');b.compare(LOCK+124,0xdeadbeef,'description canary')
    com(DRAW,6,DESC,SURFACE,0);hr('create real system-memory offscreen surface')
    com(SURFACE,22,LOCK);hr('surface description');b.compare(LOCK+8,HEIGHT,'surface height');b.compare(LOCK+12,WIDTH,'surface width')
    b.compare(LOCK+124,0xdeadbeef,'surface description canary')
    com(SURFACE,5,0,0,0,0x01000400,FX);hr('native color-fill blit')
    com(SURFACE,25,0,LOCK,1,0);hr('writable full-surface Lock')
    b.emit('a1');b.word(LOCK+36);b.emit('c700');b.word(0x00112233)
    b.emit('8b15');b.word(LOCK+16)
    for _ in range(HEIGHT-1):b.emit('01d0')
    b.emit('c740');b.code.append(4*(WIDTH-1));b.word(0x00445566)
    com(SURFACE,32,0);hr('writeback Unlock')
    com(SURFACE,25,0,LOCK,0x11,0);hr('read-only full-surface Lock')
    b.emit('8b35');b.word(LOCK+36);b.emit('bf');b.word(OUTPUT)
    b.emit('bb');b.word(HEIGHT);b.label('row')
    b.emit('b9');b.word(WIDTH);b.emit('89f2');b.label('pixel')
    b.emit('8b0225ffffff00890783c20483c70449');b.branch('pixel','0f85')
    b.emit('0335');b.word(LOCK+16);b.emit('4b');b.branch('row','0f85')
    com(SURFACE,32,0);hr('read-only Unlock');com(SURFACE,24);hr('surface not lost')
    b.compare(LOCK+124,0xdeadbeef,'Lock output canary')
    com(SURFACE,2);com(DRAW,2)
    # Also check the direct DD7 factory, not only the legacy factory/QI route.
    b.invoke(dd,'DirectDrawCreateEx',2,DRAW,IID7,0);hr('direct DD7 factory');com(DRAW,2)
    b.invoke(k,'CreateFileA',NAME,0x40000000,0,0,2,0x80,0);b.emit('83f8ff');b.fail('create output');b.emit('89c3')
    b.push(0);b.push(WRITTEN);b.push(WIDTH*HEIGHT*4);b.push(OUTPUT);b.emit('53');b.call(k,'WriteFile');b.nonzero('write pixels')
    b.compare(WRITTEN,WIDTH*HEIGHT*4,'output size');b.emit('53');b.call(k,'CloseHandle');b.nonzero('close output')
    b.invoke(k,'ExitProcess',0);b.emit('c3')
    b.label('callback');b.emit('558bec');b.increment(RECORD+4)
    b.emit('817d14');b.word(MARKER);b.branch('bad','0f85')
    b.emit('c705');b.word(RECORD+8);b.word(1)
    b.emit('8b450c85c0');b.branch('bad','0f84');b.emit('803800');b.branch('bad','0f84')
    b.emit('8b451085c0');b.branch('bad','0f84');b.emit('803800');b.branch('bad','0f84')
    b.emit('c705');b.word(RECORD+12);b.word(1)
    b.label('bad');b.emit('31c0c9c21400') # cancel, correctly clean five stdcall arguments
    b.finish();procedure=0x401000+b.labels['callback'];struct.pack_into('<I',b.code,callback_at,procedure)
    assert len(b.code)<=4096,len(b.code)
    data=bytearray(0x4000);by_dll={}
    for _,dll,name in b.calls:
        names=by_dll.setdefault(dll,[])
        if name not in names:names.append(name)
    at=0x100;symbols={}
    for index,(dll,names) in enumerate(by_dll.items()):
        raw=dll.encode()+b'\0';dll_at=at;data[at:at+len(raw)]=raw;at=(at+len(raw)+3)&~3
        lookup=at;at+=4*(len(names)+1);iat=at;at+=4*(len(names)+1)
        struct.pack_into('<IIIII',data,index*20,0x2000+lookup,0,0,0x2000+dll_at,0x2000+iat)
        for n,name in enumerate(names):
            raw=b'\0\0'+name.encode()+b'\0';data[at:at+len(raw)]=raw
            struct.pack_into('<I',data,lookup+4*n,0x2000+at);struct.pack_into('<I',data,iat+4*n,0x2000+at)
            symbols[dll,name]=DATA+iat+4*n;at=(at+len(raw)+1)&~1
    assert at<0x900
    for pos,dll,name in b.calls:struct.pack_into('<I',b.code,pos,symbols[dll,name])
    for at,value in [(IID7,'15e65ec0-3b9c-11d2-b92f-00609797ea5b'),(IIDUNK,'00000000-0000-0000-c000-000000000046'),(IIDBAD,'12345678-1357-2468-9876-0123456789ab')]:data[at-DATA:at-DATA+16]=uuid.UUID(value).bytes_le
    def word(at,v):struct.pack_into('<I',data,at-DATA,v)
    for at,v in [(DESC,124),(DESC+4,0x1007),(DESC+8,HEIGHT),(DESC+12,WIDTH),(DESC+72,32),(DESC+76,0x40),(DESC+84,32),(DESC+88,0xff0000),(DESC+92,0xff00),(DESC+96,0xff),(DESC+104,0x840),(LOCK,124),(LOCK+124,0xdeadbeef),(FX,100),(FX+80,0x00336699),(RECORD,0x31444457)]:word(at,v)
    data[NAME-DATA:NAME-DATA+12]=b'surface.bin\0'
    raw=make_pe(b.code,data);put32(raw,0x98+104,0x2000);put32(raw,0x98+108,20*(len(by_dll)+1))
    for off,value in [(4,(len(b.code)+511)&~511),(8,len(data)),(20,0x1000),(24,0x2000),(40,5),(48,5),(72,0x100000),(76,0x1000),(80,0x100000),(84,0x1000)]:put32(raw,0x98+off,value)
    return raw,procedure,{100+n:text for n,(_,text) in enumerate(b.failures)}

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--tool',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--cxx',default='c++');ap.add_argument('--prepare-only',action='store_true');args=ap.parse_args()
    args.out=args.out.resolve();args.out.mkdir(parents=True,exist_ok=True);raw,callback,failures=fixture();exe=args.out/'directdraw.exe';exe.write_bytes(raw)
    project=args.out/'project';shutil.rmtree(project,ignore_errors=True)
    c.run([str(args.tool.resolve()),'project',str(exe),str(project),'--seed',hex(callback)])
    if args.prepare_only:print('Prepared synthetic DirectDraw PE32; execution not tested');return
    if os.name!='nt':raise SystemExit('Native Windows DirectDraw oracle unavailable; not a pass')
    expected=[0x00336699]*(WIDTH*HEIGHT);expected[0]=0x00112233;expected[-1]=0x00445566;expected=struct.pack('<'+'I'*len(expected),*expected)
    original=args.out/'original';original.mkdir(exist_ok=True)
    result=subprocess.run([str(exe)],cwd=original,capture_output=True,timeout=60)
    if result.returncode:raise RuntimeError(f'Original PE32 failed: {result.returncode}: {failures.get(result.returncode,"OS/fixture failure")}')
    original_bytes=(original/'surface.bin').read_bytes();assert original_bytes==expected,'native fixture pixel result differs'
    c.run(['cmake','-S',str(project),'-B',str(project/'build'),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',f'-DWINRECOMP_SOURCE={c.ROOT}',f'-DCMAKE_CXX_COMPILER={args.cxx}'])
    c.run(['cmake','--build',str(project/'build'),'--target','recompiled_program','--parallel','2'])
    generated=args.out/'generated';generated.mkdir(exist_ok=True);report=args.out/'process.json'
    result=subprocess.run([str(project/'build/recompiled_program.exe'),str(exe),'--root',str(generated),'--allow-write','--report',str(report)],capture_output=True,text=True,timeout=60)
    if result.returncode:raise RuntimeError(f'Generated x64 failed: {result.returncode}: {failures.get(result.returncode,"")}\n{result.stderr}\n{result.stdout}')
    output=(generated/'surface.bin').read_bytes();assert output==original_bytes==expected,'original/generated offscreen pixel mismatch'
    evidence=json.loads(report.read_text());assert evidence['exited'] and evidence['exit_code']==0
    dd=evidence['directdraw'];assert dd['backend']=='native-ddraw7' and dd['adapter_callbacks']==1 and dd['surface_locks']==2 and dd['surface_unlocks']==2 and dd['blits']==1,dd
    assert dd['objects_created']==dd['objects_retired'],dd
    (args.out/'acceptance.json').write_text(json.dumps({'schema':'winrecomp.directdraw-program.v1','original':'native Windows PE32','generated':'native x64 compiled dispatch','identical_pixels':True,'output_sha256':hashlib.sha256(output).hexdigest(),'width':WIDTH,'height':HEIGHT,'process':evidence,'scope':'Synthetic native adapter/COM/offscreen-surface test, not a Direct3D scene or playable E3'},indent=2)+'\n')
    print('Original PE32 and generated x64: real DirectDraw enumeration, COM identity, surface fill/lock/writeback and identical pixels passed')
if __name__=='__main__':main()
