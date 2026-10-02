"""Measure Windows NLS APIs for a bounded CP1252 compatibility corpus.

No game files are used. Results describe the runner, not historical Windows 2000.
"""
import ctypes as c
import datetime
import json
from pathlib import Path
import platform
import sys

if sys.platform != 'win32':
    raise SystemExit('This probe requires Windows; an unavailable oracle is not a pass.')
k = c.WinDLL('kernel32', use_last_error=True)
k.GetStringTypeW.argtypes = [c.c_uint32, c.c_wchar_p, c.c_int, c.POINTER(c.c_uint16)]
k.GetStringTypeW.restype = c.c_int
k.LCMapStringW.argtypes = [c.c_uint32, c.c_uint32, c.c_wchar_p, c.c_int, c.c_wchar_p, c.c_int]
k.LCMapStringW.restype = c.c_int
k.WideCharToMultiByte.argtypes = [c.c_uint32, c.c_uint32, c.c_wchar_p, c.c_int, c.c_char_p, c.c_int, c.c_char_p, c.POINTER(c.c_int)]
k.WideCharToMultiByte.restype = c.c_int
points = set(range(256))
for value in range(256):
    try:
        points.add(ord(bytes([value]).decode('cp1252')))
    except UnicodeDecodeError:
        pass
rows = []
encode_points = set(points)
for point in sorted(points):
    ch = chr(point)
    row = {'codepoint': point, 'types': {}}
    for flag in [1, 2, 4]:
        value = c.c_uint16()
        if not k.GetStringTypeW(flag, ch, 1, c.byref(value)):
            raise c.WinError(c.get_last_error())
        row['types'][str(flag)] = value.value
    for name, flag in [('lower', 0x100), ('upper', 0x200)]:
        output = c.create_unicode_buffer(8)
        count = k.LCMapStringW(0x409, flag, ch, 1, output, len(output))
        if not count:
            raise c.WinError(c.get_last_error())
        row[name] = [ord(output[n]) for n in range(count)]
        encode_points.update(row[name])
    rows.append(row)

def encode(text, flags):
    output = c.create_string_buffer(len(text) * 4 + 8)
    used = c.c_int(0)
    size = k.WideCharToMultiByte(1252, flags, text, len(text), output, len(output), None, c.byref(used))
    if not size:
        raise c.WinError(c.get_last_error())
    return {'bytes': list(output.raw[:size]), 'used_default': bool(used.value)}

encodings = []
for point in sorted(encode_points):
    encodings.append({'codepoint': point, 'flags': {str(flag): encode(chr(point), flag) for flag in [0, 0x200, 0x220, 0x400]}})
# Verify that the bounded corpus is also compositional as an explicit-length
# string; do not extrapolate this to combining sequences or arbitrary Unicode.
joined = ''.join(chr(point) for point in sorted(encode_points))
for flag in [0, 0x200, 0x220, 0x400]:
    bulk = encode(joined, flag)
    isolated = [byte for row in encodings for byte in row['flags'][str(flag)]['bytes']]
    assert bulk['bytes'] == isolated
report = {'schema': 'winrecomp.windows-nls-observations.v2',
          'platform': platform.platform(),
          'observed_at_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
          'locale': 0x409, 'character_count': len(rows), 'rows': rows,
          'encodings': encodings, 'bounded_bulk_conversion_verified': True,
          'scope': 'Individual BMP codepoints in Latin-1/CP1252 and their measured case mappings; not arbitrary Unicode, contextual casing, or historical Windows fidelity.'}
Path('windows-nls-reference.json').write_text(json.dumps(report, indent=2) + '\n')
print('Measured', len(rows), 'NLS characters and', len(encodings), 'CP1252 encodings against Windows')
