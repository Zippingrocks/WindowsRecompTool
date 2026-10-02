"""Author-created PE fixtures. No game bytes are embedded in the test suite."""
from __future__ import annotations
import struct

def put16(b: bytearray, offset: int, value: int) -> None:
    struct.pack_into('<H', b, offset, value)

def put32(b: bytearray, offset: int, value: int) -> None:
    struct.pack_into('<I', b, offset, value)

def make_pe(code: bytes, data: bytes = b'', base: int = 0x400000) -> bytearray:
    raw_size = max(0x200, (len(code) + 511) & ~511)
    data_rva = (0x1000 + raw_size + 4095) & ~4095
    data_size = max(0x200, (len(data) + 511) & ~511)
    b = bytearray(0x200 + raw_size + data_size)
    b[:2] = b'MZ'
    put32(b, 0x3c, 0x80)
    b[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HHIIIHH', b, 0x84, 0x14c, 2, 0, 0, 0, 224, 0x102)
    opt = 0x98
    put16(b, opt, 0x10b)
    put32(b, opt+16, 0x1000)
    put32(b, opt+28, base)
    put32(b, opt+32, 0x1000)
    put32(b, opt+36, 0x200)
    put32(b, opt+56, data_rva+((data_size+4095)&~4095))
    put32(b, opt+60, 0x200)
    put16(b, opt+68, 3)
    put32(b, opt+92, 16)
    s = opt+224
    b[s:s+8] = b'.text\0\0\0'
    for off, v in [(8,len(code)),(12,0x1000),(16,raw_size),(20,0x200),(36,0x60000020)]:
        put32(b,s+off,v)
    s += 40
    b[s:s+8] = b'.data\0\0\0'
    for off, v in [(8,max(len(data),0x300)),(12,data_rva),(16,data_size),(20,0x200+raw_size),(36,0xc0000040)]:
        put32(b,s+off,v)
    b[0x200:0x200+len(code)] = code
    b[0x200+raw_size:0x200+raw_size+len(data)] = data
    return b

def with_imports(code: bytes, fallback: bool = False) -> bytearray:
    data = bytearray(0x200)
    struct.pack_into('<IIIII',data,0,0 if fallback else 0x2080,0,0,0x2060,0x20a0)
    data[0x60:0x69] = b'fake.dll\0'
    for pos in [0x80,0xa0]:
        struct.pack_into('<III',data,pos,0x20c0,0x8000002a,0)
    data[0xc0:0xc8] = b'\0\0Thing\0'
    b=make_pe(code,data)
    put32(b,0x98+96+8,0x2000)
    put32(b,0x98+100+8,40)
    return b
