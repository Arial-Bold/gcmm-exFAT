#!/usr/bin/env python3
"""Makes source/net/webpage.h from source/net/webpage.html: the page phones
and computers see in Network transfer, as one C string.

    python3 tools/make_webpage.py"""
import os
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, '..', 'source', 'net')
html = open(os.path.join(SRC, 'webpage.html'), encoding='utf-8').read()
out = ['/* webpage.h - made by tools/make_webpage.py from webpage.html: do not edit by hand. */',
       '#pragma once', 'static const char WEBPAGE[] =']
for line in html.split('\n'):
    esc = line.replace('\\', '\\\\').replace('"', '\\"').encode('utf-8')
    s = ''.join(chr(b) if 32 <= b < 127 else '\\%03o' % b for b in esc)
    out.append('\t"%s\\n"' % s)
out[-1] += ';'
open(os.path.join(SRC, 'webpage.h'), 'w').write('\n'.join(out) + '\n')
print('wrote source/net/webpage.h')
