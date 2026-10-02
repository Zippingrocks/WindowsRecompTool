"""Measure Windows NLS APIs for the bounded CP1252 compatibility corpus.

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
points = set(range(256))
for value in range(256):
    try:
        points.add(ord(bytes([value]).decode('cp1252')))
    except UnicodeDecodeError:
        pass
rows = []
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
    rows.append(row)
report = {'schema': 'winrecomp.windows-nls-observations.v1',
          'platform': platform.platform(),
          'observed_at_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
          'locale': 0x409, 'character_count': len(rows), 'rows': rows,
          'scope': 'Individual BMP codepoints in Latin-1 and the CP1252 repertoire; not full Unicode, contextual casing, or historical Windows fidelity.'}
Path('windows-nls-reference.json').write_text(json.dumps(report, indent=2) + '\n')
print('Measured', len(rows), 'characters against actual GetStringTypeW/LCMapStringW')
