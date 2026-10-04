"""Measure native display enumeration across PE metadata, not game behavior.

Run in a Visual Studio x86 or x64 developer environment on Windows. It builds
only the author-written program below, never changes display settings, and
never changes compatibility settings on any existing executable.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

SOURCE = r'''
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define DIRECTDRAW_VERSION 0x0700
#include <windows.h>
#include <ddraw.h>
#include <array>
#include <cstdint>
#include <iostream>
#include <vector>
struct State { std::vector<std::array<DWORD,9>> rows; bool cancel{}, failed{}; };
HRESULT CALLBACK modes(DDSURFACEDESC* d,void* ptr) noexcept {
    auto& s=*static_cast<State*>(ptr);
    try {
        if(s.failed)return DDENUMRET_CANCEL;
        if(!d || d->dwSize!=sizeof(*d) || !(d->dwFlags&DDSD_PIXELFORMAT) || s.rows.size()>=4096){s.failed=true;return DDENUMRET_CANCEL;}
        const auto& f=d->ddpfPixelFormat;
        s.rows.push_back({d->dwSize,d->dwFlags,d->dwWidth,d->dwHeight,f.dwRGBBitCount,
                         f.dwRBitMask,f.dwGBitMask,f.dwBBitMask,f.dwFlags});
        return s.cancel?DDENUMRET_CANCEL:DDENUMRET_OK;
    }catch(...){s.failed=true;return DDENUMRET_CANCEL;}
}
void print(State& s){
    std::cout<<"{\"failed\":"<<(s.failed?"true":"false")<<",\"rows\":[";
    bool first=true;for(auto& row:s.rows){if(!first)std::cout<<',';first=false;std::cout<<'[';
        for(unsigned i=0;i<row.size();++i){if(i)std::cout<<',';std::cout<<row[i];}std::cout<<']';}
    std::cout<<"]}";
}
int main(){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    OSVERSIONINFOA v{};v.dwOSVersionInfoSize=sizeof(v);GetVersionExA(&v);
    std::cout<<"{\"pointer_bits\":"<<8*sizeof(void*)<<",\"get_version\":["<<v.dwMajorVersion<<','<<v.dwMinorVersion<<"],\"display_settings\":[";
    bool first=true;for(DWORD i=0;i<4096;++i){DEVMODEA m{};m.dmSize=sizeof(m);if(!EnumDisplaySettingsA(nullptr,i,&m))break;
        if(!first)std::cout<<',';first=false;std::cout<<'['<<m.dmPelsWidth<<','<<m.dmPelsHeight<<','<<m.dmBitsPerPel<<','<<m.dmDisplayFrequency<<']';}
    std::cout<<"],\"directdraw\":[";first=true;
    for(unsigned factory:{0u,2u}){
        if(!first)std::cout<<',';first=false;
        IDirectDraw* d=nullptr;auto hr=DirectDrawCreate(reinterpret_cast<GUID*>(static_cast<std::uintptr_t>(factory)),&d,nullptr);
        std::cout<<"{\"factory\":"<<factory<<",\"create_hresult\":"<<DWORD(hr);
        if(SUCCEEDED(hr)){
            State all,cancel;cancel.cancel=true;
            auto h1=d->EnumDisplayModes(0,nullptr,&all,modes),h2=d->EnumDisplayModes(0,nullptr,&cancel,modes);
            std::cout<<",\"continue_hresult\":"<<DWORD(h1)<<",\"cancel_hresult\":"<<DWORD(h2)<<",\"continue\":";print(all);
            std::cout<<",\"cancel\":";print(cancel);d->Release();
        }
        std::cout<<'}';
    }
    std::cout<<"]}\n";return 0;
}
'''


def headers(exe: Path):
    data=exe.read_bytes();pe=struct.unpack_from('<I',data,0x3c)[0];opt=pe+24
    return {'machine':hex(struct.unpack_from('<H',data,pe+4)[0]),
            'os_version':list(struct.unpack_from('<HH',data,opt+40)),
            'subsystem_version':list(struct.unpack_from('<HH',data,opt+48)),
            'sha256':hashlib.sha256(data).hexdigest()}


def run(arch: str,out: Path):
    if sys.platform!='win32':raise RuntimeError('Requires native Windows, not a Wine or Linux acceptance test')
    out=out.resolve();out.mkdir(parents=True,exist_ok=False);source=out/'probe.cpp';source.write_text(SOURCE)
    guids={'win7':'35138b9a-5d96-4fbd-8e2d-a2440225f93a','win8':'4a2f28e3-53b9-4441-ba9c-d69d4a4a6e38','win10':'8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a'}
    rows=[]
    variants=[('default',None,None),('no_manifest',None,None),('legacy','5.01' if arch=='x86' else '5.02',None),('subsystem62','6.02',None),*[(n,None,g) for n,g in guids.items()]]
    for label,version,guid in variants:
        exe=out/(label+'.exe')
        cmd=['cl','/nologo','/EHsc','/std:c++17','/O2','/MT',str(source),'/Fe:'+str(exe),'/Fo:'+str(out/(label+'.obj')),'/link','ddraw.lib','user32.lib']
        cmd+=['/SUBSYSTEM:CONSOLE'+(','+version if version else '')]
        if label!='default':cmd+=['/MANIFEST:NO']
        compiled=subprocess.run(cmd,capture_output=True,text=True)
        (out/(label+'-build.log')).write_text(compiled.stdout+compiled.stderr)
        if compiled.returncode:raise RuntimeError('Probe build failed: '+label+'\n'+compiled.stdout+compiled.stderr)
        if guid:
            manifest=out/(label+'.manifest');manifest.write_text('<?xml version="1.0" encoding="UTF-8" standalone="yes"?><assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0"><compatibility xmlns="urn:schemas-microsoft-com:compatibility.v1"><application><supportedOS Id="{'+guid+'}"/></application></compatibility></assembly>')
            subprocess.run(['mt','-nologo','-manifest',str(manifest),'-outputresource:'+str(exe)+';#1'],check=True)
        info=headers(exe);assert info['machine']==('0x14c' if arch=='x86' else '0x8664'),info
        result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=60)
        (out/(label+'-stdout.log')).write_text(result.stdout);(out/(label+'-stderr.log')).write_text(result.stderr)
        if result.returncode:raise RuntimeError('Native probe failed '+label+': '+str(result.returncode))
        observation=json.loads(result.stdout);assert observation['pointer_bits']==(32 if arch=='x86' else 64)
        row={'variant':label,'requested_subsystem':version,'supported_os_guid':guid,'headers':info,'observation':observation};rows.append(row)
        (out/'observations.json').write_text(json.dumps({'schema':'winrecomp.native-display-probe.v1','arch':arch,'rows':rows,'scope':'Native API measurement only; not a WinRecomp correctness or game test'},indent=2)+'\n')
        print(arch,label,'DDS modes',[(r['factory'],len(r.get('continue',{}).get('rows',[])),len(r.get('cancel',{}).get('rows',[]))) for r in observation['directdraw']],flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--arch',choices=['x86','x64'],required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();run(a.arch,a.out)
