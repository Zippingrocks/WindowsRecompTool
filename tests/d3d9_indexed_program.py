"""Native Windows original PE32/D3D7 vs generated x64/D3D9 indexed scenes."""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess
import conformance as c
from d3d9_program import fixture, inspect_pixels
from d3d9_texture_program import inspect as texture_pixels

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--tool',type=Path,required=True);ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--cxx',default='cl');ap.add_argument('--prepare-only',action='store_true');a=ap.parse_args()
    out=a.out.resolve();out.mkdir(parents=True,exist_ok=True);rows=[]
    for textured in (False,True):
        folder=out/('textured' if textured else 'untextured');folder.mkdir(exist_ok=True)
        raw,callback,failures=fixture(textured=textured,indexed=True)
        original=folder/'original.exe';original.write_bytes(raw);project=folder/'project'
        shutil.rmtree(project,ignore_errors=True)
        c.run([str(a.tool.resolve()),'project',str(original),str(project),'--seed',hex(callback)])
        if a.prepare_only:continue
        if os.name!='nt':raise RuntimeError('Native Windows required; preparation/cross-build is not an execution pass')
        c.run(['cmake','-S',str(project),'-B',str(project/'build'),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',f'-DWINRECOMP_SOURCE={c.ROOT}',f'-DCMAKE_CXX_COMPILER={a.cxx}'])
        c.run(['cmake','--build',str(project/'build'),'--target','recompiled_program','--parallel','2'])
        results=[]
        # Repeat each path. No excluded pixels/regions; only the shared undefined
        # high X byte is masked by the authored guest code, just as in baseline.
        for repeat in range(2):
            for label,cmd in [('original-d3d7',[str(original)]),('generated-d3d9',[str(project/'build/recompiled_program.exe'),str(original),'--legacy-renderer','d3d9'])]:
                directory=folder/f'{label}-{repeat}';directory.mkdir(exist_ok=True)
                if label.startswith('generated'):cmd+=['--root',str(directory),'--allow-write','--report',str(directory/'process.json')]
                result=subprocess.run(cmd,cwd=directory,capture_output=True,text=True,timeout=90)
                (directory/'run.log').write_text(result.stdout+'\n'+result.stderr,encoding='utf-8')
                if result.returncode:raise RuntimeError(f'{label}: {result.returncode} {failures.get(result.returncode,"runtime/OS failure")}\n{result.stderr}')
                results.append((texture_pixels if textured else inspect_pixels)(directory/'triangle.bin'))
                if label.startswith('generated'):
                    process=json.loads((directory/'process.json').read_text());dd=process['directdraw']
                    assert process['exit_code']==0 and dd['d3d9_indexed_draws']==(2 if textured else 1),dd
                    assert dd['d3d9_draws']==dd['d3d9_indexed_draws'] and dd['objects_created']==dd['objects_retired'],dd
        assert all(data==results[0] for data in results), 'original/recompiled/repeated indexed pixels differ'
        rows.append({'scene':folder.name,'input_sha256':hashlib.sha256(raw).hexdigest(),
            'pixels_per_run':4096,'native_original_runs':2,'native_generated_runs':2,'all_rgb_pixels_identical':True,
            'output_sha256':hashlib.sha256(results[0]).hexdigest(),'final_process':process})
    if a.prepare_only:print('Prepared indexed scenes only; no Windows execution');return
    report={'schema':'winrecomp.indexed-native.v1','host':'native Windows','original':'x86 PE32 D3D7',
        'generated':'compiled Windows x64 D3D9','scenes':rows,'scope':'authored indexed TL triangle lists, not E3, depth or presentation'}
    (out/'acceptance.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS: original x86 and recompiled x64 sparse WORD-indexed RGB/texture pixels, twice per path')
if __name__=='__main__':main()
