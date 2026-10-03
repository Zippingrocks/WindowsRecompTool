"""Apply the four exact process-lifecycle edits and retain them in a Git commit.

This is a one-time source publication helper, not part of the WinRecomp runtime.
It is idempotent and refuses a checkout with unexpected source contents.
"""
from pathlib import Path
import hashlib

root=Path(__file__).resolve().parents[1]
path=root/'src/runtime/process.cpp'
old=path.read_bytes()
digest=hashlib.sha256(old).hexdigest()
before='f5cae7d0f4f17244338994c7c597b12a51e3e78b747727e44ca73b22f1221225'
after='e6d4dbb098bdf693309b5c66ab8dacf50b54c72c1edb73f584ac13d87039fddb'
if digest==after:
    print('Process lifecycle integration already present')
elif digest!=before:
    raise SystemExit('Unexpected process.cpp: refusing to replace concurrent work')
else:
    text=old.decode('utf-8')
    changes=[
        ('    install_win32(*this);\n','    install_win32(*this);\n    gui_=install_gui(*this);\n'),
        ('Process::~Process()=default;',
         'Process::~Process(){if(gui_)gui_->shutdown();}\nconst Image& Process::source_image() const {if(!source_image_)throw std::runtime_error("no loaded source image");return *source_image_;}'),
        ('    loaded_=true;set_error(0);','    source_image_=std::make_unique<Image>(image);\n    loaded_=true;set_error(0);'),
        ('out<<"]}";return out.str();','out<<"],\\"gui\\":"<<(gui_?gui_->report():"null")<<"}";return out.str();'),
    ]
    for source,destination in changes:
        if text.count(source)!=1:raise SystemExit('Missing or ambiguous lifecycle patch')
        text=text.replace(source,destination)
    output=text.encode('utf-8')
    if hashlib.sha256(output).hexdigest()!=after:raise SystemExit('Integrated source checksum mismatch')
    path.write_bytes(output)
    print('Integrated exact checked process lifecycle source')
