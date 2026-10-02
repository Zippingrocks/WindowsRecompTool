"""Verified dispatch and bounded jump-table recovery; no game data."""
import argparse
import json
from pathlib import Path
import struct
import conformance as c
from fixtures import make_pe

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--tool',required=True,type=Path);ap.add_argument('--out',required=True,type=Path);ap.add_argument('--cxx',default='c++');a=ap.parse_args()
    a.tool=a.tool.resolve();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=True)
    code=bytes.fromhex('83f8027719ff248500204000b811111111c3b822222222c3b833333333c3b844444444c3')
    data=struct.pack('<III',0x40100c,0x401012,0x401018)
    table=make_pe(code,data)
    # Guest function address is supplied explicitly, never guessed from data.
    callback=make_pe(bytes.fromhex('b80b104000ffd040c39090b82a000000c3'))
    call_esp=make_pe(bytes.fromhex('680c104000ff142483c404c3b82a000000c3'))
    cases=[('guarded_table',table,[]),('callback',callback,['--seed','0x40100b']),('call_esp',call_esp,['--seed','0x40100c'])]
    # MOV ECX preserves the compared EAX and flags between CMP and JA.
    preserving=bytes.fromhex('83f802b9785634127719ff248500204000b811111111c3b822222222c3b833333333c3b844444444c3')
    preserving_table=make_pe(preserving,struct.pack('<III',0x401011,0x401017,0x40101d))
    cases.append(('preserving_guard',preserving_table,[]))
    names=[];images=[];negative_tests=2

    for k,(label,pe,args) in enumerate(cases):
        name=f'flow_{k}';p=a.out/(name+'.exe');p.write_bytes(pe)
        cfg=json.loads(c.run([str(a.tool),'cfg',str(p),*args]).stdout)
        if label=='guarded_table':
            assert len(cfg['recovered_tables'])==1,cfg
            assert cfg['recovered_tables'][0]['targets']==[0x40100c,0x401012,0x401018]
            bypass=json.loads(c.run([str(a.tool),'cfg',str(p),'--seed','0x401005']).stdout)
            assert not bypass['recovered_tables'],'guard bypass should prevent recovery'
            invalid=bytearray(pe);struct.pack_into('<I',invalid,0x400,0x402000)
            q=a.out/'bad.exe';q.write_bytes(invalid)
            bad=json.loads(c.run([str(a.tool),'cfg',str(q)]).stdout)
            assert not bad['recovered_tables'],'data target must not be admitted'
        if label=='preserving_guard':
            assert len(cfg['recovered_tables'])==1
            for point in (0x401003,0x401008,0x40100a):
                rejected=json.loads(c.run([str(a.tool),'cfg',str(p),'--seed',hex(point)]).stdout)
                assert not rejected['recovered_tables'],'intermediate guard bypass must be rejected'
                negative_tests+=1
            for replacement in [bytes.fromhex('b878563412'),bytes.fromhex('b478909090'),bytes.fromhex('4190909090')]:
                mutated=bytearray(preserving);mutated[3:8]=replacement
                q=a.out/'mutated.exe';q.write_bytes(make_pe(mutated,struct.pack('<III',0x401011,0x401017,0x40101d)))
                rejected=json.loads(c.run([str(a.tool),'cfg',str(q)]).stdout)
                assert not rejected['recovered_tables'],'index/subregister/flag write invalidates proof'
                negative_tests+=1
            # x87 stack updates do not overwrite integer condition flags.
            fp=bytearray(preserving);fp[3:8]=bytes.fromhex('d9e8909090')
            q=a.out/'fp-guard.exe';q.write_bytes(make_pe(fp,struct.pack('<III',0x401011,0x401017,0x40101d)))
            assert len(json.loads(c.run([str(a.tool),'cfg',str(q)]).stdout)['recovered_tables'])==1
        c.run([str(a.tool),'lift',str(p),str(a.out/(name+'.cpp')),'--name',name,*args])
        mapped=a.out/(name+'.bin');c.run([str(a.tool),'dump-image',str(p),str(mapped)])
        names.append(name);images.append((label,mapped.read_bytes()))
    dll=c.compile_library(a.out,names,a.cxx)
    results=[c.check_case(dll,k,label,image,0x400000,0x401000,256) for k,(label,image) in enumerate(images)]
    result={'schema':'winrecomp.controlflow.v1','cases':results,'total_vectors':len(cases)*256,'negative_table_tests':negative_tests}
    (a.out/'report.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
