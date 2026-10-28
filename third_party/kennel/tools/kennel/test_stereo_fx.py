"""Targeted optional stereo ABI checks; no production plugins or firmware are built."""
import copy
import json
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path
import pack
import reloc


class StereoFX(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.work = Path(cls.tmp.name)
        cls.exe = cls.work / 'loader'
        subprocess.run([shutil.which('cc'), '-O2', '-Wall', '-Werror', '-o', str(cls.exe),
                        str(pack.SDK / 'harness/kennel_load_host.c'), str(pack.SDK / 'kennel/kn_load.c')], check=True)
        folder = cls.work / 'stereo_test'
        folder.mkdir()
        (folder / 'stereo_test.c').write_text('''#include "op1_plugin.h"
static volatile int seen;
static void init(int pos) { seen = pos; }
static void mono(int pos, int16_t *b, const int16_t *p) { b[0] = p[0] + pos; }
static void stereo(int pos, int16_t *l, int16_t *r, const int16_t *p) { l[0] = p[0]; r[0] = pos; }
const op1_fx_stereo_t FX_test = {{init, mono, {0,0,0,0}}, stereo};
const op1_fx_t FX_prefix = {init, mono, {0,0,0,0}};
''')
        cls.meta = {'name': 'stereo_test', 'kind': 'fx', 'symbol': 'FX_test', 'sources': ['stereo_test.c'], 'stereo': True}
        (folder / 'plugin.json').write_text(json.dumps(cls.meta))
        cls.data, _ = pack.pack_dir(folder, cls.work / 'stereo_test.knl', keep=folder)
        cls.obj = (folder / 'stereo_test.ro').read_bytes()
        cls.plugin = reloc.parse(cls.data)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def altered(self, field=None, value=None, transform=None):
        data = bytearray(self.data)
        if field:
            values = list(reloc.HEADER.unpack_from(data))
            values[reloc.FIELDS.index(field)] = value
            reloc.HEADER.pack_into(data, 0, *values)
        if transform:
            transform(data)
        struct.pack_into('<I', data, 12, reloc.crc32(data))
        return bytes(data)

    def callbacks(self, offset, target=None):
        field = self.plugin['entry_offset'] + offset
        def change(data):
            relocs = list(self.plugin['relocs'])
            if target is None:
                relocs = [r for r in relocs if r[0] != field]
                struct.pack_into('<I', data, self.plugin['image_file_offset'] + field, 0)
            else:
                relocs = [(off, typ, sym, target if off == field else addend) for off, typ, sym, addend in relocs]
            values = list(reloc.HEADER.unpack_from(data))
            values[reloc.FIELDS.index('reloc_count')] = len(relocs)
            reloc.HEADER.pack_into(data, 0, *values)
            for index, (off, typ, sym, addend) in enumerate(relocs):
                reloc.RELOC.pack_into(data, self.plugin['reloc_offset'] + index * reloc.RELOC.size, off, typ, 0, sym, addend)
        return self.altered(transform=change)

    def load(self, data, base=0x02980000):
        path, output = self.work / 'input.knl', self.work / 'output.bin'
        path.write_bytes(data)
        output.unlink(missing_ok=True)
        c = subprocess.run([str(self.exe), str(path), hex(base), str(output)], capture_output=True, text=True)
        try:
            py = reloc.relocate(reloc.parse(data, 1), base, {})
        except reloc.PluginError:
            py = None
        self.assertEqual(c.returncode == 0, py is not None, c.stderr)
        if py is not None:
            self.assertEqual(output.read_bytes(), py)
        return py

    def test_stereo_and_unflagged_prefix(self):
        self.assertTrue(self.plugin['flags'] & reloc.FLAG_STEREO)
        self.assertIsNotNone(self.load(self.data))
        # The same prefix is a valid old mono entry: an old file need not supply stereo.
        self.assertIsNotNone(self.load(self.altered('flags', 0)))
        mono, _ = pack.pack(dict(self.meta, stereo=False, symbol='FX_prefix'), self.obj)
        self.assertIsNotNone(self.load(mono))

    def test_flag_and_entry_rejections(self):
        for field, value in [('flags', 8), ('kind', reloc.KIND_SYNTH),
                             ('entry_offset', self.plugin['image_size'] - 4)]:
            with self.subTest(field=field):
                self.assertIsNone(self.load(self.altered(field, value)))

    def test_every_callback_is_local_executable(self):
        for field in (0, 4, 12):
            for target in (None, 1, self.plugin['text_size'], self.plugin['rodata_offset']):
                with self.subTest(field=field, target=target):
                    self.assertIsNone(self.load(self.callbacks(field, target)))
        self.assertIsNone(self.load(self.data, 0xFFFFFFF0))

    def test_actual_dispatch_generation_and_mono_replacement(self):
        # Compile the production dispatch/registration bodies and actual generated
        # table against firmware stubs, so routing is not a parallel test model.
        import importlib.util
        import sys
        sys.path.insert(0, str(pack.SDK))
        spec = importlib.util.spec_from_file_location('stereo_make_firmware', pack.SDK / 'make_firmware.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        generated = self.work / 'generated'
        (generated / 'build').mkdir(parents=True, exist_ok=True)
        module.HERE = str(generated)
        module.gen_table([], [{'name': 'test_stereo', 'order': 1, 'symbol': 'FX_stereo', 'stereo': True},
                              {'name': 'test_mono', 'order': 2, 'symbol': 'FX_plain'}])
        dispatch = (pack.SDK / 'dispatcher.c').read_text()
        body = dispatch[dispatch.index('static void fx_render_stereo_dispatch('):dispatch.index('/* Keep the raw knob words')]
        boot = (pack.SDK / 'kennel/kn_boot.c').read_text()
        register = boot[boot.index('static const char *register_plugin('):boot.index('static uint32_t alloc(')]
        code = r"""
#define OP1_HOST
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "op1_plugin.h"
#define OP1_FX_MASTER 2
#include "op1_synth.h"
volatile uint32_t sm_cy[SMC_N];
static int inits, monos, stereos, originals, budgets, sanitizes;
static void init(int pos) { assert(pos == 2); inits++; }
static void mono(int pos, int16_t *b, const int16_t *p) {
    (void)p; assert(pos == 2); monos++; for(int i=0;i<64;i++) b[i]+=5;
}
static void stereo(int pos, int16_t *l, int16_t *r, const int16_t *p) {
    (void)p; assert(pos == 2); stereos++; for(int i=0;i<64;i++){l[i]+=3;r[i]-=7;}
}
const op1_fx_stereo_t FX_stereo = {{init, mono,{0,0,0,0}}, stereo};
const op1_fx_t FX_plain = {init, mono,{0,0,0,0}};
#include "GENERATED"
static int fx_cur_master = -1;
static int fx_index(int n) { return n > 0 && n < OP1_SLOTS && op1_fxs[n] ? n : 0; }
static void fx_sanitize(void) { sanitizes++; }
static unsigned cycles_now(void) { static unsigned ticks; return ticks++; }
static void budget(int kind, int index, unsigned used) { assert(kind==1 && index>0 && used==1); budgets++; }
static void original(void *s,int16_t *l,int16_t *r,const int16_t *p) { (void)s;(void)p; originals++;l[0]=7;r[0]=-8; }
#define FILTER_ST_RENDER original
DISPATCH
static uint8_t from_file[2][OP1_SLOTS];
static struct { unsigned flags; } inventory, *kn_inv_cur = &inventory;
#define KN_INV_REPLACES 1
#define KN_NONE 0xffffffffu
static int builtin_number(int fx,const char *n) { (void)fx;(void)n;return 0; }
static int number_for(int fx,const char *n,int pref) { (void)fx;(void)n;return pref; }
static int seq(const char *a,const char *b) { return !strcmp(a,b); }
static void *kn_alloc(unsigned n) { static char arena[256];static unsigned at;void *p=arena+at;at+=n;assert(at<=256);return p; }
static void copy_str(char *d,const char *s,int n) { strncpy(d,s,n); d[n-1]=0; }
static int append(const char **table,const char *name) { (void)table;(void)name;return 0; }
static void *factory(void) { return 0; }
#define DB_FACTORY factory
static void row_ensure(void *db,int fx,const char *name,const int16_t *def,const int16_t *page,int n) {
    (void)db;(void)fx;(void)name;(void)def;(void)page;(void)n;
}
REGISTER
int main(void) {
    op1_fx_stereo_init();
    assert(op1_fxs[1] == &FX_stereo.mono && op1_fxs[2] == &FX_plain);
    assert(op1_fx_stereo_renders[1] == stereo && !op1_fx_stereo_renders[2]);
    int16_t p[8]={0},l[64],r[64];
    for(int i=0;i<64;i++){l[i]=100;r[i]=-50;}
    p[4]=OP1_ENGINE_MAGIC+1; fx_render_stereo(0,l,r,p);
    assert(l[0]==103 && r[0]==-57 && stereos==1 && monos==0 && inits==1 && budgets==1);
    fx_render_stereo(0,l,r,p); assert(inits==1 && stereos==2);
    for(int i=0;i<64;i++){l[i]=10;r[i]=30;}
    p[4]=OP1_ENGINE_MAGIC+2;fx_render_stereo(0,l,r,p);
    assert(l[0]==15 && r[0]==35 && monos==1 && stereos==2 && inits==2 && budgets==3);
    unsigned cat=sm_cy[SMC_FX];
    p[4]=0;fx_render_stereo(0,l,r,p);assert(originals==1 && l[0]==7 && r[0]==-8);
    assert(sm_cy[SMC_FX]>cat && budgets==3); // Stock counted; only plugin calls budgeted.
    // The same registration path is used by legacy .op1p and mono .knl files.
    op1_fx_stereo_renders[3]=stereo;
    assert(!register_plugin(1,"newmono",0x1000010,0xffffffff,0,0,3,p,0,0));
    assert(!op1_fx_stereo_renders[3] && from_file[1][3]);
    assert(!register_plugin(1,"newstereo",0x1000020,0xffffffff,0,0,4,p,0,stereo));
    assert(op1_fx_stereo_renders[4]==stereo);
    // Reinitialization clears file callbacks before boot registers files anew.
    op1_fx_stereo_init();assert(!op1_fx_stereo_renders[4] && op1_fx_stereo_renders[1]==stereo);
    return 0;
}
"""
        code = code.replace('"GENERATED"', json.dumps(str(generated / 'build/engines_table.c')))
        code = code.replace('DISPATCH', body).replace('REGISTER', register)
        source, executable = self.work / 'dispatch.c', self.work / 'dispatch'
        source.write_text(code)
        subprocess.run([shutil.which('cc'), '-O2', '-Wall', '-Werror', '-Wno-int-to-pointer-cast',
                        '-I' + str(pack.SDK), str(source), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)

    def test_metadata_kind_and_boolean(self):
        for changes in ({'kind': 'synth'}, {'kind': 'feature'}, {'stereo': 'false'}, {'symbol': 'FX_prefix'}):
            with self.subTest(changes=changes), self.assertRaises(pack.PackError):
                pack.pack(dict(self.meta, **changes), self.obj)


if __name__ == '__main__':
    unittest.main()
