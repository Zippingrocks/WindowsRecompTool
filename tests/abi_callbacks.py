"""Generated guest -> host API -> generated guest callback -> host -> guest.

These fixtures test actual compiled CALL/RET and the shared dispatch budget;
callbacks are not Python callbacks and are not simulated by the test harness.
"""
from __future__ import annotations
import argparse
import ctypes as ct
import json
from pathlib import Path
import conformance as c
from fixtures import make_pe


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--tool',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--cxx',default='c++');a=ap.parse_args()
    a.tool=a.tool.resolve();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=True)
    # CALL EAX enters a real runtime API; the API invokes the supplied guest
    # callback. The callback computes [ESP+4]+[ESP+8] and RET 8.
    pe=a.out/'callbacks.exe';pe.write_bytes(make_pe(bytes.fromhex('ffd0c38b44240403442408c20800')))
    name='guest_callbacks';c.run([str(a.tool),'lift',str(pe),str(a.out/(name+'.cpp')),'--name',name,'--seed','0x401003'])
    # Step emission, rather than nested whole-slice loops, feeds Process.callback.
    project=a.out/'project'
    if project.exists():
        import shutil
        shutil.rmtree(project)
    c.run([str(a.tool),'project',str(pe),str(project),'--seed','0x401003'])
    cpp=a.out/'callback_test.cpp'
    cpp.write_text(r'''#include <winrecomp/process.hpp>
#include <iostream>
#include <stdexcept>
#include "project/code_page_1025.cpp"
bool step(wr::Cpu& s,wr::Memory& m,std::uint64_t& b){return code_page_1025(s,m,b);}
int main(int argc,char** argv){try{
if(argc!=2)throw std::runtime_error("need fixture");auto image=wr::Image::load(argv[1]);
for(unsigned n=0;n<256;++n){wr::Process p(step);p.register_api("test.dll","invoke",0,[n](wr::Process& q){const std::array<wr::U32,2> args{n,0xffff0000u+n};return q.callback(0x401003,args);});p.load(image);
p.cpu.r[wr::EAX]=p.resolve("test.dll","invoke");p.cpu.r[wr::EBX]=0x12345678;p.cpu.r[wr::ESI]=0x87654321;p.cpu.r[wr::EDI]=0x34567890;p.cpu.r[wr::EBP]=0xabcdef;
const auto expected=0xffff0000u+n*2;if(p.run()!=expected || p.cpu.r[wr::EBX]!=0x12345678 || p.cpu.r[wr::ESI]!=0x87654321 || p.cpu.r[wr::EDI]!=0x34567890 || p.cpu.r[wr::EBP]!=0xabcdef)throw std::runtime_error("callback mismatch");}
std::cout<<"256 compiled cross-boundary callbacks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
''')
    cmake=a.out/'CMakeLists.txt';cmake.write_text('cmake_minimum_required(VERSION 3.20)\nproject(CallbackTest LANGUAGES C CXX)\nset(CMAKE_CXX_STANDARD 20)\nset(BUILD_TESTING OFF CACHE BOOL "" FORCE)\nadd_subdirectory("'+c.ROOT.as_posix()+'" winrecomp)\nadd_executable(callback_test callback_test.cpp)\ntarget_link_libraries(callback_test PRIVATE winrecomp_guest)\n')
    c.run(['cmake','-S',str(a.out),'-B',str(a.out/'build'),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release'])
    c.run(['cmake','--build',str(a.out/'build'),'--target','callback_test','--parallel','2'])
    executable=a.out/'build'/('callback_test.exe' if c.os.name=='nt' else 'callback_test');result=c.run([str(executable),str(pe)])
    print(result.stdout);(a.out/'report.json').write_text(json.dumps({'schema':'winrecomp.callback-abi.v1','vectors':256,'path':'generated guest -> real host dispatcher -> generated guest stdcall callback -> host -> generated guest','all_passed':True},indent=2)+'\n')
if __name__=='__main__':main()
