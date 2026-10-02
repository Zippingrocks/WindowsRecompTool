"""Validate and materialize this one immutable owned-source snapshot."""
import hashlib
import json
import lzma
from pathlib import Path, PurePosixPath
import shutil

root = Path.cwd().resolve()
meta = json.loads((root / '.runtime-bootstrap/publish.json').read_text())
packed = b''.join((root / f'.runtime-bootstrap/part-{i:02d}.xz').read_bytes() for i in range(16))
if len(packed) != 76316 or hashlib.sha256(packed).hexdigest() != '99a1749eff2a1c4b9234403a2010e382519278eae95ff9c20ff80ae548cd6970':
    raise RuntimeError('Compressed source integrity failure')
decoder = lzma.LZMADecompressor()
raw = decoder.decompress(packed, max_length=2 * 1024 * 1024)
if not decoder.eof or decoder.unused_data or len(raw) != 412841 or hashlib.sha256(raw).hexdigest() != '12ef40c7e2fa5ec8d7ba3ada1de896f3d7be659b39a247e4fdbb822008063088':
    raise RuntimeError('Uncompressed source integrity failure')
snapshot = json.loads(raw)
if snapshot['schema'] != 'winrecomp.owned-source-transfer.v1' or snapshot['base_commit'] != meta['base_commit']:
    raise RuntimeError('Unexpected source identity')
if len(snapshot['files']) != 46:
    raise RuntimeError('Unexpected source file count')
validated = []
seen = set()
for entry in snapshot['files']:
    name = entry['path']
    rel = PurePosixPath(name)
    if rel.is_absolute() or '..' in rel.parts or '\\' in name or ':' in name or name in seen:
        raise RuntimeError('Unsafe or duplicate source path')
    if name not in {'CMakeLists.txt', 'README.md', '.github/workflows/ci.yml'}:
        if rel.parts[0] not in {'docs', 'examples', 'include', 'src', 'tests', 'tools', 'verification'} or rel.suffix not in {'.cpp', '.hpp', '.py', '.json', '.md', '.txt'}:
            raise RuntimeError('Source path outside whitelist: ' + name)
    target = root / name
    if any(p.is_symlink() for p in [target, *target.parents] if p != root.parent):
        raise RuntimeError('Symlink source destination')
    data = entry['content'].encode('utf-8')
    if hashlib.sha256(data).hexdigest() != entry['sha256']:
        raise RuntimeError('Per-file integrity failure: ' + name)
    if name == '.github/workflows/ci.yml' and target.read_bytes() != data:
        raise RuntimeError('Workflow must already match the reviewed source')
    seen.add(name)
    validated.append((target, data))
for target, data in validated:
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
shutil.rmtree(root / '.runtime-bootstrap')
print('Validated and materialized all 46 owned-source files; game material is not in this snapshot.')
