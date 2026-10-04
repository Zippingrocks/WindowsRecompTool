"""Original Windows x86 textured D3D7 vs compiled x64 D3D9, authored input only."""
from pathlib import Path
import argparse, hashlib, json, os, shutil, struct, subprocess
import conformance as c
from d3d9_program import fixture, WIDTH, HEIGHT

def inspect(path):
    data=path.read_bytes()
    if len(data)!=WIDTH*HEIGHT*4:raise AssertionError('incomplete texture scene readback')
    words=struct.unpack('<4096I',data)
    probes={(0,0):0x183050,(16,16):0xff0000,(40,16):0xff00,(16,40):0xff,
            (50,50):0x345678,(63,63):0x183050}
    for (x,y),want in probes.items():
        if words[y*WIDTH+x]!=want:raise AssertionError(f'pixel ({x},{y}): {words[y*WIDTH+x]:06x} != {want:06x}')
    for color in (0xff0000,0xff00,0xff,0x345678):
        if words.count(color)<50:raise AssertionError('texture region or post-bind CPU edit missing')
    return data

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--tool',type=Path,required=True);ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--cxx',default='cl');ap.add_argument('--prepare-only',action='store_true');a=ap.parse_args()
    out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
    raw,callback,failures=fixture(textured=True);original=out/'original.exe';original.write_bytes(raw)
    project=out/'project';shutil.rmtree(project,ignore_errors=True)
    c.run([str(a.tool.resolve()),'project',str(original),str(project),'--seed',hex(callback)])
    if a.prepare_only:print('Prepared texture fixture only; no Windows execution result');return
    if os.name!='nt':raise RuntimeError('Native Windows required; Wine diagnostics must be labeled separately')
    c.run(['cmake','-S',str(project),'-B',str(project/'build'),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',f'-DWINRECOMP_SOURCE={c.ROOT}',f'-DCMAKE_CXX_COMPILER={a.cxx}'])
    c.run(['cmake','--build',str(project/'build'),'--target','recompiled_program','--parallel','2'])
    outputs=[]
    for name,cmd in [('original-d3d7',[str(original)]),('generated-d3d9',[str(project/'build/recompiled_program.exe'),str(original),'--legacy-renderer','d3d9'])]:
        directory=out/name;directory.mkdir(exist_ok=True)
        if name.startswith('generated'):cmd+=['--root',str(directory),'--allow-write','--report',str(out/'process.json')]
        result=subprocess.run(cmd,cwd=directory,capture_output=True,text=True,timeout=90)
        (out/(name+'.log')).write_text(result.stdout+'\n'+result.stderr,encoding='utf-8')
        if result.returncode:raise RuntimeError(f'{name}: {result.returncode} {failures.get(result.returncode,"runtime/OS failure")}\n{result.stderr}')
        outputs.append(inspect(directory/'triangle.bin'))
    if outputs[0]!=outputs[1]:raise AssertionError('Original D3D7 and recompiled D3D9 texture framebuffers differ')
    process=json.loads((out/'process.json').read_text());d=process['directdraw']
    assert process['exit_code']==0 and d['d3d9_draws']==2 and d['d3d9_devices']==1
    assert d['objects_created']==d['objects_retired'],d
    report={'schema':'winrecomp.d3d9-texture-proof.v1','original':'native Windows x86 D3D7',
            'generated':'native Windows x64 D3D9, compiled guest','source_input_sha256':hashlib.sha256(raw).hexdigest(),
            'pixels_compared':WIDTH*HEIGHT,'identical_rgb_pixels':True,'framebuffer_sha256':hashlib.sha256(outputs[0]).hexdigest(),
            'texture_cpu_edit_after_binding':True,'texture_lifetime_after_guest_release':True,'process':process,
            'scope':'Authored single-stage point-filtered ARGB texture, two triangles; not E3, full D3D7 or presentation'}
    (out/'acceptance.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('PASS: all 4096 original-x86/recompiled-x64 texture pixels, post-bind CPU edit and retained texture')
if __name__=='__main__':main()
