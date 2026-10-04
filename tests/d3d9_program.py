"""Original PE32/D3D7 vs recompiled native x64/D3D9 pixel acceptance.

Only authored bytes. No native x64 IDirect3D7 provider is required on the
translated path. No interpreter, image substitution or host-only drawn image.
"""
from pathlib import Path
import argparse, hashlib, json, os, shutil, struct, subprocess, uuid
from window_program import Assembler
from fixtures import make_pe,put32
import conformance as c
DATA=0x402000
CLASS,TITLE,NAME=DATA+0x900,DATA+0x940,DATA+0x980
WC,WINDOW,DRAW,D3D,DEVICE,SURFACE,ALIAS=DATA+0xa00,DATA+0xa40,DATA+0xa44,DATA+0xa48,DATA+0xa4c,DATA+0xa50,DATA+0xa54
UNK=DATA+0xa58
IID7,IID3D,IIDHAL,IIDUNK=DATA+0xa60,DATA+0xa70,DATA+0xa80,DATA+0xa90
DESC,LOCK,VIEW,VERTS,WRITTEN,OUTPUT=DATA+0xb00,DATA+0xb80,DATA+0xc00,DATA+0xc40,DATA+0xca0,DATA+0x1000
TEX,TEXDESC,TEXLOCK=DATA+0xcf0,DATA+0xd00,DATA+0xd80
WIDTH=64;HEIGHT=64

def fixture(textured=False):
    vertex_at=DATA+0xe20 if textured else VERTS
    b=Assembler();u='USER32.dll';dd='DDRAW.dll';k='KERNEL32.dll'
    def com(obj,slot,*args):
        for value in reversed(((obj,),)+args):
            if isinstance(value,tuple):b.memory_push(value[0])
            else:b.push(value)
        b.emit('a1');b.word(obj);b.emit('8b00ff90');b.word(slot*4)
    def hr(text):b.eax_is(0,text)
    b.invoke(u,'RegisterClassExA',WC);b.nonzero('register class')
    b.invoke(u,'CreateWindowExA',0,CLASS,TITLE,0x00cf0000,32,32,160,160,0,0,0x400000,0)
    b.nonzero('create window');b.store(WINDOW)
    b.invoke(dd,'DirectDrawCreateEx',0,DRAW,IID7,0);hr('DirectDrawCreateEx')
    com(DRAW,20,(WINDOW,),8);hr('SetCooperativeLevel')
    com(DRAW,0,IID3D,D3D);hr('QI IDirect3D7')
    com(D3D,0,IIDUNK,UNK);hr('controlling identity')
    com(UNK,0,IID3D,ALIAS);hr('QI back to legacy interface')
    b.emit('a1');b.word(D3D);b.emit('3b05');b.word(ALIAS);b.fail('stable legacy interface identity','0f85')
    com(ALIAS,2);com(UNK,2)
    com(DRAW,6,DESC,SURFACE,0);hr('create render target')
    com(D3D,4,IIDHAL,(SURFACE,),DEVICE);hr('create render device')
    com(DEVICE,9,ALIAS);hr('GetRenderTarget');com(ALIAS,2)
    for state,value in [(7,0),(14,0),(137,0),(22,1),(9,2),(27,0),(28,0),(29,0)]:
        com(DEVICE,20,state,value);hr('render state '+str(state))
    if textured:
        com(DRAW,6,TEXDESC,TEX,0);hr('create texture surface')
        def fill_texture(uniform=False):
            com(TEX,25,0,TEXLOCK,1,0);hr('lock texture')
            # Eight rows, four texel colors, actual native row pitch.
            b.emit('8b35');b.word(TEXLOCK+36)
            for y in range(8):
                for x in range(8):
                    color=0xff345678 if uniform else (0xffff0000 if x<4 else 0xff00ff00) if y<4 else (0xff0000ff if x<4 else 0xffffffff)
                    if uniform:color=0xff345678
                    b.emit('c786');b.word(x*4);b.word(color)
                b.emit('0335');b.word(TEXLOCK+16)
            com(TEX,32,0);hr('unlock texture')
        fill_texture()
        com(DEVICE,35,0,(TEX,));hr('SetTexture')
        com(DEVICE,34,0,ALIAS);hr('GetTexture');com(ALIAS,2)
        for state,value in [(1,2),(2,2),(3,0),(4,2),(5,2),(6,0),(13,3),(14,3),(16,1),(17,1),(18,1),(11,0)]:
            com(DEVICE,37,0,state,value);hr('texture stage '+str(state))
    com(DEVICE,13,VIEW);hr('SetViewport')
    com(DEVICE,10,0,0,1,0xff183050,0x3f800000,0);hr('Clear')
    com(DEVICE,5);hr('BeginScene')
    com(DEVICE,25,4,0x144 if textured else 0x44,vertex_at,3,0);hr('DrawPrimitive TL triangle')
    com(DEVICE,6);hr('EndScene')
    if textured:
        # Edit after binding, without SetTexture again; then draw a second,
        # disjoint triangle sampling that texel. The binding owns a reference.
        com(TEX,25,0,TEXLOCK,1,0);hr('texture CPU edit')
        b.emit('8b35');b.word(TEXLOCK+36);b.emit('c706');b.word(0xff345678)
        com(TEX,32,0);hr('texture edit unlock')
        com(TEX,2)
        com(DEVICE,34,0,ALIAS);hr('retained texture');com(ALIAS,2)
        # Reuse the six vertex slots: the second triangle is at VERTS+84.
        com(DEVICE,5);hr('second BeginScene')
        com(DEVICE,25,4,0x144,vertex_at+84,3,0);hr('changed texture draw')
        com(DEVICE,6);hr('second EndScene')
        com(DEVICE,35,0,0);hr('unbind texture')
    com(SURFACE,25,0,LOCK,0x11,0);hr('Lock completed render target')
    b.compare(LOCK+124,0xfeedcafe,'Lock canary')
    b.emit('8b35');b.word(LOCK+36);b.emit('bf');b.word(OUTPUT)
    b.emit('bb');b.word(HEIGHT);b.label('row')
    b.emit('b9');b.word(WIDTH);b.emit('89f2');b.label('pixel')
    b.emit('8b0225ffffff00890783c20483c70449');b.branch('pixel','0f85')
    b.emit('0335');b.word(LOCK+16);b.emit('4b');b.branch('row','0f85')
    com(SURFACE,32,0);hr('Unlock')
    com(DEVICE,2);com(SURFACE,2);com(D3D,2);com(DRAW,2)
    b.invoke(u,'DestroyWindow',(WINDOW,));b.nonzero('DestroyWindow')
    b.invoke(u,'UnregisterClassA',CLASS,0x400000);b.nonzero('UnregisterClass')
    b.invoke(k,'CreateFileA',NAME,0x40000000,0,0,2,0x80,0);b.emit('83f8ff');b.fail('create result file');b.emit('89c3')
    b.push(0);b.push(WRITTEN);b.push(WIDTH*HEIGHT*4);b.push(OUTPUT);b.emit('53');b.call(k,'WriteFile');b.nonzero('write result')
    b.compare(WRITTEN,WIDTH*HEIGHT*4,'result size');b.emit('53');b.call(k,'CloseHandle');b.nonzero('close result')
    b.invoke(k,'ExitProcess',0);b.emit('c3')
    b.label('wndproc');b.emit('558becff7514ff7510ff750cff7508');b.call(u,'DefWindowProcA');b.emit('c9c21000')
    b.finish();callback=0x401000+b.labels['wndproc'];assert len(b.code)<=4096
    data=bytearray(0x6000);by_dll={}
    for _,dll,name in b.calls:
        if name not in by_dll.setdefault(dll,[]):by_dll[dll].append(name)
    at=0x100;symbols={}
    for index,(dll,names) in enumerate(by_dll.items()):
        text=dll.encode()+b'\0';dll_at=at;data[at:at+len(text)]=text;at=(at+len(text)+3)&~3
        lookup=at;at+=4*(len(names)+1);iat=at;at+=4*(len(names)+1)
        struct.pack_into('<IIIII',data,index*20,0x2000+lookup,0,0,0x2000+dll_at,0x2000+iat)
        for n,name in enumerate(names):
            text=b'\0\0'+name.encode()+b'\0';data[at:at+len(text)]=text
            struct.pack_into('<I',data,lookup+n*4,0x2000+at);struct.pack_into('<I',data,iat+n*4,0x2000+at)
            symbols[dll,name]=DATA+iat+n*4;at=(at+len(text)+1)&~1
    assert at<0x900
    for pos,dll,name in b.calls:struct.pack_into('<I',b.code,pos,symbols[dll,name])
    def word(at,value):struct.pack_into('<I',data,at-DATA,value)
    for at,text in [(CLASS,b'WinRecompLegacy9'),(TITLE,b'Synthetic D3D7 on 9'),(NAME,b'triangle.bin')]:data[at-DATA:at-DATA+len(text)+1]=text+b'\0'
    for at,id in [(IID7,'15e65ec0-3b9c-11d2-b92f-00609797ea5b'),(IID3D,'f5049e77-4861-11d2-a407-00a0c90629a8'),(IIDHAL,'84e63de0-46aa-11cf-816f-0000c020156e'),(IIDUNK,'00000000-0000-0000-c000-000000000046')]:data[at-DATA:at-DATA+16]=uuid.UUID(id).bytes_le
    for at,value in [(WC,48),(WC+8,callback),(WC+20,0x400000),(WC+40,CLASS),
        (DESC,124),(DESC+4,0x1007),(DESC+8,HEIGHT),(DESC+12,WIDTH),(DESC+72,32),(DESC+76,0x40),
        (DESC+84,32),(DESC+88,0xff0000),(DESC+92,0xff00),(DESC+96,0xff),(DESC+104,0x2040),
        (LOCK,124),(LOCK+124,0xfeedcafe),(VIEW+8,WIDTH),(VIEW+12,HEIGHT),(VIEW+20,0x3f800000)]:word(at,value)
    if textured:
        # Texture data and six vertices must not overlap WRITTEN (DATA+0xca0).
        # Only five words there were used by the baseline; relocate the new
        # textured vertices to unused tail storage below the output buffer.
        for at,value in [(TEXDESC,124),(TEXDESC+4,0x1007),(TEXDESC+8,8),(TEXDESC+12,8),
            (TEXDESC+72,32),(TEXDESC+76,0x41),(TEXDESC+84,32),(TEXDESC+88,0xff0000),
            (TEXDESC+92,0xff00),(TEXDESC+96,0xff),(TEXDESC+100,0xff000000),(TEXDESC+104,0x1800),(TEXLOCK,124)]:word(at,value)
        for n,(x,y,u,v) in enumerate([(8,8,0,0),(56,8,1,0),(8,56,0,1),(56,56,.0625,.0625),(56,36,.0625,.0625),(36,56,.0625,.0625)]):
            struct.pack_into('<ffffIff',data,vertex_at-DATA+n*28,x,y,.5,1,0xffffffff,u,v)
    else:
        for n,(x,y) in enumerate([(8,8),(56,8),(8,56)]):struct.pack_into('<ffffI',data,VERTS-DATA+n*20,x,y,0.5,1,0xffd04020)
    raw=make_pe(b.code,data);put32(raw,0x98+104,0x2000);put32(raw,0x98+108,20*(len(by_dll)+1))
    for off,value in [(4,(len(b.code)+511)&~511),(8,len(data)),(20,0x1000),(24,0x2000),(40,5),(48,5),(72,0x100000),(76,0x1000),(80,0x100000),(84,0x1000)]:put32(raw,0x98+off,value)
    return raw,callback,{100+n:text for n,(_,text) in enumerate(b.failures)}

def inspect_pixels(path):
    data=path.read_bytes();assert len(data)==WIDTH*HEIGHT*4
    words=struct.unpack('<4096I',data)
    assert words[0]==0x183050 and words[16*WIDTH+16]==0xd04020 and words[-1]==0x183050
    assert set(words)=={0x183050,0xd04020},'unexpected triangle colors'
    assert sum(p==0xd04020 for p in words)>1000,'triangle missing or only a clear was rendered'
    return data

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--tool',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--prepare-only',action='store_true');ap.add_argument('--cxx',default='cl');a=ap.parse_args()
    out=a.out.resolve();out.mkdir(parents=True,exist_ok=True);raw,callback,failures=fixture();original=out/'original.exe';original.write_bytes(raw)
    project=out/'project';shutil.rmtree(project,ignore_errors=True);c.run([str(a.tool.resolve()),'project',str(original),str(project),'--seed',hex(callback)])
    if a.prepare_only:print('Prepared only; no Windows acceptance');return
    if os.name!='nt':raise RuntimeError('Native Windows required; a Wine run is a separate diagnostic')
    c.run(['cmake','-S',str(project),'-B',str(project/'build'),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',f'-DWINRECOMP_SOURCE={c.ROOT}',f'-DCMAKE_CXX_COMPILER={a.cxx}'])
    c.run(['cmake','--build',str(project/'build'),'--target','recompiled_program','--parallel','2'])
    results=[];runs=[]
    for name,cmd in [('original-d3d7',[str(original)]),('generated-d3d9',[str(project/'build/recompiled_program.exe'),str(original),'--legacy-renderer','d3d9'])]:
        directory=out/name;directory.mkdir(exist_ok=True)
        if name.startswith('generated'):cmd+=['--root',str(directory),'--allow-write','--report',str(out/'process.json')]
        result=subprocess.run(cmd,cwd=directory,capture_output=True,text=True,timeout=90)
        (out/(name+'.log')).write_text(result.stdout+'\n'+result.stderr)
        if result.returncode:raise RuntimeError(f'{name} failed {result.returncode}: {failures.get(result.returncode,"runtime/OS failure")}\n{result.stderr}')
        results.append(inspect_pixels(directory/'triangle.bin'));runs.append({'path':name,'exit_code':result.returncode})
    assert results[0]==results[1],'original D3D7 and recompiled D3D9 pixels differ'
    process=json.loads((out/'process.json').read_text());dd=process['directdraw']
    assert dd['backend']=='ddraw-d3d9-bounded' and dd['d3d9_devices']==1 and dd['d3d9_draws']==1 and dd['d3d9_clears']==1
    assert dd['objects_created']==dd['objects_retired'],dd
    evidence={'schema':'winrecomp.d3d9-render-proof.v1','original':'native Windows x86 D3D7','generated':'native Windows x64 D3D9, compiled guest',
        'identical_pixels':True,'width':WIDTH,'height':HEIGHT,'framebuffer_sha256':hashlib.sha256(results[0]).hexdigest(),'runs':runs,
        'process':process,'scope':'Authored untextured transformed triangle; not E3, full D3D7, textures, depth or presentation'}
    (out/'acceptance.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print('PASS: original x86 D3D7 and native recompiled x64 D3D9 rendered identical triangle pixels')
if __name__=='__main__':main()
