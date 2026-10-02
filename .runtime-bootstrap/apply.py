"""Apply a source-only, SHA-bound patch; reject drift and mismatched bytes."""
from pathlib import Path
import hashlib
import json
import subprocess
root=Path(__file__).resolve().parents[1]
folder=root/'.runtime-bootstrap'
spec=json.loads((folder/'publish.json').read_text())
patch=folder/'update.patch'
if hashlib.sha256(patch.read_bytes()).hexdigest()!=spec['patch_sha256']:
    raise RuntimeError('source patch checksum mismatch')
subprocess.run(['git','diff','--exit-code',spec['base'],'HEAD','--',*spec['sha256']],cwd=root,check=True)
subprocess.run(['git','apply','--check',str(patch)],cwd=root,check=True)
subprocess.run(['git','apply',str(patch)],cwd=root,check=True)
for name,expected in spec['sha256'].items():
    if hashlib.sha256((root/name).read_bytes()).hexdigest()!=expected:
        raise RuntimeError('published source differs from tested source: '+name)
manifest=root/'verification/source-hashes.json'
data=json.loads(manifest.read_text())
data['sha256'].update(spec['sha256'])
for name in data['sha256']:
    data['sha256'][name]=hashlib.sha256((root/name).read_bytes()).hexdigest()
manifest.write_text(json.dumps(data,indent=2)+'\n')
for name in ['publish.json','update.patch','apply.py']:(folder/name).unlink()
folder.rmdir()
print('Exact FP corpus/selector and native-crash diagnostics verified; no workflow changes.')
