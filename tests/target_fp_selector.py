"""Synthetic tests of the private-target oracle's conservative admission policy."""
import copy
import unittest
from target_fp import next_depth, select


def mem(base='esi', displacement=0, width=32, index='none', segment='ds', address_width=32):
    return {'kind':'memory','base':base,'index':index,'segment':segment,
            'address_width':address_width,'width':width,'displacement':displacement}

def ins(mnemonic='fld', data='d906', operands=None, address=0x401000):
    return {'mnemonic':mnemonic,'bytes':data,'size':len(bytes.fromhex(data)),
            'operands':[mem()] if operands is None else operands,'address':address}


def sequence(start=0x401000):
    return [ins(address=start),ins('fmul','d84e04',[mem(displacement=4)],start+2),
            ins('fstp','d91f',[mem(base='edi')],start+5)]


class Admission(unittest.TestCase):
    def test_controlled_sequence(self):
        selected=select(sequence())
        self.assertEqual(len(selected),1)
        self.assertEqual(selected[0]['code'],'d906d84e04d91fc3')
        self.assertEqual(selected[0]['source_instruction_count'],3)

    def test_no_prefixes_control_or_environment(self):
        for mnemonic,data in [('fld','64d906'),('fld','67d906'),('fld','48d906'),
                              ('fnstenv','d937'),('frstor','dd27'),('ret','c3'),
                              ('syscall','0f05'),('fld','d9')]:
            with self.subTest(mnemonic=mnemonic,data=data):
                self.assertIsNone(next_depth(ins(mnemonic,data),1))

    def test_bounds_and_segment_addressing(self):
        for operand in [mem(base='esp'),mem(base='ebp'),mem(base='none'),
                        mem(index='eax'),mem(segment='fs'),mem(address_width=16),
                        mem(width=80),mem(displacement=-1),mem(displacement=125),
                        mem(displacement=121,width=64)]:
            with self.subTest(operand=operand):
                self.assertIsNone(next_depth(ins(operands=[operand]),0))
        self.assertEqual(next_depth(ins(operands=[mem(displacement=124)]),0),1)
        self.assertEqual(next_depth(ins(operands=[mem(displacement=120,width=64)]),0),1)

    def test_source_is_not_writable(self):
        self.assertIsNone(next_depth(ins('fstp','d91e',[mem()]),1))
        self.assertEqual(next_depth(ins('fstp','d91f',[mem(base='edi')]),1),0)

    def test_stack_underflow_overflow_and_aliases(self):
        self.assertIsNone(next_depth(ins('fabs','d9e1',[]),0))
        self.assertIsNone(next_depth(ins(),8))
        self.assertIsNone(next_depth(ins('fxch','d9c9',[{'kind':'register','register':'st1'}]),1))
        self.assertEqual(next_depth(ins('fxch','d9c9',[{'kind':'register','register':'st1'}]),2),2)
        self.assertIsNone(next_depth(ins('fld','d9c0',[{'kind':'register','register':'st0'}]),0))
        self.assertIsNone(next_depth(ins(operands=[{'kind':'immediate'}]),0))

    def test_gaps_duplicates_and_overlap(self):
        gapped=sequence();gapped[-1]['address']+=1
        self.assertEqual(select(gapped),[])
        self.assertEqual(len(select(sequence()+sequence(0x402000))),1)
        overlapping=[ins(),ins(address=0x401002),ins('faddp','dec1',
                     [{'kind':'register','register':'st1'},{'kind':'register','register':'st0'}],0x401004),
                     ins('fstp','d91f',[mem(base='edi')],0x401006)]
        chosen=select(overlapping)
        self.assertEqual(len(chosen),1)
        self.assertEqual(chosen[0]['source_instruction_count'],4)

    def test_size_metadata_is_consistent(self):
        malformed=ins();malformed['size']=8
        self.assertIsNone(next_depth(malformed,0))

    def test_bounded_length(self):
        ops=[ins()]+[ins('fabs','d9e1',[],0x401002+n*2) for n in range(80)]
        self.assertEqual(select(ops)[0]['source_instruction_count'],64)
        for limit in [0,-1,65]:
            with self.assertRaises(ValueError):select(ops,limit=limit)


if __name__=='__main__':unittest.main(verbosity=2)
