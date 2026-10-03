"""An author-written PE32 must really create a window and render on Windows.

The original runs through Windows' PE32 loader. Its recompiled x64 counterpart
runs through WinRecomp. Neither path is an interpreter and no game bytes appear
in this fixture. Pixel output and guest callback records are compared.
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
import sys
from fixtures import make_pe, put32
import conformance as c

DATA = 0x402000
CLASS, TITLE, FILENAME, RECORDNAME = DATA+0xa00, DATA+0xa60, DATA+0xad0, DATA+0xaf0
WC, PFD, MSG, WINDOW, DC, RC = DATA+0xb00, DATA+0xb40, DATA+0xb80, DATA+0xc00, DATA+0xc04, DATA+0xc08
RECORD, PAINT, BYTES_WRITTEN, PIXELS = DATA+0xc20, DATA+0xc80, DATA+0xcd0, DATA+0x2000
PACKED, PACKNAME = DATA+0x6200, DATA+0xaa0
MARKER = 0x13572468

class Assembler:
    def __init__(self):
        self.code=bytearray();self.labels={};self.branches=[];self.calls=[];self.failures=[]
    def emit(self,s):self.code.extend(bytes.fromhex(s))
    def word(self,n):self.code.extend(struct.pack('<I',n&0xffffffff))
    def push(self,n):self.emit('68');self.word(n)
    def memory_push(self,p):self.emit('ff35');self.word(p)
    def label(self,name):self.labels[name]=len(self.code)
    def branch(self,name,op='e9'):
        self.emit(op);self.branches.append((len(self.code),name));self.word(0)
    def call(self,dll,name):
        self.emit('ff15');self.calls.append((len(self.code),dll,name));self.word(0)
    def invoke(self,dll,name,*args):
        for arg in reversed(args):
            if isinstance(arg,tuple):self.memory_push(arg[0])
            else:self.push(arg)
        self.call(dll,name)
    def fail(self,description,condition='0f84'):
        name='failure_'+str(len(self.failures));self.failures.append((name,description));self.branch(name,condition)
    def nonzero(self,description):self.emit('85c0');self.fail(description)
    def eax_is(self,n,description):self.emit('3d');self.word(n);self.fail(description,'0f85')
    def store(self,p):self.emit('a3');self.word(p)
    def compare(self,p,n,description):self.emit('813d');self.word(p);self.word(n);self.fail(description,'0f85')
    def increment(self,p):self.emit('ff05');self.word(p)
    def finish(self):
        for index,(name,description) in enumerate(self.failures):
            self.label(name);self.invoke('KERNEL32.dll','ExitProcess',100+index);self.emit('c3')
        for at,name in self.branches:struct.pack_into('<i',self.code,at,self.labels[name]-at-4)


def icon_resources(data):
    # A solid red, 32x32 author-created icon. Directory offsets are relative to
    # the resource root, whereas data entries contain image-relative RVAs.
    root=0x7000
    def w(off,n,size=4):data[root+off:root+off+size]=int(n).to_bytes(size,'little')
    w(14,2,2);w(16,3);w(20,0x80000020);w(24,14);w(28,0x80000060)
    w(32+14,1,2);w(48,1);w(52,0x80000040);w(64+14,1,2);w(80,1033);w(84,0xa0)
    w(96+14,1,2);w(112,101);w(116,0x80000080);w(128+14,1,2);w(144,1033);w(148,0xb0)
    payload=struct.pack('<IiiHHIIiiII',40,32,64,1,32,0,4096,0,0,0,0)+bytes([0,0,255,255])*1024+bytes(128)
    group=struct.pack('<HHHBBBBHHIH',0,1,1,32,32,0,0,1,32,len(payload),1)
    w(0xa0,0x2000+root+0x100);w(0xa4,len(payload));w(0xb0,0x2000+root+0xc0);w(0xb4,len(group))
    data[root+0xc0:root+0xc0+len(group)]=group;data[root+0x100:root+0x100+len(payload)]=payload
    return 0x2000+root,0x100+len(payload)


def fixture():
    b=Assembler();u='USER32.dll';g='GDI32.dll';gl='OPENGL32.dll';k='KERNEL32.dll'
    b.invoke(u,'LoadIconA',0,32512);b.nonzero('system icon')
    b.invoke(u,'LoadIconA',0x400000,101);b.nonzero('PE resource icon');b.store(WC+24);b.store(WC+44)
    b.invoke(u,'LoadIconA',0x400000,101);b.emit('3b05');b.word(WC+24);b.fail('shared icon identity','0f85')
    b.invoke(u,'LoadCursorA',0,32512);b.nonzero('system cursor');b.store(WC+28)
    b.invoke(g,'GetStockObject',4);b.nonzero('stock brush');b.store(WC+32)
    b.invoke(u,'RegisterClassExA',WC);b.nonzero('class registration')
    b.invoke(u,'CreateWindowExA',0,CLASS,TITLE,0x00cf0000,32,32,160,160,0,0,0x400000,MARKER)
    b.nonzero('native window creation / CREATESTRUCT callback');b.store(WINDOW)
    b.invoke(u,'IsWindow',(WINDOW,));b.eax_is(1,'native IsWindow')
    b.invoke(u,'ShowWindow',(WINDOW,),4);b.invoke(u,'UpdateWindow',(WINDOW,));b.nonzero('update / paint')
    b.invoke(u,'SendMessageA',(WINDOW,),0x8007,0x1357,0x2468);b.eax_is(0x37bf,'synchronous compiled message result')
    b.invoke(u,'PostMessageA',(WINDOW,),0x8007,42,57);b.nonzero('post message')
    b.emit('be');b.word(1000);b.label('pump')
    b.invoke(u,'PeekMessageA',MSG,0,0,0,1);b.emit('85c0');b.branch('drained','0f84')
    b.invoke(u,'TranslateMessage',MSG);b.invoke(u,'DispatchMessageA',MSG)
    b.emit('4e');b.branch('pump','0f85');b.emit('31c0');b.fail('bounded message pump')
    b.label('drained');b.compare(MSG+28,0xdeadbeef,'MSG public size / output canary')
    b.compare(RECORD+12,99,'queued compiled callback');b.compare(RECORD+20,2,'two custom callbacks')
    b.invoke(u,'GetDC',(WINDOW,));b.nonzero('window device context');b.store(DC)
    b.invoke(g,'ChoosePixelFormat',(DC,),PFD);b.nonzero('pixel format');b.store(DATA+0xc0c)
    b.invoke(g,'SetPixelFormat',(DC,),(DATA+0xc0c,),PFD);b.nonzero('set pixel format')
    b.invoke(gl,'wglCreateContext',(DC,));b.nonzero('real WGL context');b.store(RC)
    b.invoke(gl,'wglMakeCurrent',(DC,),(RC,));b.nonzero('current WGL context')
    b.invoke(gl,'glViewport',0,0,64,64)
    def f(v):return struct.unpack('<I',struct.pack('<f',v))[0]
    b.invoke(gl,'glClearColor',f(0),f(0),f(0.25),f(1));b.invoke(gl,'glClear',0x4000)
    b.invoke(gl,'glMatrixMode',0x1701);b.invoke(gl,'glLoadIdentity');b.invoke(gl,'glMatrixMode',0x1700);b.invoke(gl,'glLoadIdentity')
    b.invoke(gl,'glBegin',4);b.invoke(gl,'glColor3f',f(1),f(0),f(0))
    for x,y in [(-.75,-.75),(.75,-.75),(0,.75)]:b.invoke(gl,'glVertex2f',f(x),f(y))
    b.invoke(gl,'glEnd');b.invoke(gl,'glFinish')
    b.invoke(gl,'glReadPixels',0,0,64,64,0x1908,0x1401,PIXELS);b.invoke(gl,'glGetError');b.eax_is(0,'OpenGL drawing/readback error')
    b.invoke(gl,'glPixelStorei',0x0d05,8)
    b.invoke(gl,'glReadPixels',0,0,3,2,0x1908,0x1401,PACKED)
    b.invoke(gl,'glGetError');b.eax_is(0,'aligned odd-width readback')
    b.compare(PACKED+12,0xa5a5a5a5,'pack row padding preserved')
    b.compare(PACKED+28,0xa5a5a5a5,'packed output canary')
    b.invoke(gl,'glPixelStorei',0x0d05,4)
    b.invoke(g,'SwapBuffers',(DC,));b.nonzero('real buffer swap')
    b.invoke(gl,'wglMakeCurrent',0,0);b.nonzero('detach WGL context')
    b.invoke(gl,'wglDeleteContext',(RC,));b.nonzero('delete WGL context')
    b.invoke(u,'ReleaseDC',(WINDOW,),(DC,));b.nonzero('release DC')
    b.invoke(u,'DestroyWindow',(WINDOW,));b.nonzero('destroy / compiled WM_DESTROY')
    b.invoke(u,'UnregisterClassA',CLASS,0x400000);b.nonzero('unregister class')
    b.compare(RECORD+4,1,'WM_NCCREATE count');b.compare(RECORD+8,1,'WM_CREATE count');b.compare(RECORD+24,1,'WM_DESTROY count')
    for filename,pointer,size in [(FILENAME,PIXELS,64*64*4),(RECORDNAME,RECORD,32),(PACKNAME,PACKED,28)]:
        b.invoke(k,'CreateFileA',filename,0x40000000,0,0,2,0x80,0);b.emit('83f8ff');b.fail('open output file');b.emit('89c3')
        b.push(0);b.push(BYTES_WRITTEN);b.push(size);b.push(pointer);b.emit('53');b.call(k,'WriteFile');b.nonzero('write output file')
        b.compare(BYTES_WRITTEN,size,'full output write');b.emit('53');b.call(k,'CloseHandle');b.nonzero('close output')
    b.invoke(k,'ExitProcess',0);b.emit('c3')
    b.label('wndproc');b.emit('558bec8b450c')
    for msg,label in [(0x81,'nc'),(1,'create'),(0x8007,'custom'),(0xf,'paint'),(2,'destroy')]:
        b.emit('3d');b.word(msg);b.branch(label,'0f84')
    b.label('default');b.emit('ff7514ff7510ff750cff7508');b.call(u,'DefWindowProcA');b.branch('return')
    b.label('nc');b.emit('8b5514813a');b.word(MARKER);b.branch('bad_create','0f85')
    b.emit('817a04');b.word(0x400000);b.branch('bad_create','0f85')
    b.emit('8b42248138');b.word(0x526e6957);b.branch('bad_create','0f85')
    b.emit('8b42288138');b.word(0x526e6957);b.branch('bad_create','0f85')
    b.increment(RECORD+4);b.branch('default')
    b.label('bad_create');b.emit('31c0');b.branch('return')
    b.label('create');b.increment(RECORD+8);b.branch('default')
    b.label('custom');b.emit('8b4510034514');b.store(RECORD+12);b.increment(RECORD+20);b.branch('return')
    b.label('paint');b.push(PAINT);b.emit('ff7508');b.call(u,'BeginPaint');b.push(PAINT);b.emit('ff7508');b.call(u,'EndPaint');b.increment(RECORD+16);b.emit('31c0');b.branch('return')
    b.label('destroy');b.increment(RECORD+24);b.invoke(u,'PostQuitMessage',0);b.emit('31c0')
    b.label('return');b.emit('c9c21000')
    b.finish();assert len(b.code)<=4096,len(b.code)
    data=bytearray(0xa000);by_dll={}
    for pos,dll,name in b.calls:by_dll.setdefault(dll,[]);by_dll[dll]+=[] if name in by_dll[dll] else [name]
    at=0x100;symbols={}
    for index,(dll,names) in enumerate(by_dll.items()):
        encoded=dll.encode()+b'\0';dll_at=at;data[at:at+len(encoded)]=encoded;at=(at+len(encoded)+3)&~3
        lookup=at;at+=4*(len(names)+1);iat=at;at+=4*(len(names)+1)
        struct.pack_into('<IIIII',data,index*20,0x2000+lookup,0,0,0x2000+dll_at,0x2000+iat)
        for n,name in enumerate(names):
            encoded=b'\0\0'+name.encode()+b'\0';data[at:at+len(encoded)]=encoded
            struct.pack_into('<I',data,lookup+4*n,0x2000+at);struct.pack_into('<I',data,iat+4*n,0x2000+at)
            symbols[dll,name]=DATA+iat+4*n;at=(at+len(encoded)+1)&~1
    assert at<0xa00,at
    for pos,dll,name in b.calls:struct.pack_into('<I',b.code,pos,symbols[dll,name])
    for at,text in [(CLASS,'WinRecomp independent native window fixture'),(TITLE,'WinRecomp synthetic renderer acceptance'),(FILENAME,'pixels.rgba'),(RECORDNAME,'window.bin'),(PACKNAME,'packed.rgba')]:
        raw=text.encode()+b'\0';data[at-DATA:at-DATA+len(raw)]=raw
    procedure=0x401000+b.labels['wndproc'];struct.pack_into('<12I',data,WC-DATA,48,0x23,procedure,0,0,0x400000,0,0,0,0,CLASS,0)
    struct.pack_into('<HHI',data,PFD-DATA,40,1,0x25);data[PFD-DATA+9]=24;data[PFD-DATA+23]=24;data[PFD-DATA+24]=8
    struct.pack_into('<I',data,MSG-DATA+28,0xdeadbeef);struct.pack_into('<I',data,RECORD-DATA,0x31524757)
    data[PACKED-DATA:PACKED-DATA+32]=bytes([0xa5])*32
    resource_rva,resource_size=icon_resources(data)
    pe=make_pe(b.code,data);put32(pe,0x98+104,0x2000);put32(pe,0x98+108,20*(len(by_dll)+1));put32(pe,0x98+112,resource_rva);put32(pe,0x98+116,resource_size)
    for off,value in [(4,(len(b.code)+511)&~511),(8,len(data)),(20,0x1000),(24,0x2000),(40,5),(48,5),(72,0x100000),(76,0x1000),(80,0x100000),(84,0x1000)]:put32(pe,0x98+off,value)
    return pe,procedure,{100+n:description for n,(label,description) in enumerate(b.failures)}


def inspect_output(root):
    pixels=(root/'pixels.rgba').read_bytes();assert len(pixels)==64*64*4
    center=pixels[(32*64+32)*4:(32*64+32)*4+4];corner=pixels[:4]
    assert center[:3]==bytes([255,0,0]),center
    assert corner[0]==0 and corner[1]==0 and 63<=corner[2]<=65,corner
    record=struct.unpack('<8I',(root/'window.bin').read_bytes())
    assert record[0]==0x31524757 and record[1:4]==(1,1,99) and record[4]>=1 and record[5:7]==(2,1),record
    # Paint count may differ with desktop composition; semantic callback fields
    # and the framebuffer must match, not unspecified notification frequency.
    return pixels,record


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--tool',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--cxx',default='c++');ap.add_argument('--prepare-only',action='store_true');a=ap.parse_args();a.tool=a.tool.resolve();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=True)
    raw,procedure,failures=fixture();pe=a.out/'window.exe';pe.write_bytes(raw)
    project=a.out/'project'
    if project.exists():shutil.rmtree(project)
    c.run([str(a.tool),'project',str(pe),str(project),'--seed',hex(procedure)])
    if a.prepare_only:print('Prepared author-created PE and compiled-code project, not an execution pass');return
    if os.name!='nt':raise SystemExit('Native Windows window/driver oracle unavailable; not counted as a pass.')
    original=a.out/'original';original.mkdir(exist_ok=True)
    result=subprocess.run([str(pe)],cwd=original,capture_output=True,timeout=45)
    if result.returncode:raise RuntimeError(f'Original native PE32 failed: {result.returncode}: {failures.get(result.returncode,"OS/fixture failure")}')
    original_pixels,original_record=inspect_output(original)
    c.run(['cmake','-S',str(project),'-B',str(project/'build'),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',f'-DWINRECOMP_SOURCE={c.ROOT}',f'-DCMAKE_CXX_COMPILER={a.cxx}'])
    c.run(['cmake','--build',str(project/'build'),'--target','recompiled_program','--parallel','2'])
    generated=a.out/'generated';generated.mkdir(exist_ok=True);report=a.out/'process.json'
    result=subprocess.run([str(project/'build/recompiled_program.exe'),str(pe),'--root',str(generated),'--allow-write','--report',str(report)],capture_output=True,text=True,timeout=60)
    if result.returncode:raise RuntimeError(f'Generated native x64 failed: {result.returncode}: {failures.get(result.returncode,"")}\n{result.stderr}\n{result.stdout}')
    pixels,record=inspect_output(generated)
    assert pixels==original_pixels,'Actual Windows PE32/x64 framebuffer mismatch'
    packed=(generated/'packed.rgba').read_bytes()
    assert len(packed)==28 and packed==(original/'packed.rgba').read_bytes(),'Aligned framebuffer mismatch'
    assert packed[12:16]==bytes([0xa5])*4,'Pixel row padding changed'
    assert record[:4]+record[5:]==original_record[:4]+original_record[5:],'Guest callback record mismatch'
    evidence=json.loads(report.read_text());assert evidence['exited'] and evidence['exit_code']==0,evidence
    gui=evidence['gui'];assert gui['backend']=='native-win32' and gui['windows_created']==1 and gui['windows_destroyed']==1 and gui['gl_contexts_created']==1 and gui['swaps']==1,gui
    # PPM is a test framebuffer capture, not a screenshot of E3 or a game frame.
    rgb=b''.join(pixels[(y*64+x)*4:(y*64+x)*4+3] for y in reversed(range(64)) for x in range(64))
    (a.out/'synthetic-frame.ppm').write_bytes(b'P6\n64 64\n255\n'+rgb)
    (a.out/'acceptance.json').write_text(json.dumps({'schema':'winrecomp.window-program.v1','original':'Windows native PE32 loader','generated':'native x64 compiled dispatch','framebuffer_sha256':hashlib.sha256(pixels).hexdigest(),'identical_framebuffers':True,'aligned_readback_sha256':hashlib.sha256(packed).hexdigest(),'original_callback_record':original_record,'generated_callback_record':record,'process':evidence,'scope':'Author-created window and OpenGL fixture. Not E3 startup, game rendering, gameplay, or full Win32 message coverage.'},indent=2)+'\n')
    print('Native Windows PE32 and generated x64: real window, callbacks, resource icon and identical OpenGL triangle passed')
if __name__=='__main__':main()
