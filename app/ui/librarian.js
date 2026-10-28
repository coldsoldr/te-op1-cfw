// The librarian: the OP-1 disk's presets, backup, organise, edit, preview.
import { api, app, h, clear, toast, confirmDialog, promptDialog, browseDialog, modal, sorter, fmtBytes, fmtDate } from './app.js';
import { makeEditor } from './editor.js';

export async function librarianView(main) {
  let scan = null;
  let folder = 'synth';     // 'synth', 'drum', 'synth/sub', 'tape', 'album'
  let selectedRel = null;
  let query = '';
  let filters = new Set();
  let undo = { log: [], backed_up: false };
  let editor = null;
  // Column sorting: one order for the preset tables, one for tape and album.
  const presetSort = sorter('name', 1, () => renderTable());
  const fileSort = sorter('file', 1, () => renderTable());

  const diskSel = h('select');
  const refreshBtn = h('button.small', { onclick: () => load(true) }, 'Rescan');
  const backupBtn = h('button.green', { onclick: doBackup }, 'Back up the OP-1');
  const importBtn = h('button', { onclick: doImport }, 'Import .aif');
  const exportBtn = h('button', { onclick: doExport }, 'Export');
  const folderBtn = h('button', { onclick: doNewFolder }, 'New folder');
  const factoryBtn = h('button', { onclick: doCopyFactory, title: 'Put every stock factory preset on the OP-1 as a user preset, in a folder named after its engine' }, 'Copy factory presets to the OP-1');
  const undoBtn = h('button.small', { onclick: doUndo }, 'Undo');
  const writeNote = h('span.dim', { style: { fontSize: '0.85rem' } });
  const tree = h('div.tree');
  const search = h('input', { type: 'text', placeholder: 'search names, engines, effects…', style: { width: '100%' } });
  search.addEventListener('input', () => { query = search.value.toLowerCase(); renderTable(); });
  const filterBox = h('div.filters');
  const tableBox = h('div.tablebox.scrolly');
  const detail = h('div.card.sticky');
  const undoStrip = h('div.undo');

  async function load(force) {
    const disks = (await api.get('/api/device/disks')).disks;
    clear(diskSel);
    for (const d of disks) diskSel.append(h('option', { value: d, selected: d === app.device }, d));
    if (!disks.length) {
      app.device = null; await app.refreshState();
      clear(tableBox); tableBox.append(h('div.note', 'No OP-1 disk found. Connect the OP-1, press shift + COM, choose DISK, then Rescan. The librarian only reads until you back up.'));
      clear(tree); return;
    }
    if (!app.device || !disks.includes(app.device)) app.device = diskSel.value;
    await app.refreshState();
    try {
      scan = await api.get('/api/device/scan', { root: app.device });
      undo = await api.get('/api/device/undo', { root: app.device });
    } catch (e) { toast(e.message, 'err'); return; }
    renderTree(); renderFilters(); renderTable(); renderUndo();
    // The selection follows the file (a rename or an edit moves it; a
    // delete or an undo may remove it).
    const cur = selectedRel && [...scan.synth, ...scan.drum, ...scan.tape, ...scan.album].find(x => x.rel === selectedRel);
    if (cur) await select(cur); else { selectedRel = null; clear(detail); }
  }
  diskSel.addEventListener('change', () => { app.device = diskSel.value; load(); });

  function renderTree() {
    clear(tree);
    for (const top of ['synth', 'drum', 'tape', 'album']) {
      tree.append(h('button', { class: folder === top ? 'on' : '', onclick: () => { folder = top; renderTree(); renderTable(); } }, h('span', top), h('span.n', scan.counts[top])));
      for (const sub of scan.folders[top] || []) {
        const n = scan[top].filter(e => e.rel.startsWith(sub + '/')).length;
        tree.append(h('button.sub', { class: folder === sub ? 'on' : '', onclick: () => { folder = sub; renderTree(); renderTable(); } }, h('span', sub.split('/').slice(1).join('/')), h('span.n', n)));
      }
    }
  }

  function entries() {
    const top = folder.split('/')[0];
    const list = (scan[top] || []).filter(e => folder === top ? true : e.rel.startsWith(folder + '/'));
    return list.filter(e => {
      const s = e.summary;
      if (filters.has('custom') && !(s && s.custom.length)) return false;
      if (filters.has('sampler') && !(s && s.type === 'sampler')) return false;
      if (filters.has('drum') && !(s && s.type === 'drum')) return false;
      if (filters.has('dbox') && !(s && s.type === 'dbox')) return false;
      for (const f of filters) if (f.startsWith('engine:') && !(s && s.type === f.slice(7))) return false;
      if (query) {
        const hay = [e.name, s?.engine?.name, s?.effect?.label, s?.lfo?.type, s?.type].join(' ').toLowerCase();
        if (!hay.includes(query)) return false;
      }
      return true;
    });
  }

  function renderFilters() {
    clear(filterBox);
    const engines = [...new Set([...scan.synth, ...scan.drum].map(e => e.summary?.type).filter(Boolean))].sort();
    const chip = (key, label, cls) => {
      const c = h('span.chip' + (filters.has(key) ? '.on' : '') + (cls ? '.' + cls : ''), { onclick: () => { filters.has(key) ? filters.delete(key) : filters.add(key); renderFilters(); renderTable(); } }, label);
      return c;
    };
    filterBox.append(chip('custom', 'needs custom fw', filters.has('custom') ? 'orange' : ''), chip('sampler', 'sampler'), chip('drum', 'drum'), chip('dbox', 'dbox'));
    for (const e of engines) if (!['sampler', 'drum', 'dbox'].includes(e)) filterBox.append(chip('engine:' + e, e));
  }

  function renderTable() {
    clear(tableBox);
    const list = entries();
    const top = folder.split('/')[0];
    if (top === 'tape' || top === 'album') {
      const sorted = fileSort.sort(list, { file: e => e.rel, size: e => e.size, date: e => e.mtime });
      const t = h('table', h('thead', h('tr', fileSort.th('file', 'file'), fileSort.th('size', 'size'), fileSort.th('date', 'date'))), h('tbody', sorted.map(e => h('tr', { onclick: () => select(e) }, h('td.name', e.rel), h('td.num', fmtBytes(e.size)), h('td.num', fmtDate(e.mtime))))));
      tableBox.append(t);
      return;
    }
    const tagOf = e => e.summary && e.summary.custom.length ? 'custom fw' : (e.problems && e.problems.length ? 'invalid' : '');
    const sorted = presetSort.sort(list, {
      name: e => e.name.replace(/\.aif$/i, ''), engine: e => e.summary?.engine?.name, effect: e => e.summary?.effect?.label,
      lfo: e => e.summary?.lfo?.type, oct: e => e.summary?.octave, tag: tagOf, size: e => e.size, date: e => e.mtime,
    });
    const t = h('table',
      h('thead', h('tr', presetSort.th('name', 'name'), presetSort.th('engine', 'engine'), presetSort.th('effect', 'effect'), presetSort.th('lfo', 'lfo'), presetSort.th('oct', 'oct'), presetSort.th('', 'tag'), presetSort.th('size', 'size'), presetSort.th('date', 'date'))),
      h('tbody', sorted.map(e => {
        const s = e.summary;
        const row = h('tr', { class: e.rel === selectedRel ? 'selected' : '', onclick: () => select(e) },
          h('td.name', e.name.replace(/\.aif$/i, '')),
          h('td', s ? [h('i.dot.' + (s.kind === 'drum' ? 'orange' : 'blue'), { style: { marginRight: '6px' } }), s.engine.name] : h('span.bad', e.error || '?')),
          h('td', s ? (s.effect.label + (s.effect.active ? '' : ' (off)')) : ''),
          h('td', s ? s.lfo.type + (s.lfo.active ? '' : ' (off)') : ''),
          h('td.num', s ? s.octave : ''),
          h('td', s && s.custom.length ? h('span.tag.new', 'custom fw') : (e.problems && e.problems.length ? h('span.tag.orange', 'invalid') : '')),
          h('td.num', fmtBytes(e.size)), h('td.num', fmtDate(e.mtime)));
        return row;
      })));
    tableBox.append(t);
    if (!list.length) tableBox.append(h('div.note', 'Nothing here.'));
  }

  async function select(e) {
    selectedRel = e.rel;
    renderTable();
    clear(detail);
    const top = e.rel.split('/')[0];
    if (top === 'tape' || top === 'album') {
      detail.append(h('h3', e.name), h('dl.kv', h('dt', 'path'), h('dd', e.rel), h('dt', 'size'), h('dd', fmtBytes(e.size))),
        h('div.row', h('button.small', { onclick: () => doExport() }, 'Export'), h('button.small.danger', { onclick: () => doDelete() }, 'Delete')));
      return;
    }
    let full;
    try { full = await api.get('/api/device/preset', { root: app.device, rel: e.rel }); } catch (err) { toast(err.message, 'err'); return; }
    const s = full.summary;
    detail.append(h('div.row', h('h3', e.name.replace(/\.aif$/i, '')), s && s.custom.length ? h('span.tag.new', 'custom fw') : null));
    if (!s) { detail.append(h('div.note.warn', full.error || 'unreadable')); return; }
    detail.append(h('dl.kv', h('dt', 'engine'), h('dd', s.engine.name + (s.engine.multi ? ' · ' + s.engine.multi.model_name + ' · poly ' + s.engine.multi.polyphony_name : '')),
      h('dt', 'effect'), h('dd', s.effect.label + (s.effect.active ? '' : ' (off)')), h('dt', 'lfo'), h('dd', s.lfo.type + (s.lfo.active ? '' : ' (off)')),
      h('dt', 'octave'), h('dd', String(s.octave)), h('dt', 'file'), h('dd', e.rel + ' · ' + fmtBytes(e.size) + (full.audio ? ' · ' + full.audio.seconds.toFixed(2) + ' s audio' : ''))));
    if (s.kind === 'synth') {
      const bars = h('div.stack', { style: { gap: '6px' } });
      for (const k of s.engine.knobs) bars.append(h('div.knob.readonly', h('i.dot.' + k.color), h('b', k.name), h('div.bar.' + k.color, h('i', { style: { width: ((k.value + 32768) / 65535 * 100) + '%' } })), h('span.val', k.value), h('span')));
      detail.append(bars);
    }
    if (s.custom.length) detail.append(h('div.note.warn', 'Needs this firmware (' + s.custom.join(', ') + '). On stock, switch it to the stock engine or effect first.'));
    if (full.problems && full.problems.length) detail.append(h('div.note.warn', 'Invalid: ' + full.problems.join('; ')));
    detail.append(h('div.row',
      h('button.primary.small', { onclick: () => openEditor(e, full) }, 'Edit'),
      h('button.small', { onclick: () => doDuplicate() }, 'Duplicate'),
      h('button.small', { onclick: () => doRename() }, 'Rename'),
      h('button.small', { onclick: () => doMove() }, 'Move'),
      h('button.small', { onclick: () => doExport() }, 'Export'),
      h('button.small.danger', { onclick: () => doDelete() }, 'Delete')));
  }

  function openEditor(e, full) {
    if (editor && editor.destroy) editor.destroy();
    const box = h('div.card');
    editor = makeEditor({
      patch: full.patch, peaks: full.peaks, audio: full.audio,
      audioUrl: full.has_audio ? api.url('/api/device/audio', { root: app.device, rel: e.rel }) : null,
      onsave: async (p) => {
        try { const r = await op({ op: 'save', rel: e.rel, patch: p }); selectedRel = r.entry.to || e.rel; toast('Saved: ' + r.entry.text, 'ok'); box.remove(); editor = null; await load(); } catch (err) { toast(err.message, 'err'); }
      },
    });
    box.append(h('div.row.between', h('h3', 'Edit ' + e.name), h('button.small', { onclick: () => { editor.destroy(); box.remove(); editor = null; } }, 'Close')), editor);
    main.querySelector('.editor-slot').replaceChildren(box);
    box.scrollIntoView({ behavior: 'smooth', block: 'start' });
  }

  async function op(body) {
    const r = await api.post('/api/device/op', Object.assign({ root: app.device }, body));
    undo = { log: r.log, backed_up: r.backed_up };
    renderUndo();
    return r;
  }
  function requireSelection() { const e = selectedRel && [...scan.synth, ...scan.drum, ...scan.tape, ...scan.album].find(x => x.rel === selectedRel); if (!e) toast('select a file first', 'err'); return e; }
  async function guarded(fn) { try { await fn(); await load(); } catch (e) { toast(e.message, 'err'); } }

  async function doBackup() {
    backupBtn.disabled = true; backupBtn.textContent = 'Backing up…';
    try {
      const r = await api.post('/api/device/backup', { root: app.device });
      toast('Backed up ' + r.files + ' files to ' + r.backup, 'ok');
      undo.backed_up = true; renderUndo();
      app.state.backups = r.backups;
    } catch (e) { toast(e.message, 'err'); }
    backupBtn.disabled = false; backupBtn.textContent = 'Back up the OP-1';
  }
  async function doRename() {
    const e = requireSelection(); if (!e) return;
    const name = await promptDialog('Rename', 'New name (a preset\'s name inside follows its file name)', e.name.replace(/\.aif$/i, ''));
    if (name && name !== e.name.replace(/\.aif$/i, '')) guarded(async () => { const r = await op({ op: 'rename', rel: e.rel, name }); selectedRel = r.entry.to; });
  }
  async function doMove() {
    const e = requireSelection(); if (!e) return;
    const top = e.rel.split('/')[0];
    const target = await promptDialog('Move', 'Folder under ' + top + '/ (empty = the top folder)', folder.startsWith(top) && folder !== top ? folder.slice(top.length + 1) : '');
    if (target === null) return;
    guarded(async () => { const r = await op({ op: 'move', rel: e.rel, folder: target ? top + '/' + target : top }); selectedRel = r.entry.to; });
  }
  async function doDuplicate() { const e = requireSelection(); if (!e) return; const name = await promptDialog('Duplicate', 'Name of the copy', e.name.replace(/\.aif$/i, '') + ' copy.aif'); if (name) guarded(async () => { const r = await op({ op: 'duplicate', rel: e.rel, name }); selectedRel = r.entry.path; }); }
  async function doDelete() { const e = requireSelection(); if (!e) return; if (await confirmDialog('Delete ' + e.name + '?', 'A copy is kept in the undo store; Undo brings it back.', 'Delete', true)) guarded(async () => { await op({ op: 'delete', rel: e.rel }); selectedRel = null; }); }
  async function doNewFolder() { const top = folder.split('/')[0]; if (!['synth', 'drum'].includes(top)) { toast('folders go under synth or drum', 'err'); return; } const name = await promptDialog('New folder', 'Folder name under ' + top + '/'); if (name) guarded(() => op({ op: 'mkdir', folder: top + '/' + name })); }
  async function doImport() {
    const top = folder.split('/')[0];
    if (!['synth', 'drum'].includes(top)) { toast('choose a synth or drum folder first', 'err'); return; }
    const source = await browseDialog('Import a preset into ' + folder, app.state.settings.output_dir, 'file', (n) => /\.aif$/i.test(n));
    if (source) guarded(() => op({ op: 'import', source, folder }));
  }
  async function doExport() {
    const e = requireSelection(); if (!e) return;
    const dest = await browseDialog('Export ' + e.name + ' to a folder', app.state.settings.output_dir, 'dir');
    if (dest) { try { const r = await op({ op: 'export', rel: e.rel, dest }); toast('Exported to ' + r.exported, 'ok'); } catch (err) { toast(err.message, 'err'); } }
  }
  async function doCopyFactory() {
    let plan;
    try { plan = await api.get('/api/device/factory-plan', { root: app.device }); } catch (e) { toast(e.message, 'err'); return; }
    const folders = {};
    for (const f of plan.files) { const top = f.rel.split('/')[0]; (folders[top] = folders[top] || new Map()).set(f.folder, (folders[top].get(f.folder) || 0) + (f.exists ? 0 : 1)); }
    const list = h('div.stack', { style: { gap: '4px', fontSize: '0.85rem' } });
    for (const top of ['synth', 'drum']) for (const [fld, n] of folders[top] || []) list.append(h('div.row.between', h('span.mono', top + '/' + fld + '/'), h('span.dim', n + ' new')));
    const ok = await new Promise((resolve) => {
      const m = modal([
        h('h3', 'Copy the factory presets to the OP-1'),
        h('p.dim', 'Every stock factory preset becomes a user preset on the disk, in a folder named after its engine, with its factory audio (sampler and drum kits). You can then rename, change or delete them in disk mode, and the firmware\'s own copies stay as they are: to drop those, delete the rows in the Factory tab and build. Files already on the disk are kept. One Undo removes everything this writes.'),
        list,
        h('div.row.between', h('span.dim', plan.existing ? plan.existing + ' already on the disk, skipped' : 'nothing on the disk yet'), h('span.mono', plan.new + ' files · ' + fmtBytes(plan.total))),
        plan.problems.length ? h('div.note.warn', plan.problems.length + ' row(s) skipped: ' + plan.problems.map(x => x.name + ': ' + x.problem).join('; ')) : null,
        h('div.row', { style: { justifyContent: 'flex-end' } },
          h('button', { onclick: () => { m.close(); resolve(false); } }, 'Cancel'),
          h('button.primary', { disabled: !plan.new, onclick: () => { m.close(); resolve(true); } }, 'Copy ' + plan.new + ' presets')),
      ], { narrow: true });
    });
    if (!ok) return;
    factoryBtn.disabled = true; factoryBtn.textContent = 'Copying…';
    try {
      const r = await op({ op: 'factory' });
      toast(r.entry.text + (r.entry.skipped.length ? ' (' + r.entry.skipped.length + ' already there)' : ''), 'ok');
      await load();
    } catch (e) { toast(e.message, 'err'); }
    factoryBtn.disabled = !undo.backed_up; factoryBtn.textContent = 'Copy factory presets to the OP-1';
  }
  async function doUndo() { guarded(async () => { const r = await op({ op: 'undo' }); toast('Undid: ' + r.entry.text, 'ok'); }); }

  function renderUndo() {
    clear(undoStrip);
    writeNote.textContent = undo.backed_up ? 'backed up this session: changes allowed' : 'read-only until you back up';
    for (const b of [importBtn, folderBtn, factoryBtn]) b.disabled = !undo.backed_up;
    const last = undo.log[undo.log.length - 1];
    undoStrip.append(last ? h('span', last.text + ' · ' + last.when.replace('T', ' ')) : h('span.dim', 'no changes yet'), undoBtn);
    undoBtn.disabled = !undo.log.length;
  }

  clear(main);
  main.append(h('section.view',
    h('header', h('h2', 'Librarian'), h('p', 'The presets on your OP-1 in disk mode: browse, back up, organise, edit and preview. Nothing is written to the OP-1 before a backup, and every change can be undone.')),
    h('div.row', h('label.field', 'OP-1 disk', diskSel), refreshBtn, backupBtn, importBtn, exportBtn, folderBtn, factoryBtn, writeNote),
    h('div.cols.wide-side',
      h('div.stack', tree),
      h('div.stack', search, filterBox, tableBox, undoStrip),
      detail),
    h('div.editor-slot')));
  await load();
  return { destroy() { if (editor && editor.destroy) editor.destroy(); } };
}
