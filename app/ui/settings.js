// Settings: the stock file, the shipped image, the OP-1 disk, backups, output folder, and the safety rules.
import { api, app, h, clear, toast, browseDialog } from './app.js';

export async function settingsView(main) {
  const s = Object.assign({}, app.state.settings);
  const img = app.state.image;

  const stockInput = h('input.mono', { type: 'text', value: s.stock_package });
  const stockCheck = h('div.checks');
  const outInput = h('input.mono', { type: 'text', value: s.output_dir });
  const backupInput = h('input.mono', { type: 'text', value: s.backup_dir });
  const deviceInput = h('input.mono', { type: 'text', value: s.device_root, placeholder: 'auto-detected' });
  const windowSel = h('select', h('option', { value: 'browser', selected: s.window === 'browser' }, 'browser tab (current)'), h('option', { value: 'desktop', selected: s.window === 'desktop' }, 'desktop window (pywebview, not installed yet)'));

  async function checkStock() {
    clear(stockCheck);
    stockCheck.append(h('div', h('span.pending', '○'), 'checking…'));
    try {
      const r = await api.get('/api/stock', { path: stockInput.value });
      clear(stockCheck);
      stockCheck.append(h('div', h('span' + (r.ok ? '.ok' : '.bad'), r.ok ? '✓' : '✗'), r.ok ? 'verified: stock 246, OP1_vdk.ldr sha256 ' + r.vdk_sha256.slice(0, 12) + '…, ' + r.files + ' files' : r.problems.join('; ')));
    } catch (e) { clear(stockCheck); stockCheck.append(h('div', h('span.bad', '✗'), e.message)); }
  }

  async function save() {
    try {
      app.state.settings = await api.post('/api/settings', { stock_package: stockInput.value, output_dir: outInput.value, backup_dir: backupInput.value, device_root: deviceInput.value, window: windowSel.value });
      toast('Settings saved', 'ok');
      checkStock();
    } catch (e) { toast(e.message, 'err'); }
  }
  const pick = (input, kind, filter) => h('button.small', { onclick: async () => { const p = await browseDialog('Choose', input.value || app.state.settings.output_dir, kind, filter); if (p) input.value = p; } }, 'Browse…');

  const disks = h('div.row');
  const refreshDisks = async () => { clear(disks); const r = await api.get('/api/device/disks'); if (!r.disks.length) disks.append(h('span.dim', 'no OP-1 disk found')); for (const d of r.disks) disks.append(h('span.chip.on', d)); };
  const backups = h('ul.plain');
  for (const b of app.state.backups.slice(0, 8)) backups.append(h('li', h('b', b.when), ' · ', b.files + ' files · ', h('span.mono', b.path)));
  if (!app.state.backups.length) backups.append(h('li', 'no backups yet'));

  clear(main);
  main.append(h('section.view',
    h('header', h('h2', 'Settings')),
    h('div.cols',
      h('div.stack',
        h('div.card', h('h3', 'Firmware'),
          h('label.field', 'Stock firmware file (the unmodified op1_246.op1)', h('div.row', h('div.grow', stockInput), pick(stockInput, 'file', (n) => /\.op1$/i.test(n)), h('button.small', { onclick: checkStock }, 'Verify'))),
          stockCheck,
          h('dl.kv', h('dt', 'shipped image'), h('dd', img.elf + ' · sha256 ' + img.elf_sha256.slice(0, 16) + '…'), h('dt', 'built with'), h('dd', img.built_with),
            h('dt', 'module bits'), h('dd', Object.entries(img.mask_bits).map(([k, v]) => k + '=' + v).join(' · ')),
            h('dt', 'stock OP1_vdk.ldr'), h('dd', img.stock.vdk_sha256.slice(0, 16) + '…'),
            h('dt', 'preset limits'), h('dd', img.limits.non_sampler + ' non-sampler · ' + img.limits.sampler + ' sampler · ' + img.limits.total + ' total')),
          h('label.field', 'Output folder for built packages', h('div.row', h('div.grow', outInput), pick(outInput, 'dir')))),
        h('div.card', h('h3', 'OP-1 disk'),
          h('div.row', h('span.eyebrow', 'detected'), disks, h('button.small', { onclick: refreshDisks }, 'Detect')),
          h('label.field', 'Disk location (leave empty to auto-detect)', h('div.row', h('div.grow', deviceInput), pick(deviceInput, 'dir'))),
          h('label.field', 'Backup folder', h('div.row', h('div.grow', backupInput), pick(backupInput, 'dir'))),
          h('div', h('span.eyebrow', 'backups'), backups)),
        h('div.card', h('h3', 'App'), h('label.field', 'Window', windowSel), h('div.row', h('button.primary', { onclick: save }, 'Save settings')))),
      h('div.card.sticky', h('h3', 'Safety'),
        h('ul.plain',
          h('li', h('b', 'The bootloader is never touched.'), ' A package never contains te-boot.ldr, and every package passes the bootloader check before it is reported.'),
          h('li', h('b', 'You install, not the app.'), ' Builds go to a file; you copy it to the OP-1 in te-boot upload mode (hold COM at power-on, "1. Upload firmware"). The app never writes to the OP-1 in that mode.'),
          h('li', h('b', 'Back up first.'), ' In disk mode the librarian reads freely but writes only after a backup, one confirmed change at a time, with undo.'),
          h('li', h('b', 'No FRC in file names, no .KEY files.'), ' Both would make te-boot do things you cannot undo.'),
          h('li', h('b', 'Never use te-boot’s factory reset or format.'), ' They erase the display calibration files.'),
          h('li', h('b', 'Recovery:'), ' hold COM at power-on and install the stock op1_246.op1. Before going back to stock, set every saved sound to the first effect of its category.')))),
  ));
  checkStock();
  refreshDisks();
  return {};
}
