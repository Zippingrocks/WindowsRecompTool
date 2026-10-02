"""Original PE32 vs generated x64: native window and compiled WNDPROC.

Author-written bytes only. Linux generates/compiles the fixture, verifies that
GUI execution is explicitly rejected, and reports native GUI execution as not
run. Windows must really create, show, paint, message and destroy its HWND.
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
import conformance as c
from fixtures import make_pe, put32


def fixture():
    kernel=['ExitProcess','CreateFileA','WriteFile','CloseHandle']
    user=['LoadIconA','LoadCursorA','RegisterClassExA','CreateWindowExA','DefWindowProcA',
          'ShowWindow','UpdateWindow','BeginPaint','EndPaint','PostMessageA','PostQuitMessage',
          'GetMessageA','TranslateMessage','DispatchMessageA','DestroyWindow','UnregisterClassA',
          'GetClientRect','IsWindow','SetWindowTextA','GetWindowTextA']
    iats={name:0x402200+4*n for n,name in enumerate(kernel)}
    iats.update({name:0x402220+4*n for n,name in enumerate(user)})
    data=bytearray(4096)
    cursor=0x300
    for index,(dll,names,oft,iat) in enumerate([('KERNEL32.dll',kernel,0x100,0x200),('USER32.dll',user,0x120,0x220)]):
        dlloff=0x60+index*32
        struct.pack_into('<IIIII',data,index*20,0x2000+oft,0,0,0x2000+dlloff,0x2000+iat)
        data[dlloff:dlloff+len(dll)+1]=dll.encode()+b'\0'
        for n,name in enumerate(names):
            text=b'\0\0'+name.encode()+b'\0';data[cursor:cursor+len(text)]=text
            struct.pack_into('<I',data,oft+4*n,0x2000+cursor);struct.pack_into('<I',data,iat+4*n,0x2000+cursor)
            cursor=(cursor+len(text)+1)&~1
    assert cursor<0x900
    TITLE=0x402900;CLASS=0x402940;WC=0x402980;MSG=0x4029c0;STATE=0x402a00
    PAINT=0x402a40;RECT=0x402a80;TEXT=0x402ac0;PATH=0x402b00;WRITTEN=0x402b40
    WNDPROC=0x401600
    for at,text in [(TITLE,b'WinRecomp native GUI acceptance'),(CLASS,b'WinRecompAcceptance'),(PATH,b'gui-result.bin')]:
        offset=at-0x402000;data[offset:offset+len(text)+1]=text+b'\0'
    # record = signature, observed callback mask, WM_APP payload sum, paint flag
    struct.pack_into('<IIII',data,STATE-0x402000,0x32495547,0,0,0)
    struct.pack_into('<12I',data,WC-0x402000,48,3,WNDPROC,0,0,0x400000,0,0,6,0,CLASS,0)
    code=bytearray();labels={};fixups=[]
    def emit(text):code.extend(bytes.fromhex(text))
    def u32(value):code.extend(struct.pack('<I',value))
    def push(value):emit('68');u32(value)
    def call(name):emit('ff15');u32(iats[name])
    def label(name):labels[name]=len(code)
    def branch(name,condition=None):
        emit('e9' if condition is None else '0f'+condition);fixups.append((len(code),name));u32(0)
    def boolean():emit('85c0');branch('fail','84')
    def abs_mov(reg,at):emit({'eax':'a1','ebx':'8b1d'}[reg]);u32(at)
    def check_eax(value):emit('3d');u32(value);branch('fail','85')
    def store(at):emit('a3');u32(at)
    push(32512);push(0);call('LoadIconA');boolean();store(WC+24)
    push(32512);push(0);call('LoadCursorA');boolean();store(WC+28)
    push(WC);call('RegisterClassExA');boolean()
    for value in [0x11223344,0x400000,0,0,240,320,40,40,0x00cf0000,TITLE,CLASS,0]:push(value)
    call('CreateWindowExA');boolean();store(STATE+16)
    push(5);emit('50');call('ShowWindow')
    abs_mov('eax',STATE+16);emit('50');call('UpdateWindow');boolean()
    push(TITLE);abs_mov('eax',STATE+16);emit('50');call('SetWindowTextA');boolean()
    push(64);push(TEXT);abs_mov('eax',STATE+16);emit('50');call('GetWindowTextA');check_eax(len(b'WinRecomp native GUI acceptance'))
    push(RECT);abs_mov('eax',STATE+16);emit('50');call('GetClientRect');boolean()
    push(456);push(123);push(0x8001);abs_mov('eax',STATE+16);emit('50');call('PostMessageA');boolean()
    label('loop')
    push(0);push(0);push(0);push(MSG);call('GetMessageA');emit('83f8ff');branch('fail','84');emit('85c0');branch('endloop','84')
    push(MSG);call('TranslateMessage');push(MSG);call('DispatchMessageA');branch('loop')
    label('endloop')
    abs_mov('eax',STATE+16);emit('50');call('DestroyWindow');boolean()
    abs_mov('eax',STATE+16);emit('50');call('IsWindow');check_eax(0)
    push(0x400000);push(CLASS);call('UnregisterClassA');boolean()
    abs_mov('eax',STATE+4);check_eax(31);abs_mov('eax',STATE+8);check_eax(579);abs_mov('eax',STATE+12);check_eax(1)
    for v in [0,0x80,2,0,0,0x40000000,PATH]:push(v)
    call('CreateFileA');emit('83f8ff');branch('fail','84');emit('89c3')
    push(0);push(WRITTEN);push(16);push(STATE);emit('53');call('WriteFile');boolean()
    emit('53');call('CloseHandle');boolean();push(0);call('ExitProcess');emit('c3')
    label('fail');push(99);call('ExitProcess');emit('c3')
    assert len(code)<0x600
    code.extend(b'\xcc'*(0x600-len(code)))
    label('wndproc');emit('5589e5')
    for msg,target in [(0x81,'nccreate'),(1,'create'),(0x24,'minmax'),(0xf,'paint'),(0x8001,'app'),(2,'destroy')]:
        emit('817d0c');u32(msg);branch(target,'84')
    label('default');emit('ff7514ff7510ff750cff7508');call('DefWindowProcA');emit('5dc21000')
    def mask(bit):emit('830d');u32(STATE+4);code.append(bit)
    def ret(value):emit('b8');u32(value);emit('5dc21000')
    label('nccreate');emit('8b45148138');u32(0x11223344);branch('badcreate','85');emit('817804');u32(0x400000);branch('badcreate','85');emit('817828');u32(CLASS);branch('badcreate','85');mask(1);branch('default')
    label('badcreate');ret(0)
    label('create');emit('8b45148138');u32(0x11223344);branch('bad','85');mask(2);ret(0)
    label('minmax');emit('8b4514c74018c8000000c7401c96000000');mask(4);ret(0)
    label('paint');push(PAINT);emit('ff7508');call('BeginPaint');emit('85c0');branch('bad','84');push(PAINT);emit('ff7508');call('EndPaint');emit('85c0');branch('bad','84');emit('c705');u32(STATE+12);u32(1);ret(0)
    label('app');emit('8b4510034514');store(STATE+8);mask(8);push(0);call('PostQuitMessage');ret(0)
    label('destroy');mask(16);ret(0)
    label('bad');push(98);call('ExitProcess');ret(0)
    for at,target in fixups:struct.pack_into('<i',code,at,labels[target]-at-4)
    raw=make_pe(code,data);put32(raw,0x98+104,0x2000);put32(raw,0x98+108,60)
    for offset,value in [(4,(len(code)+511)&~511),(8,4096),(20,0x1000),(24,0x2000),(40,5),(48,5),(72,0x100000),(76,0x1000),(80,0x100000),(84,0x1000)]:put32(raw,0x98+offset,value)
    return raw,WNDPROC,struct.pack('<IIII',0x32495547,31,579,1)


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--tool',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--cxx',default='c++');a=ap.parse_args();a.tool=a.tool.resolve();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=True)
    raw,proc,expected=fixture();pe=a.out/'gui.exe';pe.write_bytes(raw)
    original={'status':'not_run','reason':'requires native Windows desktop'}
    if os.name=='nt':
        root=a.out/'original';root.mkdir(exist_ok=True)
        done=subprocess.run([str(pe)],cwd=root,capture_output=True,timeout=30)
        if done.returncode:raise RuntimeError(f'Original native Windows GUI fixture failed with {done.returncode}')
        assert (root/'gui-result.bin').read_bytes()==expected
        original={'status':'passed','oracle':'original PE32 under Windows, not WinRecomp','output_sha256':hashlib.sha256(expected).hexdigest()}
    project=a.out/'project';shutil.rmtree(project,ignore_errors=True)
    c.run([str(a.tool),'project',str(pe),str(project),'--seed',hex(proc)])
    c.run(['cmake','-S',str(project),'-B',str(project/'build'),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',f'-DWINRECOMP_SOURCE={c.ROOT}',f'-DCMAKE_CXX_COMPILER={a.cxx}'])
    c.run(['cmake','--build',str(project/'build'),'--target','recompiled_program','--parallel','2'])
    executable=project/'build'/('recompiled_program.exe' if os.name=='nt' else 'recompiled_program')
    root=a.out/'generated';root.mkdir(exist_ok=True);report=a.out/'run.json'
    command=[str(executable),str(pe),'--root',str(root),'--allow-write','--report',str(report)]
    refused=subprocess.run(command,capture_output=True,text=True,timeout=30)
    assert refused.returncode==2 and '--gui' in refused.stderr
    result=subprocess.run(command+['--gui'],capture_output=True,text=True,timeout=30)
    evidence=json.loads(report.read_text())
    if os.name=='nt':
        if result.returncode:raise RuntimeError(f'Generated native GUI failed: {result.stdout}\n{result.stderr}')
        assert (root/'gui-result.bin').read_bytes()==expected
        assert evidence['gui']['native_windows_created']==1 and evidence['gui']['guest_window_callbacks']>=6 and evidence['gui']['paint_cycles']>=1
        status='passed'
    else:
        assert result.returncode==2 and 'requires Windows' in result.stderr
        status='not_run: native Windows backend unavailable; explicit rejection verified'
    (a.out/'acceptance.json').write_text(json.dumps({'schema':'winrecomp.gui-acceptance.v1','original':original,'generated_status':status,'generated':evidence,'opt_in_rejection_passed':True,'expected_output_sha256':hashlib.sha256(expected).hexdigest(),'playable_game':False},indent=2)+'\n')
    print('Native GUI original/generated acceptance:',status)
if __name__=='__main__':main()
