"""Copies upstream Plaits' DSP into ports/plaits/src (namespace op1plaits).

  python3 resources/copy_upstream.py [--force]      (from ports/plaits)

Copies mutableinstruments/plaits/dsp (without dsp/engine2: the port uses the
original 16 engines) and plaits/resources.{h,cc} into src/, and rewrites:
  - namespace plaits            -> namespace op1plaits
  - #include "plaits/dsp/..."   -> #include "src/..."
  - #include "plaits/resources.h" -> #include "src/resources.h"
  - include guards PLAITS_      -> OP1PLAITS_
Nothing else is changed here. src/voice.{h,cc} are then edited by hand to
drop the engine2 engines and the user-data flash (see docs/plaits-port.md);
`make copy-check` proves the copy renders bit-identically to upstream.
"""
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.dirname(HERE)
MI = os.path.join(PORT, '..', '..', 'mutableinstruments')
UPSTREAM = os.path.join(MI, 'plaits')
DEST = os.path.join(PORT, 'src')


def rewrite(text):
    text = re.sub(r'\bnamespace plaits\b', 'namespace op1plaits', text)
    text = text.replace('// namespace plaits', '// namespace op1plaits')
    text = text.replace('#include "plaits/dsp/', '#include "src/')
    text = text.replace('#include "plaits/resources.h"', '#include "src/resources.h"')
    text = re.sub(r'\bPLAITS_', 'OP1PLAITS_', text)
    return text


def main(argv):
    if os.path.exists(DEST) and '--force' not in argv:
        print('%s exists (hand edits to voice.*); use --force to overwrite' % DEST)
        return 1
    count = 0
    for root, dirs, files in os.walk(os.path.join(UPSTREAM, 'dsp')):
        dirs[:] = sorted(d for d in dirs if d != 'engine2')
        rel = os.path.relpath(root, os.path.join(UPSTREAM, 'dsp'))
        for name in sorted(files):
            if not name.endswith(('.h', '.cc')):
                continue
            src = os.path.join(root, name)
            dst = os.path.normpath(os.path.join(DEST, rel, name))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(src, 'r', newline='') as f:
                text = f.read()
            with open(dst, 'w', newline='') as f:
                f.write(rewrite(text))
            count += 1
    for name in ('resources.h', 'resources.cc'):
        with open(os.path.join(UPSTREAM, name), 'r', newline='') as f:
            text = f.read()
        with open(os.path.join(DEST, name), 'w', newline='') as f:
            f.write(rewrite(text))
        count += 1
    print('copied %d files to %s' % (count, DEST))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
