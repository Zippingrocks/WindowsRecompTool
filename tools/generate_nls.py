"""Regenerate the bounded NLS header from measured, versioned Windows results."""
from pathlib import Path
import argparse
import json

ROOT = Path(__file__).resolve().parents[1]

def generate(data):
    if data['schema'] != 'winrecomp.windows-nls-observations.v2':
        raise ValueError('unsupported measurement schema')
    rows = data['rows']; encodings = data['encodings']
    for values in (rows, encodings):
        codes = [row['codepoint'] for row in values]
        if codes != sorted(set(codes)):
            raise ValueError('measurement codepoints must be sorted and unique')
    text = '''#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
// Observed GetStringTypeW/LCMapStringW results, Windows Server 2022.
// Probe: tests/windows_nls_probe.py; Actions run 36962189827.
// Not a full Unicode database or a claim of Windows 2000 NLS fidelity.
namespace wr {
struct NlsRow { std::uint16_t code,ctype1,ctype2,ctype3,lower,upper; };
'''
    text += 'inline constexpr std::array<NlsRow,'+str(len(rows))+'> nls_rows{{\n'
    for row in rows:
        if len(row['lower']) != 1 or len(row['upper']) != 1:
            raise ValueError('multi-codepoint case mapping cannot use this bounded table')
        values = [row['codepoint'], *(row['types'][str(n)] for n in (1,2,4)), row['lower'][0],row['upper'][0]]
        text += '{'+','.join(map(str,values))+'},\n'
    text += '''}};
inline const NlsRow* nls_row(std::uint32_t value) {
    std::size_t low=0,high=nls_rows.size();while(low<high){const auto mid=low+(high-low)/2;if(nls_rows[mid].code<value)low=mid+1;else high=mid;}
    return low<nls_rows.size() && nls_rows[low].code==value?&nls_rows[low]:nullptr;
}
struct NlsEncoding { std::uint16_t code;std::array<std::uint8_t,4> value;std::array<bool,4> substituted; };
'''
    text += 'inline constexpr std::array<NlsEncoding,'+str(len(encodings))+'> nls_encodings{{\n'
    for row in encodings:
        values = [row['flags'][str(n)] for n in (0,0x200,0x220,0x400)]
        if any(len(v['bytes'])!=1 for v in values):
            raise ValueError('non-single-byte conversion cannot use this bounded table')
        text += '{'+str(row['codepoint'])+', {'+','.join(str(v['bytes'][0]) for v in values)+'}, {'+','.join(str(v['used_default']).lower() for v in values)+'}},\n'
    text += '''}};
inline const NlsEncoding* nls_encoding(std::uint32_t value) {
 std::size_t low=0,high=nls_encodings.size();while(low<high){const auto mid=low+(high-low)/2;if(nls_encodings[mid].code<value)low=mid+1;else high=mid;}
 return low<nls_encodings.size() && nls_encodings[low].code==value?&nls_encodings[low]:nullptr;
}
}
'''
    return text

def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--check',action='store_true'); args=parser.parse_args()
    text=generate(json.loads((ROOT/'verification/windows-nls-observations.json').read_text()))
    path=ROOT/'include/winrecomp/nls_reference.hpp'
    if args.check:
        if path.read_text()!=text: raise SystemExit('NLS header differs from measured data; regenerate it')
        print('Bounded NLS header reproduces measured data exactly')
    else:path.write_text(text)

if __name__=='__main__':main()
