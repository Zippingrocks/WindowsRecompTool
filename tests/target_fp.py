"""Select bounded, unchanged x87 instruction sequences from an owned PE.

Only dual-mode, register-only or ESI/EDI-buffer x87 operations are admitted to
our native test oracle. This is NOT a general facility for executing input EXEs.
Generated code/corpus bytes remain in the caller's local output directory.
"""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import subprocess

ALLOWED = frozenset({'fld','fst','fstp','fadd','faddp','fmul','fmulp','fsub',
                    'fsubp','fsubr','fsubrp','fdiv','fdivp','fdivr','fdivrp',
                    'fchs','fabs','fsqrt','frndint','fxch','fld1','fldz','fldpi'})
CONSTANTS = frozenset({'fld1','fldz','fldpi'})

def next_depth(instruction: dict, depth: int) -> int | None:
    """Preflight exactly the instructions the original native oracle will run."""
    mnemonic=instruction['mnemonic']
    raw=bytes.fromhex(instruction['bytes'])
    if mnemonic not in ALLOWED or not 2<=len(raw)<=8 or instruction['size']!=len(raw) or not 0xd8<=raw[0]<=0xdf:
        return None  # No prefixes, branches, integer/system instructions or RET.
    for operand in instruction['operands']:
        if operand['kind']=='memory':
            if (operand['base'] not in ('esi','edi') or operand['index']!='none'
                or operand['segment']!='ds' or operand['address_width']!=32
                or operand['width'] not in (32,64)
                or not 0<=operand['displacement']<=128-operand['width']//8):
                return None
            if mnemonic in ('fst','fstp') and operand['base']!='edi':
                return None  # Only the destination buffer can be modified.
        elif operand['kind']=='register':
            register=operand['register']
            if register not in {f'st{n}' for n in range(depth)}:
                return None
        else:
            return None
    if mnemonic=='fld' or mnemonic in CONSTANTS:
        return depth+1 if depth<8 else None
    if not depth:
        return None
    return depth-1 if mnemonic.endswith('p') else depth

def select(instructions: list[dict], limit: int=24) -> list[dict]:
    """Take long, non-overlapping sequences with a valid initially empty stack."""
    if not 1<=limit<=64:raise ValueError('Target corpus limit must be between 1 and 64')
    by_address={i['address']:i for i in instructions}
    candidates=[]
    for instruction in instructions:
        if instruction['mnemonic']!='fld' or not instruction['operands'] or instruction['operands'][0]['kind']!='memory':
            continue
        sequence=[];current=instruction;depth=0
        while current and len(sequence)<64:
            after=next_depth(current,depth)
            if after is None:break
            sequence.append(current);depth=after
            current=by_address.get(current['address']+current['size'])
        if len(sequence)>=3:candidates.append(sequence)
    selected=[];covered=set();seen=set()
    for sequence in sorted(candidates,key=lambda s:(-len(s),s[0]['address'])):
        addresses={i['address'] for i in sequence}
        code=''.join(i['bytes'] for i in sequence)
        if addresses & covered or code in seen:continue
        source=sequence[0]['address'];widths={o['width'] for i in sequence for o in i['operands'] if o['kind']=='memory'}
        selected.append({'name':f'target_{source:08x}','code':code+'c3',
                         'format':'target_f64' if widths=={64} else 'target_f32',
                         'environment':False,'source_address':source,
                         'source_instruction_count':len(sequence),
                         'source_bytes_sha256':hashlib.sha256(bytes.fromhex(code)).hexdigest()})
        covered.update(addresses);seen.add(code)
        if len(selected)>=limit:break
    return selected

def load(tool: Path, exe: Path, contract: Path) -> tuple[list[dict], dict]:
    identity=json.loads(contract.read_text())
    digest=hashlib.sha256(exe.read_bytes()).hexdigest()
    if digest!=identity['sha256']:
        raise RuntimeError('FP corpus input SHA-256 does not match target contract')
    graph=json.loads(subprocess.run([str(tool),'cfg',str(exe)],capture_output=True,check=True,text=True).stdout)
    if graph['diagnostics'] or graph['budget_exhausted']:
        raise RuntimeError('FP corpus requires a diagnostic-free bounded decode')
    items=select(graph['instructions'])
    if len(items)<8:raise RuntimeError('Too few safe target x87 sequences to validate')
    metadata={'input_sha256':digest,'sequences':[{k:v for k,v in item.items() if k not in ('code','format','environment')} for item in items],
              'selection':'24 longest non-overlapping distinct safe x87 sequences, at most 64 instructions each; contiguous original bytes, harness-added RET',
              'scope':'Controlled ESI/EDI buffers and initially empty x87 stack; not whole original functions or full gameplay'}
    return items,metadata
