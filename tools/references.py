"""Fetch explicitly requested pinned references; never silently update their SHAs."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('names',nargs='*')
    ap.add_argument('--all',action='store_true')
    ap.add_argument('--verify',action='store_true')
    args=ap.parse_args()
    entries=json.loads((ROOT/'research/upstreams.lock.json').read_text())['entries']
    names={x['name'] for x in entries}
    unknown=set(args.names)-names
    if unknown:ap.error('unknown references: '+', '.join(sorted(unknown)))
    wanted=names if args.all else set(args.names or ['zydis'])
    for e in entries:
        if e['name'] not in wanted:continue
        if not args.verify:
            subprocess.run(['git','submodule','update','--init','--recursive','--',e['path']],cwd=ROOT,check=True)
        p=ROOT/e['path']
        result=subprocess.run(['git','-C',str(p),'rev-parse','HEAD'],capture_output=True,text=True,check=True)
        if result.stdout.strip()!=e['commit']:raise RuntimeError('pin mismatch: '+e['name'])
        print(e['name'],e['commit'],e['role'])
if __name__=='__main__':main()
