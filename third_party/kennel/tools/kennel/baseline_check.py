#!/usr/bin/env python3
"""Build this kennel tree as a hundekraft plugin base and compare reference.op1 with an expected SHA-1.

    python3 tools/kennel/baseline_check.py OP1_EMULATOR_ROOT DEST [EXPECTED_SHA1]

Uses OP1-Emulator's tools/firmware/op1r_plugins.make_base with the op1r30mo-chord-plugins profile and `source`
pointed at this checkout (only for this run; profiles.py is not changed). DEST must not exist. Exit 1 on mismatch.
Copyright (C) 2026 Hundekraft. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE).
"""
import hashlib
import os
import sys
from pathlib import Path

root, dest = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
expected = sys.argv[3] if len(sys.argv) > 3 else None
kennel = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tools/firmware'))
import profiles, op1r_plugins as P

profile = dict(profiles.BASES['op1r30mo-chord-plugins'], source=os.path.relpath(kennel, root))
P.select(profile)
P.make_base(dest)
sha = hashlib.sha1((dest / 'reference.op1').read_bytes()).hexdigest()
print(sha)
if expected and sha != expected:
    sys.exit(f'mismatch: expected {expected}')
