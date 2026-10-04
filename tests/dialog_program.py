"""Original PE32 versus recompiled x64 modal dialog on actual Windows.

An entirely authored fixture selects its own button through PostMessage; this is
not a renderer choice for E3 and no production dialog is auto-answered.
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
from fixtures import make_pe,put32
from window_program import Assembler
import conformance as c

BASE=0x402000
RECORD,WINDOW,CONTROL,TEXT,OUTPUT,WRITTEN,FILENAME= [BASE+n for n in (0x900,0x940,0x944,0x960,0x9a0,0x9c0,0x9e0)]
MARKER=0xabcdef12

def template():
    b=bytearray(struct.pack('<IIHhhhh',0x80c000c0,0,3,0,0,180,90)+bytes(4))
    def text(s):b.extend(s.encode('utf-16le')+b'\0\0')
    text('WinRecomp compiled dialog');b.extend(struct.pack('<H',8));text('MS Sans Serif')
    for style,ident,atom,name,y in [(0x50010001,1,0x80,'OK',5),(0x50a10101,1001,0x83,'',25),(0x50010003,1002,0x80,'Option',65)]:
        b.extend(bytes((-len(b))%4));b.extend(struct.pack('<IIhhhhHHH',style,0,5,y,100,14,ident,0xffff,atom));text(name);b.extend(bytes(2))
    return b


def fixture():
    b=Assembler();u='USER32.dll';k='KERNEL32.dll'
    b.push(MARKER);b.push(0);entry_proc=len(b.code)-4;b.push(0);b.push(101);b.push(0x400000);b.call(u,'DialogBoxParamA');b.eax_is(209,'modal result comes from EndDialog')
    b.invoke(u,'IsWindow',(WINDOW,));b.eax_is(0,'dialog destroyed on return');b.invoke(u,'IsWindow',(CONTROL,));b.eax_is(0,'child handle lifetime')
    b.emit('c705');b.word(RECORD+24);b.word(1)
    b.push(MARKER);b.push(0);cancel_proc=len(b.code)-4;b.push(0);b.push(101);b.push(0x400000);b.call(u,'DialogBoxParamA');b.eax_is(0,'zero is a legitimate dialog result')
    for at,value,why in [(RECORD,2,'two initializations'),(RECORD+4,2,'posted command callbacks'),(RECORD+8,MARKER,'guest initialization value'),(RECORD+12,1,'check state'),(RECORD+16,15,'ANSI list text length'),(RECORD+20,0x23456789,'DWL_MSGRESULT width')]:b.compare(at,value,why)
    b.invoke(k,'CreateFileA',FILENAME,0x40000000,0,0,2,0x80,0);b.emit('83f8ff');b.fail('output open');b.emit('89c3')
    b.push(0);b.push(WRITTEN);b.push(28);b.push(RECORD);b.emit('53');b.call(k,'WriteFile');b.nonzero('record write')
    b.emit('53');b.call(k,'CloseHandle');b.nonzero('record close');b.invoke(k,'ExitProcess',0);b.emit('c3')
    while len(b.code)%16:b.emit('90')
    procedure=0x401000+len(b.code);b.emit('5589e5535657')
    b.emit('8b450c3d10010000');b.branch('init','0f84');b.emit('3d11010000');b.branch('command','0f84');b.emit('3d37800000');b.branch('custom','0f84');b.emit('31c0');b.branch('return')
    b.label('custom');b.push(0x23456789);b.push(0);b.emit('ff7508');b.call(u,'SetWindowLongA');b.branch('handled')
    b.label('init');b.increment(RECORD);b.emit('8b4508');b.store(WINDOW);b.emit('8b4514');b.eax_is(MARKER,'initialization marshalling');b.store(RECORD+8)
    b.invoke(u,'GetDlgItem',(WINDOW,),1001);b.nonzero('list control');b.store(CONTROL)
    b.invoke(u,'SendMessageA',(CONTROL,),0x180,0,TEXT);b.eax_is(0,'list string insertion')
    b.invoke(u,'SendMessageA',(CONTROL,),0x186,0,0);b.eax_is(0,'list selection')
    b.invoke(u,'SendMessageA',(CONTROL,),0x189,0,OUTPUT);b.eax_is(15,'list text copy');b.store(RECORD+16)
    for off,v in [(0,0x74736152),(4,0x7a697265),(8,0x74207265),(12,0x00747865)]:b.compare(OUTPUT+off,v,'unaltered ANSI list string')
    b.compare(OUTPUT+16,0xdeadbeef,'output canary')
    b.invoke(u,'CheckDlgButton',(WINDOW,),1002,1);b.nonzero('check control');b.invoke(u,'IsDlgButtonChecked',(WINDOW,),1002);b.eax_is(1,'check state');b.store(RECORD+12)
    b.invoke(u,'SendMessageA',(WINDOW,),0x8037,0,0);b.eax_is(0x23456789,'DWL_MSGRESULT');b.store(RECORD+20)
    b.invoke(u,'GetDlgItem',(WINDOW,),1);b.store(CONTROL);b.invoke(u,'PostMessageA',(WINDOW,),0x111,1,(CONTROL,));b.nonzero('posted dialog command');b.branch('handled')
    b.label('command');b.emit('8b451025ffff00003d01000000');b.branch('unhandled','0f85');b.increment(RECORD+4)
    b.emit('8b4514');b.emit('3b05');b.word(CONTROL);b.fail('command control handle marshalling','0f85')
    b.emit('833d');b.word(RECORD+24);b.emit('00');b.branch('cancel','0f85');b.invoke(u,'EndDialog',(WINDOW,),209);b.nonzero('EndDialog result');b.branch('handled')
    b.label('cancel');b.invoke(u,'EndDialog',(WINDOW,),0);b.nonzero('EndDialog zero');b.branch('handled')
    b.label('unhandled');b.emit('31c0');b.branch('return');b.label('handled');b.emit('b801000000');b.label('return');b.emit('5f5e5b5dc21000');b.finish()
    for at in (entry_proc,cancel_proc):struct.pack_into('<I',b.code,at,procedure)
    data=bytearray(0x2000);by_dll={}
    for _,dll,name in b.calls:by_dll.setdefault(dll,[]);by_dll[dll]+=[] if name in by_dll[dll] else [name]
    pos=0x100;table=0x500;slots={}
    for index,(dll,names) in enumerate(by_dll.items()):
        dname=pos;v=dll.encode()+b'\0';data[pos:pos+len(v)]=v;pos=(pos+len(v)+1)&~1
        lookup=table;table+=4*(len(names)+1);iat=table;table+=4*(len(names)+1)
        struct.pack_into('<IIIII',data,index*20,0x2000+lookup,0,0,0x2000+dname,0x2000+iat)
        for n,name in enumerate(names):
            v=bytes(2)+name.encode()+b'\0';data[pos:pos+len(v)]=v
            struct.pack_into('<I',data,lookup+4*n,0x2000+pos);struct.pack_into('<I',data,iat+4*n,0x2000+pos);slots[dll,name]=BASE+iat+4*n;pos=(pos+len(v)+1)&~1
    assert pos<0x500 and table<0x900 and len(b.code)<0x1000
    for at,dll,name in b.calls:struct.pack_into('<I',b.code,at,slots[dll,name])
    data[TEXT-BASE:TEXT-BASE+16]=b'Rasterizer text\0';data[FILENAME-BASE:FILENAME-BASE+11]=b'dialog.bin\0';put32(data,OUTPUT-BASE+16,0xdeadbeef)
    root=0x1000;payload=template()
    struct.pack_into('<HII',data,root+14,1,5,0x80000018);struct.pack_into('<HII',data,root+38,1,101,0x80000030);struct.pack_into('<HII',data,root+62,1,1033,72)
    struct.pack_into('<II',data,root+72,0x3000+96,len(payload));data[root+96:root+96+len(payload)]=payload
    pe=make_pe(b.code,data);put32(pe,0x98+104,0x2000);put32(pe,0x98+108,20*(len(by_dll)+1));put32(pe,0x98+112,0x3000);put32(pe,0x98+116,96+len(payload))
    for off,v in [(4,(len(b.code)+511)&~511),(8,len(data)),(20,0x1000),(24,0x2000),(40,5),(48,5),(72,0x100000),(76,0x1000),(80,0x100000),(84,0x1000)]:put32(pe,0x98+off,v)
    return pe,procedure,{100+n:why for n,(_,why) in enumerate(b.failures)}


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--tool',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--cxx',default='cl');ap.add_argument('--prepare-only',action='store_true');a=ap.parse_args();a.out=a.out.resolve();a.tool=a.tool.resolve();a.out.mkdir(parents=True,exist_ok=True)
    raw,procedure,failures=fixture();pe=a.out/'dialog.exe';pe.write_bytes(raw);project=a.out/'project'
    if project.exists():shutil.rmtree(project)
    c.run([str(a.tool),'project',str(pe),str(project),'--seed',hex(procedure)])
    if a.prepare_only:return
    if os.name!='nt':raise SystemExit('Native Windows required; not a passing game result')
    original=a.out/'original';original.mkdir(exist_ok=True)
    run=subprocess.run([str(pe)],cwd=original,capture_output=True,timeout=45)
    if run.returncode:raise RuntimeError(f'Original Windows dialog fixture: {run.returncode}: {failures.get(run.returncode)}')
    expected=(original/'dialog.bin').read_bytes();assert len(expected)==28
    c.run(['cmake','-S',str(project),'-B',str(project/'build'),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',f'-DWINRECOMP_SOURCE={c.ROOT}',f'-DCMAKE_CXX_COMPILER={a.cxx}'])
    c.run(['cmake','--build',str(project/'build'),'--target','recompiled_program','--parallel','2'])
    generated=a.out/'generated';generated.mkdir(exist_ok=True);report=a.out/'process.json'
    run=subprocess.run([str(project/'build/recompiled_program.exe'),str(pe),'--root',str(generated),'--allow-write','--report',str(report)],capture_output=True,text=True,timeout=60)
    if run.returncode:raise RuntimeError(f'Generated x64: {run.returncode}: {failures.get(run.returncode)}\n{run.stdout}\n{run.stderr}')
    actual=(generated/'dialog.bin').read_bytes();assert actual==expected
    process=json.loads(report.read_text());assert process['exited'] and process['exit_code']==0
    assert process['gui']['dialogs_created']==2 and process['gui']['dialogs_completed']==2
    evidence={'schema':'winrecomp.modal-dialog-program.v1','source_input_sha256':hashlib.sha256(raw).hexdigest(),'original':'Windows PE32 loader','generated':'native Windows x64 compiled dispatch','identical_semantic_record':True,'record':list(struct.unpack('<7I',actual)),'record_sha256':hashlib.sha256(actual).hexdigest(),'process':process,'scope':'Authored modal-dialog test, not E3 dialog execution or gameplay.'}
    (a.out/'acceptance.json').write_text(json.dumps(evidence,indent=2)+'\n');print('Original PE32 and recompiled Windows x64: two real modal dialogs, compiled controls/callbacks, identical records')
if __name__=='__main__':main()
