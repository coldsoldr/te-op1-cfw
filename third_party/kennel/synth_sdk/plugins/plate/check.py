#!/usr/bin/env python3
"""Focused Plate DSP and unclipped procedural geometry checks (native text excluded)."""
import itertools
import runpy
import json
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET
P=Path(__file__).resolve().parent
SDK=P.parent.parent
with tempfile.TemporaryDirectory(prefix='plate-check-') as tmp:
    out=Path(tmp)
    flags=['cc','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=undefined']
    subprocess.run(flags+[str(P/'plate_test.c'),str(P/'plate_dsp.c'),'-o',str(out/'dsp')],check=True)
    subprocess.run([str(out/'dsp')],check=True)
    subprocess.run(flags+['-DOP1_HOST','-DFX=FX_plate','-DSCREEN=SCREEN_plate','-I'+str(SDK),str(SDK/'harness/screen_harness.c'),*(str(P/n) for n in ('plate.c','plate_dsp.c','plate_screen.c')),'-lm','-o',str(out/'screen')],check=True)
    for knobs in itertools.product((0,32767),repeat=4):
        frame=json.loads(subprocess.check_output([str(out/'screen'),*map(str,knobs),'1','100']))
        assert frame['lines']
        for x,y,xx,yy,width,color in frame['lines']:
            assert min(x,xx)-width//2>=0 and max(x,xx)+width//2<320*1024
            assert min(y,yy)-width//2>=0 and max(y,yy)+width//2<160*1024
    subprocess.run(flags+['-DOP1_HOST','-DFX=FX_plate','-DSCREEN=SCREEN_plate','-I'+str(SDK),str(P/'screen_test.c'),*(str(P/n) for n in ('plate.c','plate_dsp.c','plate_screen.c')),'-lm','-o',str(out/'bypass')],check=True)
    subprocess.run([str(out/'bypass')],check=True)
    assert runpy.run_path(str(P/'gen_art.py'))['generate']()==(P/'plate_art.h').read_text(), 'stale Plate rectangle table'
    assert len(list(ET.parse(P/'plate.svg').getroot()))==0, 'runtime SVG must not restore retained rectangle nodes'
    assert len(list(ET.parse(P/'plate_art.svg').getroot()))==657, 'reference art rectangle count changed'
    meta=json.loads((P/'plugin.json').read_text())
    assert meta['name']=='plate' and meta['stereo'] and meta['loadable'] and 'order' not in meta
    svg=ET.parse(P/'plate.svg').getroot()
    assert svg.attrib['viewBox']=='0 0 320 160'
    assert all(e.tag.rsplit('}',1)[-1] in {'svg','path'} and 'opacity' not in e.attrib for e in svg.iter())
print('Plate metadata/SVG and 16 procedural endpoint states PASS; native text needs real captures')
