#!/usr/bin/env python3
"""Rename a factory preset folder in an unpacked op1_factory.db, display only.

The folder of a synth_presets row is the name the preset browser shows for
its group (dr wave's is 'dr wave' while its type is 'drwave'); the firmware
matches factory presets to nothing by folder (docs/next-projects.md). The
patch JSON, and with it "type", is left alone.

Refuses unless exactly EXPECT rows have folder FROM, every one of them is a
synth preset whose JSON type is TYPE, and no row already has folder TO.
Keeps the file as the firmware's SQLite 3.6.20 reads it: rollback journal
(header bytes 18/19 = 1), no -journal file left, integrity_check ok.

Usage:
    python tools/rename_preset_folder.py DB FROM TO --expect N --type TYPE

Example (the MULTI engine's presets, ids 97-112):
    python tools/rename_preset_folder.py "op1 firmware/work_x/content/op1_factory.db" \\
        phase multi --expect 16 --type phase
"""

import argparse
import json
import os
import sqlite3
import sys


def fail(message):
    print('ERROR: ' + message)
    sys.exit(1)


def header_versions(path):
    with open(path, 'rb') as f:
        header = f.read(20)
    if header[:16] != b'SQLite format 3\x00':
        fail('{} is not an SQLite 3 database'.format(path))
    return header[18], header[19]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('db')
    parser.add_argument('old')
    parser.add_argument('new')
    parser.add_argument('--expect', type=int, required=True, help='rows that must have folder OLD')
    parser.add_argument('--type', required=True, help='the JSON type every renamed row must have')
    args = parser.parse_args()

    if not os.path.isfile(args.db):
        fail('{} not found'.format(args.db))
    if os.path.exists(args.db + '-journal') or os.path.exists(args.db + '-wal'):
        fail('{} has a journal file next to it'.format(args.db))
    if header_versions(args.db) != (1, 1):
        fail('{} is not in rollback-journal format'.format(args.db))

    db = sqlite3.connect(args.db, isolation_level=None)
    rows = db.execute('SELECT id, patch FROM synth_presets WHERE folder = ?', (args.old,)).fetchall()
    if len(rows) != args.expect:
        fail('{} rows have folder {!r}, expected {}'.format(len(rows), args.old, args.expect))
    for row_id, patch in rows:
        kind = json.loads(patch).get('type')
        if kind != args.type:
            fail('row {} has type {!r}, expected {!r}'.format(row_id, kind, args.type))
    if db.execute('SELECT count(*) FROM synth_presets WHERE folder = ?', (args.new,)).fetchone()[0]:
        fail('folder {!r} already exists'.format(args.new))

    db.execute('BEGIN')
    changed = db.execute('UPDATE synth_presets SET folder = ? WHERE folder = ?', (args.new, args.old)).rowcount
    if changed != args.expect:
        db.execute('ROLLBACK')
        fail('updated {} rows, expected {}'.format(changed, args.expect))
    db.execute('COMMIT')
    check = db.execute('PRAGMA integrity_check').fetchone()[0]
    db.close()

    if check != 'ok':
        fail('integrity_check: ' + check)
    if os.path.exists(args.db + '-journal') or os.path.exists(args.db + '-wal'):
        fail('a journal file was left behind')
    if header_versions(args.db) != (1, 1):
        fail('the database left rollback-journal format')
    ids = [r[0] for r in rows]
    print('Renamed folder {!r} -> {!r}: {} rows (ids {}..{}); integrity ok.'.format(
        args.old, args.new, changed, min(ids), max(ids)))


if __name__ == '__main__':
    main()
