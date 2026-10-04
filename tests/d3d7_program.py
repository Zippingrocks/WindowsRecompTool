"""Native PE32 versus recompiled x64, including nested D3D7 callbacks.

All code/data is authored for this test. Native Windows is the original-program
oracle. --prepare-only prepares inputs but makes no execution claim.
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
IID_DD,IID_D3D=DATA+0x900,DATA+0x910
DD,D3D,DEVICE_CB,Z_CB,MODE_CB=DATA+0xa00,DATA+0xa04,DATA+0xa08,DATA+0xa0c,DATA+0xa10
ID,HW,SW=DATA+0xb00,DATA+0x1000,DATA+0x1200
RECORD,NAME,WRITTEN=DATA+0x1600,DATA+0x1700,DATA+0x1720
SIZE=104
CONTEXT=0xe1357246

def fixture():
    b=Assembler();k='KERNEL32.dll'
    def com(obj,slot,*args):
        for arg in reversed(((obj,),)+args):
            if isinstance(arg,tuple):b.memory_push(arg[0])
            else:b.push(arg)
        b.emit('a1');b.word(obj);b.emit('8b00ff90');b.word(4*slot)
    def hr(text):b.eax_is(0,text)
    def ctx(offset):
        b.emit('817d');b.code.append(offset);b.word(CONTEXT);b.fail('callback context preserved','0f85')
    b.invoke('DDRAW.dll','DirectDrawCreateEx',0,DD,IID_DD,0);hr('create DirectDraw7')
    com(DD,0,IID_D3D,D3D);hr('native IDirect3D7 QueryInterface')
    com(DD,27,ID,0);hr('GetDeviceIdentifier');b.compare(ID+1068,0xdeadcafe,'device identifier extent')
    for offset,dest in [(1032,80),(1036,84),(1040,88)]:
        b.emit('a1');b.word(ID+offset);b.store(RECORD+dest)
    com(DD,11,HW,SW);hr('GetCaps');b.compare(HW+380,0xdeadcafe,'hardware DDCAPS extent');b.compare(SW+380,0xdeadcafe,'software DDCAPS extent')
    for source,dest in [(HW+4,92),(SW+4,96)]:b.emit('a1');b.word(source);b.store(RECORD+dest)
    com(D3D,3,(DEVICE_CB,),CONTEXT);hr('EnumDevices and nested depth formats')
    b.emit('a1');b.word(RECORD+4);b.nonzero('device callbacks executed')
    b.emit('a1');b.word(RECORD+8);b.nonzero('nested z callbacks executed')
    com(DD,8,0,0,CONTEXT,(MODE_CB,));hr('DD7 display modes')
    b.emit('a1');b.word(RECORD+12);b.nonzero('display callbacks executed')
    com(D3D,7);b.store(RECORD+100) # preserve actual driver HRESULT, not fabricated success
    com(D3D,2);com(DD,2)
    b.invoke(k,'CreateFileA',NAME,0x40000000,0,0,2,0x80,0);b.emit('83f8ff');b.fail('create output');b.emit('89c3')
    b.push(0);b.push(WRITTEN);b.push(SIZE);b.push(RECORD);b.emit('53');b.call(k,'WriteFile');b.nonzero('write record')
    b.compare(WRITTEN,SIZE,'record size');b.emit('53');b.call(k,'CloseHandle');b.nonzero('close record')
    b.invoke(k,'ExitProcess',0);b.emit('c3')
    b.label('device');b.emit('558bec');b.increment(RECORD+4);ctx(20)
    for offset in [8,12]:
        b.emit('8b45');b.code.append(offset);b.nonzero('native device string pointer');b.emit('803800');b.fail('native device string content')
    b.emit('8b5510') # EDX = borrowed D3DDEVICEDESC7*
    for offset,dest in [(0,16),(116,20),(120,24),(132,28),(136,32),(184,36)]:
        b.emit('8b82');b.word(offset);b.store(RECORD+dest)
    for n in range(4):
        b.emit('8b82');b.word(196+4*n);b.store(RECORD+64+4*n)
    # Nested guest -> host -> guest while the outer callback payload is live.
    com(D3D,6,RECORD+64,(Z_CB,),CONTEXT);hr('nested EnumZBufferFormats')
    b.emit('31c0c9c21000') # cancel; four stdcall parameters
    b.label('z');b.emit('558bec');b.increment(RECORD+8);ctx(12)
    b.emit('8b5508813a');b.word(32);b.fail('DDPIXELFORMAT 32 bytes','0f85')
    b.emit('f74204');b.word(0x400);b.fail('ZBUFFER flag')
    for offset,dest in [(12,40),(20,44)]:b.emit('8b42');b.code.append(offset);b.store(RECORD+dest)
    b.emit('31c0c9c20800')
    b.label('mode');b.emit('558bec');b.increment(RECORD+12);ctx(12)
    b.emit('8b5508813a');b.word(124);b.fail('DDSURFACEDESC2 x86 layout','0f85')
    b.emit('837a2400');b.fail('no host surface pointer leakage','0f85')
    for offset,dest in [(12,48),(8,52),(0,56)]:b.emit('8b42');b.code.append(offset);b.store(RECORD+dest)
    b.emit('c705');b.word(RECORD+60);b.word(CONTEXT)
    b.emit('31c0c9c20800')
    b.finish();assert len(b.code)<=4096,len(b.code)
    callbacks=[0x401000+b.labels[n] for n in ['device','z','mode']]
    data=bytearray(0x3000);dlls={}
    for _,dll,name in b.calls:
        names=dlls.setdefault(dll,[])
        if name not in names:names.append(name)
    at=0x100;symbols={}
    for idx,(dll,names) in enumerate(dlls.items()):
        text=dll.encode()+b'\0';dll_at=at;data[at:at+len(text)]=text;at=(at+len(text)+3)&~3
        lookup=at;at+=4*(len(names)+1);iat=at;at+=4*(len(names)+1)
        struct.pack_into('<IIIII',data,idx*20,0x2000+lookup,0,0,0x2000+dll_at,0x2000+iat)
        for n,name in enumerate(names):
            text=b'\0\0'+name.encode()+b'\0';data[at:at+len(text)]=text
            for table in [lookup,iat]:struct.pack_into('<I',data,table+4*n,0x2000+at)
            symbols[dll,name]=DATA+iat+4*n;at=(at+len(text)+1)&~1
    assert at<0x900
    for pos,dll,name in b.calls:struct.pack_into('<I',b.code,pos,symbols[dll,name])
    for at,value in [(IID_DD,'15e65ec0-3b9c-11d2-b92f-00609797ea5b'),(IID_D3D,'f5049e77-4861-11d2-a407-00a0c90629a8')]:
        data[at-DATA:at-DATA+16]=uuid.UUID(value).bytes_le
    for at,value in [(DEVICE_CB,callbacks[0]),(Z_CB,callbacks[1]),(MODE_CB,callbacks[2]),
        (RECORD,0x37443357),(ID+1068,0xdeadcafe),(HW,380),(HW+380,0xdeadcafe),(SW,380),(SW+380,0xdeadcafe)]:
        struct.pack_into('<I',data,at-DATA,value)
    data[NAME-DATA:NAME-DATA+9]=b'd3d7.bin\0'
    raw=make_pe(b.code,data);put32(raw,0x98+104,0x2000);put32(raw,0x98+108,20*(len(dlls)+1))
    for off,value in [(4,(len(b.code)+511)&~511),(8,len(data)),(20,0x1000),(24,0x2000),(40,5),(48,5),(72,0x100000),(76,0x1000),(80,0x100000),(84,0x1000)]:
        put32(raw,0x98+off,value)
    return raw,callbacks,{100+n:text for n,(_,text) in enumerate(b.failures)}

def inspect_record(path):
    data=path.read_bytes();assert len(data)==SIZE,len(data)
    row=struct.unpack('<26I',data)
    assert row[0]==0x37443357 and min(row[1:4])>0,row
    assert row[7]>0 and row[8]>0 and row[10]>0,row
    assert row[12]>0 and row[13]>0 and row[14]==124 and row[15]==CONTEXT,row
    assert any(row[16:20]),'device GUID missing'
    return data

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--tool',type=Path,required=True);ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--cxx',default='c++');ap.add_argument('--prepare-only',action='store_true');args=ap.parse_args()
    args.out=args.out.resolve();args.out.mkdir(parents=True,exist_ok=True)
    raw,seeds,failures=fixture();exe=args.out/'d3d7.exe';exe.write_bytes(raw)
    project=args.out/'project'
    if project.exists():shutil.rmtree(project)
    command=[str(args.tool.resolve()),'project',str(exe),str(project)]
    for seed in seeds:command+=['--seed',hex(seed)]
    c.run(command)
    (args.out/'fixture-failures.json').write_text(json.dumps(failures,indent=2)+'\n')
    if args.prepare_only:print('Prepared only; original and generated programs not run');return
    if os.name!='nt':raise RuntimeError('Native Windows PE32 oracle unavailable, not a pass')
    c.run(['cmake','-S',str(project),'-B',str(project/'build'),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',f'-DWINRECOMP_SOURCE={c.ROOT}',f'-DCMAKE_CXX_COMPILER={args.cxx}'])
    c.run(['cmake','--build',str(project/'build'),'--target','recompiled_program','--parallel','2'])
    records=[]
    for label in ['original','generated']:
        out=args.out/label;out.mkdir(exist_ok=True)
        command=[str(exe)] if label=='original' else [str(project/'build/recompiled_program.exe'),str(exe),'--root',str(out),'--allow-write','--report',str(args.out/'process.json')]
        result=subprocess.run(command,cwd=out,capture_output=True,text=True,timeout=60)
        if result.returncode:raise RuntimeError(f'{label} failed {result.returncode}: {failures.get(result.returncode,"OS/runtime error")}\n{result.stdout}\n{result.stderr}')
        records.append(inspect_record(out/'d3d7.bin'))
    assert records[0]==records[1],'native PE32/generated x64 device, Z and mode records differ'
    report=json.loads((args.out/'process.json').read_text());dd=report['directdraw'];row=struct.unpack('<26I',records[0])
    assert report['exited'] and report['exit_code']==0
    assert [dd['device_callbacks'],dd['zformat_callbacks'],dd['mode_callbacks']]==list(row[1:4]),dd
    assert dd['objects_created']==dd['objects_retired'],dd
    evidence={'schema':'winrecomp.d3d7-program.v1','original':'native Windows PE32 loader',
        'generated':'native x64 compiled dispatch','input_sha256':hashlib.sha256(raw).hexdigest(),
        'record_sha256':hashlib.sha256(records[0]).hexdigest(),'record':list(row),'identical_records':True,
        'nested_callback':True,'process':report,'scope':'Author-written D3D7 device/capability enumeration, not game rendering or playability.'}
    (args.out/'acceptance.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print('Original PE32 vs generated x64 D3D7 device/caps/mode/nested depth callbacks: passed')
if __name__=='__main__':main()
