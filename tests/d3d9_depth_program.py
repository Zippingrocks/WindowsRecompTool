"""Native Windows original x86/D3D7 versus recompiled x64/D3D9 D16 scenes.

No game bytes. The fixture calls actual legacy APIs and copies all defined RGB
pixels; the test never supplies an image to the recompiled program.
"""
from pathlib import Path
import argparse, hashlib, json, os, shutil, struct, subprocess, uuid
from window_program import Assembler
from fixtures import make_pe, put32
import conformance as c

DATA=0x403000
CLASS,TITLE,NAME=DATA+0x900,DATA+0x940,DATA+0x980
WC,WINDOW,DRAW,D3D,DEVICE,TARGET,Z,WRITTEN= [DATA+x for x in (0xa00,0xa40,0xa44,0xa48,0xa4c,0xa50,0xa54,0xa58)]
IID7,IID3D,IIDHAL=DATA+0xa60,DATA+0xa70,DATA+0xa80
DESC,ZDESC,LOCK,NEAR,FAR,INDICES,CAPS= [DATA+x for x in (0xb00,0xb80,0xc00,0xd00,0xd60,0xdd0,0xde0)]
SCENARIO,REMAINING,OUTPTR=DATA+0xe00,DATA+0xe04,DATA+0xe08
TABLE,OUTPUT=DATA+0x1000,DATA+0x2000
RED,BLUE,BACK=0xff0000,0x0000ff,0x183050
# name, Z enable/write/compare, initial/near/far Z, intermediate operation,
# independently expected interior RGB. Depths are exactly representable binary
# fractions so these tests are not comparing rasterizer-specific rounding noise.
SCENES=[
    ('less',1,1,2,1.,.25,.75,0,RED),
    ('less_equal_tie',1,1,4,1.,.5,.5,0,BLUE),
    ('less_tie',1,1,2,1.,.5,.5,0,RED),
    ('greater',1,1,5,0.,.75,.25,0,RED),
    ('greater_equal_tie',1,1,7,0.,.5,.5,0,BLUE),
    ('equal',1,1,3,.5,.25,.5,0,BLUE),
    ('not_equal',1,1,6,.5,.25,.25,0,RED),
    ('never',1,1,1,1.,.25,.75,0,BACK),
    ('always',1,1,8,1.,.25,.75,0,BLUE),
    ('writes_disabled',1,0,4,1.,.25,.75,0,BLUE),
    ('testing_disabled',0,1,4,1.,.25,.75,0,BLUE),
    ('color_clear_preserves_depth',1,1,4,1.,.25,.75,1,BACK),
    ('depth_clear_preserves_color',1,1,4,1.,.25,.75,2,BLUE),
    ('detach_rebind_preserves_depth',1,1,4,1.,.25,.75,3,RED),
]

def fixture():
    b=Assembler();u='USER32.dll';dd='DDRAW.dll';k='KERNEL32.dll'
    def config(offset): return ('config',offset)
    def com(obj,slot,*args):
        for arg in reversed(((obj,),)+args):
            if isinstance(arg,tuple) and arg[0]=='config':
                b.emit('8b15');b.word(SCENARIO);b.emit('ffb2');b.word(arg[1])
            elif isinstance(arg,tuple): b.memory_push(arg[0])
            else: b.push(arg)
        b.emit('a1');b.word(obj);b.emit('8b00ff90');b.word(4*slot)
    def hr(text): b.eax_is(0,text)
    b.invoke(u,'RegisterClassExA',WC);b.nonzero('register')
    b.invoke(u,'CreateWindowExA',0,CLASS,TITLE,0xcf0000,20,20,160,160,0,0,0x400000,0);b.nonzero('window');b.store(WINDOW)
    b.invoke(dd,'DirectDrawCreateEx',0,DRAW,IID7,0);hr('DirectDrawCreateEx')
    com(DRAW,20,(WINDOW,),8);hr('cooperative level')
    com(DRAW,0,IID3D,D3D);hr('D3D7 interface')
    com(DRAW,6,DESC,TARGET,0);hr('color target')
    com(DRAW,6,ZDESC,Z,0);hr('D16 surface')
    com(TARGET,3,(Z,));hr('attach depth')
    com(Z,2) # Attachment must retain the resource without this guest reference.
    com(TARGET,12,CAPS,Z);hr('retained depth')
    com(D3D,4,IIDHAL,(TARGET,),DEVICE);hr('device with attached depth')
    for key,value in ((137,0),(22,1),(9,2),(27,0),(28,0),(29,0)):
        com(DEVICE,20,key,value);hr('render state '+str(key))
    b.label('scenario')
    for key,offset in ((7,0),(14,4),(23,8)):
        com(DEVICE,20,key,config(offset));hr('depth state '+str(key))
    com(DEVICE,10,0,0,3,0xff183050,config(12),0);hr('clear color and depth')
    b.emit('a1');b.word(SCENARIO);b.emit('8b4010')
    for offset in (8,28,48): b.store(NEAR+offset)
    com(DEVICE,5);hr('first BeginScene')
    com(DEVICE,25,4,0x44,NEAR,3,0);hr('near nonindexed draw')
    com(DEVICE,6);hr('first EndScene')
    # Optional operation between scenes. Depth must persist unless explicitly
    # cleared; scene transitions and reattachment may not recreate empty Z.
    b.emit('a1');b.word(SCENARIO);b.emit('8b401883f801');b.branch('not_color','0f85')
    com(DEVICE,10,0,0,1,0xff183050,0x7fc00000,0);hr('color-only clear ignores inactive NaN Z')
    b.branch('after_operation')
    b.label('not_color');b.emit('83f802');b.branch('not_depth','0f85')
    com(DEVICE,10,0,0,2,0xff000000,0x3f800000,0);hr('depth-only clear')
    b.branch('after_operation')
    b.label('not_depth');b.emit('83f803');b.branch('after_operation','0f85')
    com(TARGET,8,0,(Z,));hr('detach depth');com(TARGET,3,(Z,));hr('rebind same depth')
    b.label('after_operation')
    b.emit('a1');b.word(SCENARIO);b.emit('8b4014')
    for offset in (8,28,48): b.store(FAR+offset)
    com(DEVICE,5);hr('second BeginScene')
    com(DEVICE,26,4,0x44,FAR,3,INDICES,3,0);hr('far indexed draw')
    com(DEVICE,6);hr('second EndScene')
    com(TARGET,25,0,LOCK,0x11,0);hr('Lock target')
    b.compare(LOCK+124,0xfeedcafe,'target lock canary')
    b.emit('8b35');b.word(LOCK+36);b.emit('8b3d');b.word(OUTPTR)
    b.emit('bb');b.word(64);b.label('row');b.emit('b9');b.word(64);b.emit('89f2');b.label('pixel')
    b.emit('8b0225ffffff00890783c20483c70449');b.branch('pixel','0f85')
    b.emit('0335');b.word(LOCK+16);b.emit('4b');b.branch('row','0f85')
    b.emit('893d');b.word(OUTPTR)
    com(TARGET,32,0);hr('Unlock target')
    b.emit('8305');b.word(SCENARIO);b.emit('20ff0d');b.word(REMAINING);b.branch('scenario','0f85')
    com(TARGET,8,0,(Z,));hr('detach cleanup');com(Z,2);com(DEVICE,2);com(TARGET,2);com(D3D,2);com(DRAW,2)
    b.invoke(u,'DestroyWindow',(WINDOW,));b.nonzero('DestroyWindow')
    b.invoke(u,'UnregisterClassA',CLASS,0x400000);b.nonzero('UnregisterClass')
    b.invoke(k,'CreateFileA',NAME,0x40000000,0,0,2,0x80,0);b.emit('83f8ff');b.fail('create result');b.emit('89c3')
    b.push(0);b.push(WRITTEN);b.push(len(SCENES)*64*64*4);b.push(OUTPUT);b.emit('53');b.call(k,'WriteFile');b.nonzero('write result')
    b.compare(WRITTEN,len(SCENES)*64*64*4,'result size');b.emit('53');b.call(k,'CloseHandle');b.nonzero('close result')
    b.invoke(k,'ExitProcess',0);b.emit('c3')
    b.label('wndproc');b.emit('558becff7514ff7510ff750cff7508');b.call(u,'DefWindowProcA');b.emit('c9c21000')
    b.finish();callback=0x401000+b.labels['wndproc'];assert len(b.code)<=8192
    data=bytearray(0x40000);by_dll={}
    for _,dll,name in b.calls:
        if name not in by_dll.setdefault(dll,[]):by_dll[dll].append(name)
    at=0x100;symbols={}
    for i,(dll,names) in enumerate(by_dll.items()):
        text=dll.encode()+b'\0';dll_at=at;data[at:at+len(text)]=text;at=(at+len(text)+3)&~3
        lookup=at;at+=4*(len(names)+1);iat=at;at+=4*(len(names)+1)
        struct.pack_into('<IIIII',data,i*20,0x3000+lookup,0,0,0x3000+dll_at,0x3000+iat)
        for n,name in enumerate(names):
            text=b'\0\0'+name.encode()+b'\0';data[at:at+len(text)]=text
            for table in (lookup,iat): struct.pack_into('<I',data,table+4*n,0x3000+at)
            symbols[dll,name]=DATA+iat+4*n;at=(at+len(text)+1)&~1
    assert at<0x900
    for pos,dll,name in b.calls:struct.pack_into('<I',b.code,pos,symbols[dll,name])
    def word(at,value):struct.pack_into('<I',data,at-DATA,value)
    for at,text in ((CLASS,b'WinRecompDepth16'),(TITLE,b'Authored depth test'),(NAME,b'depth.bin')):data[at-DATA:at-DATA+len(text)+1]=text+b'\0'
    for at,id in ((IID7,'15e65ec0-3b9c-11d2-b92f-00609797ea5b'),(IID3D,'f5049e77-4861-11d2-a407-00a0c90629a8'),(IIDHAL,'84e63de0-46aa-11cf-816f-0000c020156e')):data[at-DATA:at-DATA+16]=uuid.UUID(id).bytes_le
    for at,value in ((WC,48),(WC+8,callback),(WC+20,0x400000),(WC+40,CLASS),(LOCK,124),(LOCK+124,0xfeedcafe),
                     (SCENARIO,TABLE),(REMAINING,len(SCENES)),(OUTPTR,OUTPUT),(CAPS,0x20000)) :word(at,value)
    for at,is_z in ((DESC,False),(ZDESC,True)):
        for off,value in ((0,124),(4,0x1007),(8,64),(12,64),(72,32),(76,0x400 if is_z else 0x40),
                          (84,16 if is_z else 32),(88,0 if is_z else 0xff0000),(92,0xffff if is_z else 0xff00),
                          (96,0 if is_z else 0xff),(104,0x24000 if is_z else 0x2040)):word(at+off,value)
    for at,color in ((NEAR,0xffff0000),(FAR,0xff0000ff)):
        for n,(x,y) in enumerate(((8,8),(56,8),(8,56))):struct.pack_into('<ffffI',data,at-DATA+20*n,x,y,.5,1,color)
    struct.pack_into('<3H',data,INDICES-DATA,0,1,2)
    for n,(_,enable,write,func,initial,near,far,operation,_) in enumerate(SCENES):
        struct.pack_into('<IIIfffII',data,TABLE-DATA+32*n,enable,write,func,initial,near,far,operation,0)
    raw=make_pe(b.code.ljust(8192,b'\xcc'),data)
    put32(raw,0x98+104,0x3000);put32(raw,0x98+108,20*(len(by_dll)+1))
    for off,value in ((4,8192),(8,len(data)),(20,0x1000),(24,0x3000),(40,5),(48,5),(72,0x100000),(76,0x1000),(80,0x100000),(84,0x1000)):put32(raw,0x98+off,value)
    return raw,callback,{100+n:text for n,(_,text) in enumerate(b.failures)}

def inspect_pixels(path):
    data=path.read_bytes();assert len(data)==len(SCENES)*16384
    for n,scene in enumerate(SCENES):
        words=struct.unpack('<4096I',data[n*16384:(n+1)*16384]);expected=scene[-1]
        assert words[0]==BACK and words[-1]==BACK,(scene[0],'background')
        assert words[16*64+16]==expected,(scene[0],hex(words[16*64+16]),hex(expected))
        assert set(words)<={BACK,expected},(scene[0],'unexpected colors')
        if expected!=BACK:assert sum(p==expected for p in words)>1000,(scene[0],'missing triangle')
    return data

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--tool',required=True,type=Path);ap.add_argument('--out',required=True,type=Path)
    ap.add_argument('--cxx',default='cl');ap.add_argument('--prepare-only',action='store_true');a=ap.parse_args()
    out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
    raw,callback,failures=fixture();original=out/'original.exe';original.write_bytes(raw);project=out/'project';shutil.rmtree(project,ignore_errors=True)
    c.run([str(a.tool.resolve()),'project',str(original),str(project),'--seed',hex(callback)])
    if a.prepare_only:print('Prepared depth fixtures only; no Windows execution or acceptance');return
    if os.name!='nt':raise RuntimeError('Native Windows required; other hosts are not acceptance')
    c.run(['cmake','-S',str(project),'-B',str(project/'build'),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',f'-DWINRECOMP_SOURCE={c.ROOT}',f'-DCMAKE_CXX_COMPILER={a.cxx}'])
    c.run(['cmake','--build',str(project/'build'),'--target','recompiled_program','--parallel','2'])
    results=[]
    for repeat in range(2):
        for label,cmd in [('original-d3d7',[str(original)]),('generated-d3d9',[str(project/'build/recompiled_program.exe'),str(original),'--legacy-renderer','d3d9'])]:
            folder=out/f'{label}-{repeat}';folder.mkdir(exist_ok=True)
            if label.startswith('generated'):cmd+=['--root',str(folder),'--allow-write','--report',str(folder/'process.json')]
            result=subprocess.run(cmd,cwd=folder,capture_output=True,text=True,timeout=120)
            (folder/'run.log').write_text(result.stdout+'\n'+result.stderr)
            if result.returncode:raise RuntimeError(f'{label}: {result.returncode}: {failures.get(result.returncode,"OS/runtime failure")}\n{result.stderr}')
            results.append(inspect_pixels(folder/'depth.bin'))
            if label.startswith('generated'):
                process=json.loads((folder/'process.json').read_text());dd=process['directdraw']
                assert dd['d3d9_draws']==2*len(SCENES) and dd['d3d9_indexed_draws']==len(SCENES),dd
                assert dd['d3d9_depth_surfaces']==1 and dd['objects_created']==dd['objects_retired'],dd
    assert all(data==results[0] for data in results),'original/generated/repeated complete RGB outputs differ'
    report={'schema':'winrecomp.depth-native.v1','host':'native Windows','original':'PE32 x86 D3D7',
            'generated':'compiled PE32+ x64 D3D9','input_sha256':hashlib.sha256(raw).hexdigest(),
            'scenes':[s[0] for s in SCENES],'pixels_per_scene':4096,'executions':4,'all_rgb_pixels_identical':True,
            'output_sha256':hashlib.sha256(results[0]).hexdigest(),'last_process':process,
            'scope':'Authored offscreen D16, eight Z comparisons, disable/write/clear/rebind; not E3 or presentation'}
    (out/'acceptance.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'PASS: {len(SCENES)} D16 scenes, all RGB pixels original/generated/repeated on native Windows')
if __name__=='__main__':main()
