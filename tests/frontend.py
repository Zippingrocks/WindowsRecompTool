from __future__ import annotations
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest
from fixtures import make_pe, with_imports, put16, put32

EXE = pathlib.Path(sys.argv.pop(1)).resolve()
class Frontend(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.path=pathlib.Path(self.temp.name)
    def tearDown(self):
        self.temp.cleanup()
    def invoke(self, command, data, *args, expected=0):
        p=self.path/'input.exe';p.write_bytes(data)
        result=subprocess.run([str(EXE),command,str(p),*map(str,args)],capture_output=True,text=True,timeout=20)
        self.assertEqual(result.returncode,expected,result.stderr)
        return result
    def cfg(self,code,*args):
        return json.loads(self.invoke('cfg',make_pe(bytes.fromhex(code)),*args).stdout)
    def test_basic_pe(self):
        j=json.loads(self.invoke('analyze',make_pe(b'\xc3')).stdout)
        self.assertEqual((j['image_base'],j['entry'],len(j['sections'])),(0x400000,0x401000,2))
    def test_reject_non_pe(self):
        self.invoke('analyze',b'no',expected=1)
    def test_reject_wrong_machine(self):
        b=make_pe(b'\xc3');put16(b,0x84,0x8664);self.invoke('analyze',b,expected=1)
    def test_reject_pe32_plus(self):
        b=make_pe(b'\xc3');put16(b,0x98,0x20b);self.invoke('analyze',b,expected=1)
    def test_pe_pointer_overflow(self):
        b=make_pe(b'\xc3');put32(b,0x3c,0xffffffff);self.invoke('analyze',b,expected=1)
    def test_short_optional_header(self):
        b=make_pe(b'\xc3');put16(b,0x94,50);self.invoke('analyze',b,expected=1)
    def test_directory_count_overflow(self):
        b=make_pe(b'\xc3');put32(b,0x98+92,0xffffffff);self.invoke('analyze',b,expected=1)
    def test_virtual_wrap(self):
        b=make_pe(b'\xc3',base=0xfffff000);self.invoke('analyze',b,expected=1)
    def test_raw_overflow(self):
        b=make_pe(b'\xc3');put32(b,0x178+20,0xfffffff0);self.invoke('analyze',b,expected=1)
    def test_virtual_overlap(self):
        b=make_pe(b'\xc3');put32(b,0x178+40+12,0x1000);self.invoke('analyze',b,expected=1)
    def test_raw_overlap(self):
        b=make_pe(b'\xc3');put32(b,0x178+40+20,0x200);self.invoke('analyze',b,expected=1)
    def test_zero_fill(self):
        out=self.path/'mapped.bin';self.invoke('dump-image',make_pe(b'\xc3'),out)
        b=out.read_bytes();self.assertEqual(b[0x2200:0x2300],bytes(0x100));self.assertEqual(b[0x1000],0xc3)
    def test_name_and_ordinal_imports(self):
        j=json.loads(self.invoke('analyze',with_imports(b'\xc3')).stdout)
        self.assertEqual([(i['name'],i['ordinal']) for i in j['imports']],[('Thing',None),('#42',42)])
    def test_original_thunk_fallback(self):
        j=json.loads(self.invoke('analyze',with_imports(b'\xc3',True)).stdout)
        self.assertEqual(len(j['imports']),2)
    def test_import_termination(self):
        b=with_imports(b'\xc3');put32(b,0x98+100+8,20);self.invoke('analyze',b,expected=1)
    def test_back_edge_splits_existing_block(self):
        j=self.cfg('31c04083f80575fac3')
        self.assertEqual([b['start'] for b in j['blocks']],[0x401000,0x401002,0x401008])
        self.assertEqual(len(j['instructions']),5)
    def test_direct_call_seed(self):
        j=self.cfg('e801000000c3c3');self.assertIn(0x401006,j['function_candidates'])
    def test_import_is_external_not_guessed_code(self):
        j=json.loads(self.invoke('cfg',with_imports(bytes.fromhex('ff15a0204000c3'))).stdout)
        self.assertTrue(any(e['kind']=='call_import' and e['detail']=='fake.dll!Thing' for e in j['edges']))
    def test_indirect_is_unresolved(self):
        j=self.cfg('ffe0');self.assertEqual(j['edges'][0]['kind'],'jump_indirect');self.assertIsNone(j['edges'][0]['to'])
    def test_jump_table_is_only_a_candidate(self):
        j=self.cfg('ff248580204000');self.assertEqual(j['jump_table_candidates'],[0x401000]);self.assertEqual(len(j['instructions']),1)
    def test_overlap_is_diagnostic(self):
        j=self.cfg('7401b878563412c3');self.assertTrue(any('overlap' in x for x in j['diagnostics']))
    def test_decode_stops_at_raw_boundary(self):
        b=make_pe(b'\x0f');put32(b,0x178+16,1)
        j=json.loads(self.invoke('cfg',b).stdout);self.assertTrue(any('decode failure' in x for x in j['diagnostics']))
    def test_budget_is_not_success(self):
        j=json.loads(self.invoke('cfg',make_pe(bytes.fromhex('9090c3')),'--max-instructions','1',expected=3).stdout)
        self.assertTrue(j['budget_exhausted'])
    def test_non_code_seed_rejected(self):
        self.invoke('cfg',make_pe(b'\xc3'),'--entry','0x402000',expected=1)
    def test_deterministic_manifest(self):
        b=make_pe(bytes.fromhex('31c04083f80575fac3'))
        self.assertEqual(self.invoke('cfg',b).stdout,self.invoke('cfg',b).stdout)
    def test_unsupported_does_not_clobber_output(self):
        out=self.path/'output.cpp';out.write_text('KEEP')
        self.invoke('lift',make_pe(bytes.fromhex('0f0b')),out,expected=1);self.assertEqual(out.read_text(),'KEEP')
    def test_fs_memory_requires_runtime_segment(self):
        out=self.path/'output.cpp'
        self.invoke('lift',make_pe(bytes.fromhex('64a100000000c3')),out)
        self.assertIn('segment_address',out.read_text())
    def test_integer_lift(self):
        out=self.path/'output.cpp';self.invoke('lift',make_pe(bytes.fromhex('8b44240403442408c3')),out)
        self.assertIn('wr::add',out.read_text());self.assertNotIn('decode',out.read_text())
    def test_invalid_identifier(self):
        self.invoke('lift',make_pe(b'\xc3'),self.path/'output.cpp','--name','a;system',expected=1)
    def test_cpp_keyword_rejected(self):
        self.invoke('lift',make_pe(b'\xc3'),self.path/'output.cpp','--name','while',expected=1)
    def test_input_not_overwritten(self):
        b=make_pe(b'\xc3');p=self.path/'input.exe'
        self.invoke('lift',b,p,expected=1);self.assertEqual(p.read_bytes(),b)
    def test_exports_and_tls_seeds(self):
        b=make_pe(bytes.fromhex('c3c3c3'))
        # Export directory, one function RVA, and a TLS callback VA array.
        put32(b,0x98+96,0x2000);put32(b,0x98+100,0x40)
        put32(b,0x400+20,1);put32(b,0x400+28,0x2040);put32(b,0x440,0x1001)
        put32(b,0x98+96+9*8,0x2060);put32(b,0x98+100+9*8,24)
        put32(b,0x460+12,0x402080);put32(b,0x480,0x401002)
        j=json.loads(self.invoke('cfg',b).stdout);self.assertEqual(j['roots'],[0x401000,0x401001,0x401002])

if __name__=='__main__': unittest.main(verbosity=2)
