"""Validate a user-supplied PE against a metadata-only integration contract."""
from __future__ import annotations
import argparse
import collections
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--tool',type=Path,required=True)
    ap.add_argument('--exe',type=Path,required=True)
    ap.add_argument('--contract',type=Path,required=True)
    ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--conformance',action='store_true')
    ap.add_argument('--cxx',default='c++')
    a=ap.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    contract=json.loads(a.contract.read_text())
    digest=hashlib.sha256(a.exe.read_bytes()).hexdigest()
    if digest!=contract['sha256']:raise RuntimeError('input SHA-256 mismatch; refusing to apply addresses from another build')
    started=time.perf_counter()
    def run(*cmd):return subprocess.run(list(map(str,cmd)),check=True,capture_output=True).stdout
    pe=json.loads(run(a.tool,'analyze',a.exe))
    for key in ['image_base','entry','image_size']:
        if pe[key]!=contract[key]:raise RuntimeError('PE mismatch: '+key)
    if len(pe['imports'])!=contract['import_count']:raise RuntimeError('import count mismatch')
    if [s['name'] for s in pe['sections']]!=contract['sections']:raise RuntimeError('section mismatch')
    first=run(a.tool,'cfg',a.exe);second=run(a.tool,'cfg',a.exe)
    if first!=second:raise RuntimeError('nondeterministic CFG')
    (a.out/'cfg.json').write_bytes(first);g=json.loads(first)
    if g['budget_exhausted'] or g['diagnostics']:raise RuntimeError('CFG discovery diagnostic/budget failure')
    counts=collections.Counter(e['kind'] for e in g['edges'])
    actual={'instructions':len(g['instructions']),'blocks':len(g['blocks']),'function_candidates':len(g['function_candidates']),
            'unresolved_indirect_transfers':counts['call_indirect']+counts['jump_indirect'],'jump_table_candidates':len(g['jump_table_candidates'])}
    if actual!=contract['expected_cfg']:raise RuntimeError(f'CFG regression: {actual}')
    report={'input_sha256':digest,'imports':len(pe['imports']),'cfg':actual,'manifest_sha256':hashlib.sha256(first).hexdigest(),
            'deterministic':True,'whole_program_complete':False,'elapsed_seconds':round(time.perf_counter()-started,3)}
    if a.conformance:
        script=Path(__file__).resolve().parents[1]/'tests/conformance.py'
        cmd=[sys.executable,str(script),'--tool',str(a.tool),'--out',str(a.out/'conformance'),'--target',str(a.exe),'--vectors','1024','--cxx',a.cxx]
        for address in contract['validated_entries']:cmd+=['--entry',hex(address)]
        subprocess.run(cmd,check=True)
        report['conformance']=json.loads((a.out/'conformance/report.json').read_text())
    (a.out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
if __name__=='__main__':main()
