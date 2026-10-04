"""Compile authored DLL-fallback and CPUID x86 into a native test program.

The host's optional CPU instructions or a real DLL are never used as the guest
implementation. Compare explicit scalar-v1/namespace contracts, not host CPUID.
Optional --toolchain/--runner runs Windows-target code on a labeled test host.
"""
import argparse
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
from program import imports
from fixtures import put32

ROOT=Path(__file__).resolve().parents[1]

def fixture():
    names=['LoadLibraryA','GetLastError','GetProcAddress','FreeLibrary','IsProcessorFeaturePresent']
    code=bytearray();failures=[]
    def e(s):code.extend(bytes.fromhex(s))
    def p(n):e('68');code.extend(struct.pack('<I',n))
    def call(n):e('ff15');code.extend(struct.pack('<I',0x402180+names.index(n)*4))
    def fail(cc='85'):e('0f'+cc);failures.append(len(code));code.extend(bytes(4))
    p(0x402900);call('LoadLibraryA');e('85c0');fail()
    call('GetLastError');e('3d7e000000');fail()
    p(0x402980);call('LoadLibraryA');e('85c0');fail('84');e('89c3')
    p(0x402980);call('LoadLibraryA');e('39d8');fail()
    p(0x4029c0);e('53');call('GetProcAddress');e('85c0');fail('84');e('89c5')
    e('53');call('FreeLibrary');e('83f801');fail()
    e('ffd5');e('3d78563412');fail()
    e('53');call('FreeLibrary');e('83f801');fail()
    # Preserve/read flags and toggle ID just as a normal CPUID capability probe.
    e('9c5889c23500002000509d9c5831d0');e('2500002000');e('3d00002000');fail()
    e('31c00fa2');e('83f801');fail();e('81fb57696e52');fail()
    e('81fa65636f6d');fail();e('81f970435055');fail()
    e('b8010000000fa2');e('81fa01800000');fail();e('81f900000080');fail()
    p(6);call('IsProcessorFeaturePresent');e('85c0');fail()
    e('31c0c3');end=len(code);e('b863000000c3')
    for at in failures:struct.pack_into('<i',code,at,end-at-4)
    raw=imports(code,names);offset=struct.unpack_from('<I',raw,0x98+224+40+20)[0]
    for pos,text in [(0x900,b'WR_Absent_C29F117DC7014b4c87e05d2faac7fd8d.dll\0'),(0x980,b'wrfixtures.dll\0'),(0x9c0,b'Probe\0')]:raw[offset+pos:offset+pos+len(text)]=text
    return raw

def run(cmd,**kw):
    result=subprocess.run([str(x) for x in cmd],capture_output=True,text=True,timeout=180,**kw)
    if result.returncode:raise RuntimeError(f'{cmd}\n{result.stdout}\n{result.stderr}')
    return result

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--tool',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--toolchain',type=Path);ap.add_argument('--runner',type=Path);args=ap.parse_args()
    out=args.out.resolve();out.mkdir(parents=True,exist_ok=True);pe=out/'fixture.exe';pe.write_bytes(fixture())
    project=out/'project';shutil.rmtree(project,ignore_errors=True)
    run([args.tool.resolve(),'project',pe,project])
    (out/'check.cpp').write_text(r'''#include <winrecomp/process.hpp>
#include <iostream>
#include <fstream>
bool compiled_step(wr::Cpu&,wr::Memory&,std::uint64_t&);
int main(int argc,char** argv){try{
if(argc!=3)throw std::runtime_error("need fixture and empty directory");auto image=wr::Image::load(argv[1]);
for(unsigned n=0;n<64;++n){wr::ProcessOptions o;o.dll_search_roots={argv[2]};o.processor_profile=wr::ProcessorProfile::scalar_v1;
wr::Process p(compiled_step,o);unsigned calls=0;p.register_api("wrfixtures.dll","Probe",0,[&](auto&)->wr::U32{++calls;return 0x12345678;});p.load(image);
if(p.run()!=0 || calls!=1 || p.module("wrfixtures.dll")!=0 || p.module_probes.front().outcome!="absent_from_configured_namespace")throw std::runtime_error("compiled module/CPUID mismatch");}
// An unspecified CPU does not silently select the scalar feature profile.
{wr::ProcessOptions o;o.dll_search_roots={argv[2]};wr::Process p(compiled_step,o);p.register_api("wrfixtures.dll","Probe",0,[](auto&)->wr::U32{return 0x12345678;});p.load(image);bool stopped=false;try{p.run();}catch(const wr::GuestFault& e){stopped=e.kind==wr::FaultKind::unsupported && std::string(e.what()).find("CPUID requires explicit")!=std::string::npos;}if(!stopped)throw std::runtime_error("implicit CPU feature profile");}
// Unconfigured namespace must stop, never choose the missing-library branch.
{wr::Process p(compiled_step);p.load(image);bool stopped=false;try{p.run();}catch(const wr::GuestFault& e){stopped=e.kind==wr::FaultKind::unsupported;}if(!stopped || p.module_probes.back().outcome!="namespace_not_configured")throw std::runtime_error("namespace rejection missing");}
// Existing unknown DLL is not converted into a missing DLL merely to run further.
auto path=std::filesystem::path(argv[2])/"WR_Absent_C29F117DC7014b4c87e05d2faac7fd8d.dll";
if(std::filesystem::exists(path))throw std::runtime_error("test directory must start empty");
std::ofstream(path)<<"author-written non-executable candidate";
{wr::ProcessOptions o;o.dll_search_roots={argv[2]};wr::Process p(compiled_step,o);p.load(image);bool stopped=false;try{p.run();}catch(const wr::GuestFault& e){stopped=e.kind==wr::FaultKind::unsupported;}if(!stopped || p.module_probes.back().outcome!="present_but_untranslated")throw std::runtime_error("present DLL rejection missing");}
std::filesystem::remove(path);
std::cout<<"64 compiled DLL-fallback/handle/CPUID scenarios and 3 negative scenarios passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
''')
    (out/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(ModuleExecution LANGUAGES C CXX)\nset(CMAKE_CXX_STANDARD 20)\nset(BUILD_TESTING OFF CACHE BOOL "" FORCE)\nadd_subdirectory("'+ROOT.as_posix()+'" winrecomp)\nfile(GLOB GENERATED "${CMAKE_CURRENT_SOURCE_DIR}/project/code_page_*.cpp")\nadd_executable(module_execution check.cpp project/dispatch.cpp ${GENERATED})\ntarget_link_libraries(module_execution PRIVATE winrecomp_guest)\n')
    command=['cmake','-S',out,'-B',out/'build','-G','Ninja','-DCMAKE_BUILD_TYPE=Release']
    if args.toolchain:command += ['-DCMAKE_TOOLCHAIN_FILE='+str(args.toolchain.resolve()),'-DCMAKE_EXE_LINKER_FLAGS=-static -static-libgcc -static-libstdc++','-DCMAKE_CXX_FLAGS_RELEASE=-O1 -DNDEBUG']
    run(command);run(['cmake','--build',out/'build','--target','module_execution','--parallel','3'])
    root=out/'dll-root';root.mkdir(exist_ok=True)
    exe=out/'build'/('module_execution.exe' if args.runner or sys.platform=='win32' else 'module_execution')
    if args.runner:
        def winpath(p):return 'Z:'+str(p.resolve()).replace('/','\\')
        result=run([args.runner.resolve(),exe,winpath(pe),winpath(root)])
    else:result=run([exe,pe,root])
    print(result.stdout)
    report={'schema':'winrecomp.modules-execution.v1','passed':True,'positive_cases':64,'negative_cases':3,'host':'Wine diagnostic' if args.runner else sys.platform,'compiled_guest':True,'cpu_profile':'scalar-v1','namespace':'explicit roots','original_os_comparison':False}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':main()
