"""Verify tracked source policy and exact gitlink/lockfile consistency."""
from __future__ import annotations
import json
import hashlib
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
def git(*args):return subprocess.check_output(['git',*args],cwd=ROOT,text=True)
def main():
    entries=json.loads((ROOT/'research/upstreams.lock.json').read_text())['entries']
    tracked={}
    for line in git('ls-files','--stage').splitlines():
        meta,path=line.split('\t',1);mode,sha,stage=meta.split()
        if stage!='0':raise RuntimeError('unmerged index')
        tracked[path]=(mode,sha)
        if Path(path).suffix.lower() in {'.exe','.dll','.xbe','.xex','.zip','.pdb','.bundle'}:
            raise RuntimeError('binary/game material must not be tracked: '+path)
        if path.startswith(('local/','build/','generated/')):raise RuntimeError('private/generated path tracked: '+path)
    for e in entries:
        if tracked.get(e['path'])!=('160000',e['commit']):raise RuntimeError('gitlink does not match pin: '+e['name'])
    zydis=ROOT/'third_party/zydis'
    current=subprocess.check_output(['git','-C',str(zydis),'rev-parse','HEAD'],text=True).strip()
    if current!=entries[0]['commit']:raise RuntimeError('decoder checkout mismatch')
    for required in ['LICENSE','dependencies/zycore/LICENSE']:
        if not (zydis/required).is_file():raise RuntimeError('missing upstream license: '+required)
    source_hashes=ROOT/'verification/source-hashes.json'
    if source_hashes.exists():
        for name,expected in json.loads(source_hashes.read_text())['sha256'].items():
            actual=hashlib.sha256(subprocess.check_output(['git','show',':'+name],cwd=ROOT)).hexdigest()
            if actual!=expected:raise RuntimeError('source transfer checksum mismatch: '+name)
    print(f'source policy passed; {len(entries)} pinned upstream gitlinks; no tracked game binaries')
if __name__=='__main__':main()
