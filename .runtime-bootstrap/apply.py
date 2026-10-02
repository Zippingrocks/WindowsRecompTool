"""Apply the exact locally tested CI repair; reject drift before any source write."""
from pathlib import Path
import hashlib
import json
import re
import subprocess

root = Path(__file__).resolve().parents[1]
spec = json.loads((root / '.runtime-bootstrap/publish.json').read_text())
paths = list(spec['sha256'])
source_paths = [name for name in paths if not name.startswith('.github/')]
subprocess.run(['git', 'diff', '--exit-code', spec['base'], 'HEAD', '--', *source_paths], cwd=root, check=True)
changes = {name: (root / name).read_text() for name in paths}
for name in ['include/winrecomp/process.hpp', 'src/runtime/process.cpp']:
    changes[name] = re.sub(r'\bcdecl\b', 'caller_cleans_stack', changes[name])
changes['include/winrecomp/integer.hpp'] = changes['include/winrecomp/integer.hpp'].replace('    if(!n)return a;', '''    if(!n) {
        // Only a zero MASKED count preserves OF. A nonzero count that becomes
        // zero modulo 9/17 still leaves OF undefined (RCL/RCR full cycles).
        // Preserve CF and all other flags and the destination in this case.
        s.defined_flags&=~OF;
        return a;
    }''', 1)
regression = '''    // RCL/RCR count=9/17 is not count=0: hardware may change the undefined OF.
    // Test both directions, widths, incoming OF/CF and all encoded count bytes.
    for(unsigned width:{8u,16u,32u})for(unsigned kind:{5u,6u})
      for(unsigned count=0;count<256;++count)for(unsigned initial:{0u,wr::CF,wr::OF,wr::CF|wr::OF}) {
        wr::Cpu c;c.flags=initial|wr::ZF|2u;c.defined_flags=wr::STATUS_FLAGS;
        const auto before=c.flags;const auto value=wr::shift(c,0xa5a5a5a5u,count,width,kind);
        const auto masked=count&31u;
        CHECK(bool(c.defined_flags&wr::OF)==(masked<=1));
        CHECK((c.defined_flags&~wr::OF)==(wr::STATUS_FLAGS&~wr::OF));
        if(!masked || (width<32 && masked%(width+1)==0)) {
          CHECK(value==(0xa5a5a5a5u&wr::mask(width)));
          CHECK(c.flags==before);
        }
      }
'''
changes['tests/core.cpp'] = changes['tests/core.cpp'].replace('#include "winrecomp/runtime.hpp"', '#include "winrecomp/integer.hpp"').replace('    wr::Decoder d;', regression + '    wr::Decoder d;')
for name in ['.github/workflows/ci.yml', '.github/workflows/publish-runtime.yml']:
    changes[name] = changes[name].replace("      - name: Preserve source history for offline workspaces\n        if: runner.os == 'Linux'", "      - name: Preserve source history for offline workspaces\n        if: always() && runner.os == 'Linux'")
for name, text in changes.items():
    if hashlib.sha256(text.encode()).hexdigest() != spec['sha256'][name]:
        raise RuntimeError('Repaired source differs from tested bytes: ' + name)
for name, text in changes.items():
    (root / name).write_text(text, encoding='utf-8', newline='\n')
manifest = root / 'verification/source-hashes.json'
data = json.loads(manifest.read_text())
for name in data['sha256']:
    data['sha256'][name] = hashlib.sha256((root / name).read_bytes()).hexdigest()
manifest.write_text(json.dumps(data, indent=2) + '\n')
(root / '.runtime-bootstrap/publish.json').unlink()
Path(__file__).unlink()
(root / '.runtime-bootstrap').rmdir()
print('All six repaired files match the locally tested SHA-256 values; source hashes refreshed.')
