// The factory preset editor: the rows inside the firmware, edited as a preset set that the builder applies.
import { api, app, h, clear, toast, confirmDialog, promptDialog, browseDialog, modal, sorter } from './app.js';
import { makeEditor } from './editor.js';

const STORAGE = 'op1app.factory.set';

export async function factoryView(main) {
  let data = await api.get('/api/factory');
  let set = { name: 'my set', changes: [] };
  try { const saved = JSON.parse(localStorage.getItem(STORAGE)); if (saved && Array.isArray(saved.changes)) set = saved; } catch (e) { /* ignore */ }
  let table = 'synth_presets';
  let folder = data.synth_presets_folders[0]?.folder || 'cluster';
  let selectedId = null;
  let editor = null;
  const rowSort = sorter('id', 1, () => renderRows());

  const budgetBox = h('div.stack', { style: { gap: '6px' } });
  const folders = h('div.tree');
  const rowsBox = h('div.tablebox.scrolly');
  const setName = h('span.mono');
  const setInfo = h('span.dim');
  const drawer = h('div.card.sticky');
  const problems = h('div.stack');

  // ---- the effective rows: stock rows + the set's changes ----
  function effectiveRows(t) {
    const rows = data[t].map(r => Object.assign({}, r, { state: 'stock' }));
    let nextId = Math.max(0, ...rows.map(r => r.id)) + 1;
    // Rows a feature inserts at build time (the iter presets): shown, not editable.
    for (const m of data[t + '_mods'] || []) rows.push(Object.assign({}, m, { state: 'mod' }));
    for (const c of set.changes) {
      if (c.table !== t) continue;
      if (c.op === 'replace') { const r = rows.find(x => x.id === c.id); if (r) { r.patch = c.patch; r.folder = c.folder || r.folder; r.state = 'changed'; r.summary = null; } }
      else if (c.op === 'append') { rows.push({ id: c._id || (c._id = nextId++), folder: c.folder, patch: c.patch, state: 'added', summary: null }); }
      else if (c.op === 'delete') { const i = rows.findIndex(x => x.id === c.id); if (i >= 0) rows[i].state = 'deleted'; }
    }
    return rows;
  }

  function counts() {
    const s = effectiveRows('synth_presets').filter(r => r.state !== 'deleted');
    const d = effectiveRows('drum_presets').filter(r => r.state !== 'deleted');
    const sampler = s.filter(r => r.folder === 'sampler' || r.patch?.type === 'sampler').length;
    return { synth: s.length, sampler, drum: d.length, nonSampler: s.length - sampler, total: s.length + d.length };
  }

  function renderBudget() {
    clear(budgetBox);
    const c = counts();
    const L = app.state.image.limits;
    const bar = (label, n, limit) => {
      const frac = Math.min(1, n / limit);
      return h('div', h('div.row.between', h('span.eyebrow', label), h('span.mono.dim', n + ' of ' + limit)),
        h('div.meter', h('i', { class: n > limit ? 'warn' : '', style: { width: (frac * 100) + '%' } }), h('b', { style: { left: ((limit - 5) / limit * 100) + '%' } })));
    };
    budgetBox.append(bar('non-sampler synth presets', c.nonSampler, L.non_sampler), bar('sampler presets', c.sampler, L.sampler), bar('all presets', c.total, L.total));
    budgetBox.append(h('div.note.warn', { style: { fontSize: '0.85rem' } }, 'A bad or extra factory row stops the OP-1 from booting (recoverable by flashing a good package). Every row is validated here and again before packaging. The iter folder is the iter presets feature: its rows are added at build time only with that feature on, and are counted here.'));
  }

  function renderFolders() {
    clear(folders);
    for (const t of ['synth_presets', 'drum_presets']) {
      folders.append(h('div.eyebrow', { style: { padding: '8px 10px 2px' } }, t === 'synth_presets' ? 'synth' : 'drum'));
      const rows = effectiveRows(t).filter(r => r.state !== 'deleted');
      const names = [...new Set(rows.map(r => r.folder))];
      for (const f of names) {
        const n = rows.filter(r => r.folder === f).length;
        const changed = rows.some(r => r.folder === f && r.state !== 'stock' && r.state !== 'mod') || set.changes.some(c => c.table === t && c.op === 'delete' && data[t].find(x => x.id === c.id)?.folder === f);
        folders.append(h('button', { class: (t === table && f === folder) ? 'on' : '', onclick: () => { table = t; folder = f; selectedId = null; renderFolders(); renderRows(); clear(drawer); } },
          h('span', f, changed ? h('span.tag.orange', { style: { marginLeft: '8px' } }, 'edited') : null), h('span.n', n)));
      }
    }
  }

  function renderRows() {
    clear(rowsBox);
    const all = effectiveRows(table).filter(r => r.folder === folder);
    const rows = rowSort.sort(all, {
      id: r => r.id, name: r => r.patch?.name, engine: r => r.patch?.type, effect: r => r.summary ? r.summary.effect.label : r.patch?.fx_type,
      lfo: r => r.patch?.lfo_type, oct: r => r.patch?.octave,
      tag: r => (r.summary ? r.summary.custom.length > 0 : customOf(r.patch || {})) ? 'custom fw' : r.state === 'mod' ? 'iter' : r.state === 'stock' ? '' : r.state,
    });
    const t = h('table',
      h('thead', h('tr', rowSort.th('id', 'id'), rowSort.th('name', 'name'), rowSort.th('engine', 'engine'), rowSort.th('effect', 'effect'), rowSort.th('lfo', 'lfo'), rowSort.th('oct', 'oct'), rowSort.th('', 'tag'), h('th', ''))),
      h('tbody', rows.map(r => {
        const p = r.patch || {};
        const custom = r.summary ? r.summary.custom.length > 0 : customOf(p);
        const row = h('tr', { class: r.id === selectedId ? 'selected' : '', style: r.state === 'deleted' ? { opacity: 0.4, textDecoration: 'line-through' } : null, onclick: () => select(r) },
          h('td.num', r.id), h('td.name', p.name), h('td', p.type),
          h('td', r.summary ? r.summary.effect.label : (p.fx_type || '')), h('td', p.lfo_type), h('td.num', p.octave),
          h('td', custom ? h('span.tag.new', 'custom fw') : null, r.state === 'mod' ? h('span.tag', { style: { marginLeft: '6px' } }, 'iter presets feature') : r.state !== 'stock' ? h('span.tag.orange', { style: { marginLeft: '6px' } }, r.state) : null),
          h('td', h('div.row', { style: { gap: '4px' } },
            r.state === 'mod' ? null : h('button.small', { onclick: (e) => { e.stopPropagation(); openEditor(r, 'replace'); } }, 'Edit'),
            h('button.small', { onclick: (e) => { e.stopPropagation(); openEditor(r, 'append'); } }, 'Copy as new'),
            r.state === 'mod' ? null : r.state === 'deleted' ? h('button.small', { onclick: (e) => { e.stopPropagation(); undelete(r); } }, 'Restore')
              : h('button.small.danger', { onclick: (e) => { e.stopPropagation(); deleteRow(r); } }, 'Delete'))));
        return row;
      })));
    rowsBox.append(t);
  }

  function customOf(p) {
    const fx = p.fx_params || [];
    return ((fx[4] & 0xf000) === 0x6000) || p.type === 'iter' || p.fx_type === 'filter' || (p.type === 'phase' && Math.floor(((p.knobs || [])[6] + 2048) / 4096) === 1);
  }

  async function select(r) {
    selectedId = r.id;
    renderRows();
    clear(drawer);
    const p = r.patch;
    drawer.append(h('div.row', h('h3', p.name), h('span.tag', r.folder), r.state === 'mod' ? h('span.tag', 'iter presets feature') : r.state !== 'stock' ? h('span.tag.orange', r.state) : null));
    const s = r.summary || (await api.post('/api/validate', { patch: p })).summary;
    if (s) {
      drawer.append(h('dl.kv', h('dt', 'engine'), h('dd', s.engine.name + (s.engine.multi ? ' · ' + s.engine.multi.model_name + ' · poly ' + s.engine.multi.polyphony_name : '')),
        h('dt', 'effect'), h('dd', s.effect.label + (s.effect.active ? '' : ' (off)')), h('dt', 'lfo'), h('dd', s.lfo.type + (s.lfo.active ? '' : ' (off)')), h('dt', 'octave'), h('dd', String(s.octave))));
      if (s.kind === 'synth') {
        const bars = h('div.stack', { style: { gap: '6px' } });
        for (const k of s.engine.knobs) bars.append(h('div.knob.readonly', h('i.dot.' + k.color), h('b', k.name), h('div.bar.' + k.color, h('i', { style: { width: ((k.value + 32768) / 65535 * 100) + '%' } })), h('span.val', k.value), h('span')));
        drawer.append(bars);
      }
      if (s.custom.length) drawer.append(h('div.note.warn', 'Needs this firmware: ' + s.custom.join(', ') + '. On stock firmware a factory row like this may stop the boot.'));
    }
    const kind = table === 'synth_presets' ? 'synth' : 'drum';
    if (r.state === 'stock' && (p.type === 'sampler' || p.type === 'drum')) {
      const audio = h('audio', { controls: true, src: api.url('/api/factory/audio', { kind, name: p.name }), style: { width: '100%' } });
      drawer.append(h('div.stack', h('span.eyebrow', 'factory sample'), audio));
    }
    if (r.state === 'mod') {
      drawer.append(h('div.note', 'Added at build time by the iter presets feature (op1repacker\'s set), as it ships. To change one, copy it as a new row and edit the copy.'));
      drawer.append(h('div.row', h('button.small', { onclick: () => openEditor(r, 'append') }, 'Copy as new')));
      return;
    }
    drawer.append(h('div.row', h('button.primary.small', { onclick: () => openEditor(r, 'replace') }, 'Edit'), h('button.small', { onclick: () => openEditor(r, 'append') }, 'Copy as new')));
  }

  async function openEditor(r, mode) {
    if (editor && editor.destroy) editor.destroy();
    const kind = table === 'synth_presets' ? 'synth' : 'drum';
    let peaks = null, audio = null, audioUrl = null;
    const p = r.patch;
    if (r.state === 'stock' && (p.type === 'sampler' || p.type === 'drum')) {
      try { const pk = await api.get('/api/factory/peaks', { kind, name: p.name }); peaks = pk.peaks; audio = pk; audioUrl = api.url('/api/factory/audio', { kind, name: p.name }); } catch (e) { /* no audio */ }
    }
    const box = h('div.card');
    editor = makeEditor({
      patch: p, folder: r.folder, peaks, audio, audioUrl, saveLabel: mode === 'replace' ? 'Replace row ' + r.id : 'Append as a new row',
      onsave: (np, fld) => {
        if (mode === 'replace') {
          set.changes = set.changes.filter(c => !(c.table === table && c.op === 'replace' && c.id === r.id) && !(c.table === table && c.op === 'append' && c._id === r.id));
          if (r.state === 'added') set.changes.push({ table, op: 'append', folder: fld || r.folder, patch: np, _id: r.id });
          else set.changes.push({ table, op: 'replace', id: r.id, folder: fld || r.folder, patch: np });
        } else {
          set.changes.push({ table, op: 'append', folder: fld || r.folder, patch: np });
        }
        saveLocal(); box.remove(); editor = null; renderAll(); toast(mode === 'replace' ? 'Row ' + r.id + ' replaced in the set' : 'Row appended to the set', 'ok');
      },
    });
    box.append(h('div.row.between', h('h3', (mode === 'replace' ? 'Edit row ' + r.id : 'New row from ' + p.name)), h('button.small', { onclick: () => { editor.destroy(); box.remove(); editor = null; } }, 'Close')), editor);
    main.querySelector('.editor-slot').replaceChildren(box);
    box.scrollIntoView({ behavior: 'smooth', block: 'start' });
  }

  function deleteRow(r) {
    if (r.state === 'added') set.changes = set.changes.filter(c => c._id !== r.id);
    else set.changes.push({ table, op: 'delete', id: r.id });
    saveLocal(); renderAll();
  }
  function undelete(r) { set.changes = set.changes.filter(c => !(c.table === table && c.op === 'delete' && c.id === r.id)); saveLocal(); renderAll(); }

  async function addFromFile() {
    const source = await browseDialog('Add a preset file as a factory row', app.state.settings.output_dir, 'file', (n) => /\.aif$/i.test(n));
    if (!source) return;
    try {
      const f = await api.get('/api/file/preset', { path: source });
      const kind = f.summary.kind;
      const t = kind === 'synth' ? 'synth_presets' : 'drum_presets';
      const fld = await promptDialog('Folder', 'Factory folder for the new row', t === table ? folder : (kind === 'synth' ? 'multi' : 'drum'));
      if (fld === null) return;
      set.changes.push({ table: t, op: 'append', folder: fld, patch: f.patch });
      saveLocal(); renderAll(); toast('Added ' + f.patch.name + ' to the set', 'ok');
    } catch (e) { toast(e.message, 'err'); }
  }

  async function checkSet() {
    clear(problems);
    try {
      const r = await api.post('/api/presets/preview', { data: set });
      if (r.problems.length) for (const p of r.problems) problems.append(h('div.note.warn', p));
      else problems.append(h('div.note.ok', '✓ the set applies cleanly: ' + r.counts.synth + ' synth (' + r.counts.sampler + ' sampler), ' + r.counts.drum + ' drum rows, integrity ok'));
    } catch (e) { problems.append(h('div.note.warn', e.message)); }
  }

  async function saveSet() {
    const name = await promptDialog('Save the preset set', 'Name (saved in app/presets/)', set.name);
    if (!name) return;
    try { const r = await api.post('/api/presets/set', { name, data: set }); set.name = name; saveLocal(); app.state.preset_sets = r.sets; toast('Saved ' + r.path, 'ok'); renderAll(); } catch (e) { toast(e.message, 'err'); }
  }
  async function loadSet() {
    const sets = (await api.get('/api/presets/sets')).sets;
    if (!sets.length) { toast('no saved sets', 'err'); return; }
    const list = h('div.browse');
    const m = modal([h('h3', 'Load a preset set'), list], { narrow: true });
    for (const s of sets) list.append(h('button', { onclick: async () => { try { set = await api.get('/api/presets/set', { file: s.file }); set.name = set.name || s.name; saveLocal(); m.close(); renderAll(); } catch (e) { toast(e.message, 'err'); } } }, s.name + ' (' + s.changes + ' changes)'));
  }
  async function resetSet() { if (await confirmDialog('Reset to stock?', 'Drops every change in the current set (saved files stay).', 'Reset', true)) { set = { name: 'my set', changes: [] }; saveLocal(); renderAll(); } }
  function saveLocal() { localStorage.setItem(STORAGE, JSON.stringify(set)); }

  function renderAll() {
    setName.textContent = set.name;
    setInfo.textContent = set.changes.length + ' change' + (set.changes.length === 1 ? '' : 's') + ' from stock';
    renderBudget(); renderFolders(); renderRows(); checkSet();
  }

  clear(main);
  main.append(h('section.view',
    h('header', h('h2', 'Factory presets'), h('p', 'The presets that ship inside the firmware. Edit a row in place, append new ones or delete some; the changes are a preset set the builder applies when you build. Factory audio for sampler and drum rows plays from the stock files.')),
    h('div.card.pad-sm', h('div.row.between',
      h('div.row', h('span.eyebrow', 'preset set'), setName, setInfo),
      h('div.row', h('button.small', { onclick: addFromFile }, 'Add from a .aif file'), h('button.small', { onclick: saveSet }, 'Save set'), h('button.small', { onclick: loadSet }, 'Load set'), h('button.small.danger', { onclick: resetSet }, 'Reset to stock'),
        h('a.btn.small', { href: '#builder/' + encodeURIComponent((set.name || 'my set') + '.json') }, 'Build with this set →'))),
      problems),
    h('div.cols.wide-side', h('div.stack', folders), h('div.stack', budgetBox, rowsBox), drawer),
    h('div.editor-slot')));
  renderAll();
  return { destroy() { if (editor && editor.destroy) editor.destroy(); } };
}
