"""OP-1 firmware packages (.op1): decode, unpack, pack without te-boot.ldr,
verify, and compare. The byte format is op1repacker's (CRC-32 prefix, LZMA
"alone" stream, tar); packing reuses its code so the result is the proven
one. Every package goes through tools/verify_bootloader.py before it is
reported as good."""

import binascii
import contextlib
import hashlib
import io
import lzma
import os
import shutil
import tarfile
import tempfile

from . import paths  # noqa: F401  (puts tools/ and op1repacker-master/ on sys.path)
import verify_bootloader  # noqa: E402
from op1repacker.op1_repack import OP1Repack  # noqa: E402

EXCLUDED = ('te-boot.ldr',)


class FirmwareError(Exception):
    pass


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def decode(path):
    """The tar bytes of a package, after checking its CRC."""
    with open(path, 'rb') as f:
        data = f.read()
    if len(data) < 8:
        raise FirmwareError('{}: too small to be a firmware package'.format(path))
    stored = int.from_bytes(data[:4], 'little')
    if stored != binascii.crc32(data[4:]):
        raise FirmwareError('{}: CRC-32 mismatch (not a complete .op1 file)'.format(path))
    try:
        return lzma.decompress(data[4:], format=lzma.FORMAT_ALONE)
    except lzma.LZMAError as e:
        raise FirmwareError('{}: not an LZMA package ({})'.format(path, e))


def _members(tar):
    for m in tar.getmembers():
        name = m.name[2:] if m.name.startswith('./') else m.name
        if name.startswith('/') or '..' in name.split('/'):
            raise FirmwareError('unsafe path in package: {!r}'.format(m.name))
        yield name, m


def contents(path):
    """{relative name: sha256} of every file in a package."""
    raw = decode(path)
    result = {}
    with tarfile.open(fileobj=io.BytesIO(raw)) as tar:
        for name, m in _members(tar):
            if m.isfile():
                result[name] = sha256_bytes(tar.extractfile(m).read())
    return result


def unpack(path, dest):
    """Extract a package into dest (created; must not exist or be empty).
    Returns the list of file names."""
    if os.path.exists(dest) and os.listdir(dest):
        raise FirmwareError('{} is not empty'.format(dest))
    os.makedirs(dest, exist_ok=True)
    raw = decode(path)
    names = []
    with tarfile.open(fileobj=io.BytesIO(raw)) as tar:
        for name, m in _members(tar):
            target = os.path.join(dest, *name.split('/'))
            if m.isdir():
                os.makedirs(target, exist_ok=True)
            elif m.isfile():
                os.makedirs(os.path.dirname(target), exist_ok=True)
                with open(target, 'wb') as f:
                    f.write(tar.extractfile(m).read())
                names.append(name)
    return names


def verify_stock(path, image):
    """Check that a package is the stock firmware the image was made for.
    Returns a report dict with ok, problems, vdk_sha256, teboot_sha256."""
    problems = []
    report = {'path': path, 'ok': False, 'problems': problems, 'vdk_sha256': None, 'teboot_sha256': None}
    if not os.path.isfile(path):
        problems.append('file not found: {}'.format(path))
        return report
    try:
        files = contents(path)
    except FirmwareError as e:
        problems.append(str(e))
        return report
    want = image['stock']
    vdk = files.get('OP1_vdk.ldr')
    boot = files.get('te-boot.ldr')
    report['vdk_sha256'] = vdk
    report['teboot_sha256'] = boot
    if vdk is None:
        problems.append('no OP1_vdk.ldr in the package')
    elif vdk != want['vdk_sha256']:
        problems.append('OP1_vdk.ldr is not stock 246 (sha256 {}…); the builder needs the unmodified op1_246.op1'.format(vdk[:12]))
    if boot is not None and boot != want['teboot_sha256']:
        problems.append('te-boot.ldr is not the known stock bootloader')
    if not os.path.basename(path).lower().endswith('.op1'):
        problems.append('not a .op1 file name')
    report['ok'] = not problems
    report['files'] = len(files)
    return report


def pack(work_dir, out_path):
    """Pack a work dir into out_path without te-boot.ldr. Refuses to
    overwrite. Returns the package size."""
    if not os.path.isfile(os.path.join(work_dir, 'OP1_vdk.ldr')):
        raise FirmwareError('{} has no OP1_vdk.ldr'.format(work_dir))
    if os.path.exists(out_path):
        raise FirmwareError('{} already exists (remove it first)'.format(out_path))
    name = os.path.basename(out_path)
    for problem in verify_bootloader.check_file_name(out_path):
        raise FirmwareError(problem)
    if 'FRC' in name.upper():
        raise FirmwareError('the file name must not contain FRC')
    repacker = OP1Repack()
    with tempfile.TemporaryDirectory() as temp:
        stage = os.path.join(temp, 'fw')
        shutil.copytree(work_dir, stage,
                        ignore=lambda d, names: [n for n in names if d == work_dir and n in EXCLUDED])
        repacker.compress_tar(stage, out_path)
        repacker.compress_lzma(out_path)
        repacker.add_crc(out_path)
    return os.path.getsize(out_path)


def verify(path):
    """tools/verify_bootloader.py's checks on a package or a directory.
    Returns (ok, problems, printed lines)."""
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        problems = verify_bootloader.verify(path, allow_stock_bootloader=False)
    lines = [l.strip() for l in out.getvalue().splitlines() if l.strip()]
    return not problems, list(problems), lines


def compare_with_dir(package, work_dir):
    """tools/pkg_check.py: every file in the package equals the work dir's,
    and every work dir file (bar te-boot.ldr) is in the package.
    Returns (files in package, mismatches)."""
    pkg = contents(package)
    mismatches = []
    for rel, digest in pkg.items():
        wp = os.path.join(work_dir, *rel.split('/'))
        if not os.path.isfile(wp) or sha256_file(wp) != digest:
            mismatches.append('differs from the work dir: ' + rel)
    for root, _, files in os.walk(work_dir):
        for f in files:
            rel = os.path.relpath(os.path.join(root, f), work_dir).replace('\\', '/')
            if rel not in pkg and rel not in EXCLUDED:
                mismatches.append('missing from the package: ' + rel)
    return len(pkg), mismatches


def diff_dirs(stock_dir, work_dir):
    """Files added, changed and removed in work_dir relative to stock_dir
    (te-boot.ldr ignored)."""
    def tree(d):
        out = {}
        for root, _, files in os.walk(d):
            for f in files:
                rel = os.path.relpath(os.path.join(root, f), d).replace('\\', '/')
                if rel not in EXCLUDED:
                    out[rel] = sha256_file(os.path.join(root, f))
        return out
    a, b = tree(stock_dir), tree(work_dir)
    return {
        'added': sorted(k for k in b if k not in a),
        'changed': sorted(k for k in b if k in a and a[k] != b[k]),
        'removed': sorted(k for k in a if k not in b),
    }
