// The builder: tick modules and mods, build, verify, next step.
import { api, app, h, clear, toggle, toast } from './app.js';

const STORAGE = 'op1app.builder.selected';

export async function builderView(main) {
  const data = await api.get('/api/features');
  const features = data.features;
  const byId = Object.fromEntries(features.map(f => [f.id, f]));
  let selected = new Set(features.filter(f => f.kind === 'module' || ['iter', 'presets-iter', 'gfx-iter-lab', 'filter', 'gfx-tape-invert'].includes(f.id)).map(f => f.id));
  try { const saved = JSON.parse(localStorage.getItem(STORAGE)); if (Array.isArray(saved)) selected = new Set(saved.filter(id => byId[id])); } catch (e) { /* ignore */ }

  const settings = app.state.settings;
  let resolved = null;
  let polling = null;

  // ---- the grid ----
  const grid = h('div.stack');
  const cards = {};
  const groups = data.groups;
  for (const g of groups) {
    const inGroup = features.filter(f => f.group === g);
    const cats = [...new Set(inGroup.map(f => f.category))];
    const groupEl = h('div.group', h('div.ghead', h('h2', data.group_titles[g])));
    for (const cat of cats) {
      const inCat = inGroup.filter(f => f.category === cat);
      const chain = inCat.find(f => f.chain)?.chain;
      const head = h('div.ghead', h('span.eyebrow', data.category_titles[cat] || cat), chain ? chainChips(chain, inCat) : null);
      groupEl.append(head);
      const mgrid = h('div.mgrid');
      // sub-options (presets-iter, gfx-iter-lab) nest under iter; the cwo graphics are one card.
      const subs = inCat.filter(f => f.requires.length);
      const cwo = inCat.filter(f => f.id.startsWith('gfx-cwo-'));
      for (const f of inCat) {
        if (f.requires.length || (cwo.includes(f) && f !== cwo[0])) continue;
        const card = f === cwo[0] ? cwoCard(cwo) : moduleCard(f, subs.filter(s => s.requires.includes(f.id)));
        mgrid.append(card);
      }
      groupEl.append(mgrid);
    }
    grid.append(groupEl);
  }

  function chainChips(chain, inCat) {
    const parts = chain.split('→').map(s => s.trim());
    const el = h('div.chain');
    parts.forEach((p, i) => {
      if (i) el.append(h('span.arr', '→'));
      const isNew = inCat.some(f => f.name.toLowerCase() === p.toLowerCase().replace(/^.*: /, ''));
      el.append(h('span.e' + (isNew ? '.new' : ''), p.replace(/^multi: /, '')));
    });
    return el;
  }

  function thumb(f) {
    const name = Object.keys(f.screens)[0] || (f.id === 'gfx-iter-lab' ? 'iter-lab.svg' : null);
    if (!name) return h('div.screen.ph', f.id === 'iter' ? 'iter' : f.name);
    return h('img.screen', { src: '/api/screen?name=' + name, alt: f.name + ' screen' });
  }

  function moduleCard(f, subs) {
    const t = toggle(selected.has(f.id), (on) => set(f.id, on));
    const warn = h('div.warn');
    const body = h('div.body',
      h('div.title', h('h4', f.name), f.tag ? h('span.tag.new', f.tag) : null, f.cpu !== 'none' ? h('span.cpu' + (f.cpu === 'heavy' ? '.heavy' : ''), f.cpu) : null, t),
      h('div.what', f.description), warn);
    if (subs.length) {
      const box = h('div.subopts');
      for (const s of subs) {
        const st = toggle(selected.has(s.id), (on) => set(s.id, on));
        cards[s.id] = { toggle: st };
        box.append(h('div.row', st, h('span', s.name), h('span.dim', '· ' + s.description)));
      }
      body.append(box);
    }
    const card = h('article.card.mcard' + (selected.has(f.id) ? '.on' : ''), thumb(f), body);
    cards[f.id] = { card, toggle: t, warn };
    return card;
  }

  function cwoCard(options) {
    const current = options.find(o => selected.has(o.id))?.id || 'none';
    const sel = h('select');
    sel.append(h('option', { value: 'none' }, 'cow (stock)'));
    for (const o of options) sel.append(h('option', { value: o.id, selected: o.id === current }, o.name.replace('cwo: ', '')));
    sel.addEventListener('change', () => { for (const o of options) selected.delete(o.id); if (sel.value !== 'none') selected.add(sel.value); update(); });
    const card = h('article.card.mcard', h('div.screen.ph', 'cwo'),
      h('div.body', h('div.title', h('h4', 'cwo graphics')), h('div.what', 'The animal on the cwo screen.'), h('div.row', sel)));
    cards['gfx-cwo'] = { card, select: sel };
    return card;
  }

  function set(id, on) {
    if (on) selected.add(id); else {
      selected.delete(id);
      for (const f of features) if (f.requires.includes(id)) selected.delete(f.id);
    }
    if (on) for (const r of byId[id].requires) selected.add(r);
    update();
  }

  // ---- the build panel ----
  const stockRow = h('div.checks');
  const chips = h('div.row');
  const warnings = h('div.stack');
  const meter = h('div.meter', h('i'), h('b'));
  const meterText = h('div.dim', { style: { fontSize: '0.85rem' } });
  const outName = h('input.mono', { type: 'text', value: 'op1_246_custom.op1' });
  const setSelect = h('select');
  const buildBtn = h('button.primary.big', { onclick: startBuild }, 'Build');
  const results = h('div.stack.hidden');
  const panel = h('div.card.sticky',
    h('h3', 'Build'),
    stockRow,
    h('div', h('div.eyebrow', 'Chosen'), chips),
    warnings,
    h('div.stack', { style: { gap: '6px' } }, h('div.eyebrow', 'Factory preset budget'), meter, meterText),
    h('label.field', 'Output file (in ' + settings.output_dir + ')', outName),
    h('label.field', 'Factory preset set', setSelect),
    buildBtn,
    results,
    h('div.note', h('b', 'Next step after a build: '), 'copy the file to the OP-1 in te-boot upload mode (switch off, hold COM while switching on, "1. Upload firmware", copy, eject). The app never writes to the OP-1 itself.'),
    h('div.note', { style: { color: 'var(--fg-faint)' } }, h('b', 'Recovery: '), 'hold COM at power-on and install the stock op1_246.op1. Before going back to stock, set every saved sound and drum kit to the first effect of its category and save it, or stock may not start.'),
  );

  async function loadStock() {
    clear(stockRow);
    stockRow.append(h('div', h('span.pending', '○'), 'checking the stock firmware…'));
    try {
      const s = await api.get('/api/stock');
      clear(stockRow);
      stockRow.append(h('div', h('span' + (s.ok ? '.ok' : '.bad'), s.ok ? '✓' : '✗'),
        h('span', s.ok ? 'stock op1_246.op1 verified (OP1_vdk.ldr sha256 ' + s.vdk_sha256.slice(0, 8) + '…)' : 'stock firmware: ' + s.problems.join('; ') + ' (see Settings)')));
      buildBtn.disabled = !s.ok;
    } catch (e) {
      clear(stockRow);
      stockRow.append(h('div', h('span.bad', '✗'), 'stock firmware: ' + e.message));
      buildBtn.disabled = true;
    }
  }

  function loadSets() {
    clear(setSelect);
    setSelect.append(h('option', { value: '' }, 'stock presets (plus the mods above)'));
    for (const s of app.state.preset_sets) setSelect.append(h('option', { value: s.file }, s.name + ' (' + s.changes + ' changes)'));
    const want = location.hash.split('/')[1];
    if (want) setSelect.value = decodeURIComponent(want);
  }

  async function update() {
    localStorage.setItem(STORAGE, JSON.stringify([...selected]));
    for (const [id, c] of Object.entries(cards)) {
      if (c.toggle) c.toggle.set(selected.has(id));
      if (c.card) c.card.classList.toggle('on', selected.has(id));
    }
    resolved = await api.post('/api/resolve', { selected: [...selected] });
    clear(chips);
    if (!resolved.selected.length) chips.append(h('span.dim', 'nothing: a stock firmware with the module code present but every module off'));
    for (const id of resolved.selected) chips.append(h('span.chip.on', byId[id].name, h('button', { 'aria-label': 'remove ' + byId[id].name, onclick: () => set(id, false) }, '×')));
    clear(warnings);
    for (const e of resolved.errors) warnings.append(h('div.note.warn', e));
    for (const w of resolved.warnings) warnings.append(h('div.note.warn', w));
    const lists = resolved.names.map((n) => n.active ? n.shown : n.stock).join(', ');
    warnings.append(h('div.note', 'The engine and effect lists will read: ', h('b', lists + ', delay'),
      resolved.names.some((n) => n.active) ? ' (a category keeps its stock name while none of its modules is on).' : ' (stock names: no module is on).'));
    for (const c of Object.values(cards)) if (c.warn) clear(c.warn);
    for (const f of features) {
      if (!selected.has(f.id) || !cards[f.id]?.warn) continue;
      for (const [k, text] of Object.entries(f.warnings)) {
        if (k.startsWith('!') ? !selected.has(k.slice(1)) : selected.has(k)) cards[f.id].warn.textContent = text;
      }
    }
    const b = resolved.budget;
    const frac = Math.min(1, b.non_sampler / b.non_sampler_limit);
    meter.firstChild.style.width = (frac * 100) + '%';
    meter.firstChild.className = b.non_sampler > b.non_sampler_limit - 4 ? 'warn' : '';
    meter.lastChild.style.left = (b.non_sampler_limit - 5) / b.non_sampler_limit * 100 + '%';
    meterText.textContent = b.non_sampler + ' of ' + b.non_sampler_limit + ' non-sampler · ' + b.sampler + ' of ' + b.sampler_limit + ' sampler · ' + b.total + ' of ' + b.total_limit + ' total (a chosen preset set adds its own rows)';
    buildBtn.disabled = !resolved.ok || buildBtn.dataset.running === '1';
  }

  async function startBuild() {
    results.classList.remove('hidden');
    clear(results);
    const checks = h('div.checks');
    const log = h('div.log');
    results.append(h('div.eyebrow', 'Building'), checks, log);
    buildBtn.dataset.running = '1';
    buildBtn.disabled = true;
    try {
      await api.post('/api/build', { selected: [...selected], out_name: outName.value.trim(), preset_set: setSelect.value || null });
    } catch (e) {
      toast(e.message, 'err');
      results.append(h('div.note.warn', e.message));
      buildBtn.dataset.running = '0';
      update();
      return;
    }
    polling = setInterval(async () => {
      let s;
      try { s = await api.get('/api/build/status'); } catch (e) { return; }
      log.textContent = s.log.join('\n');
      log.scrollTop = log.scrollHeight;
      if (s.running) return;
      clearInterval(polling); polling = null;
      buildBtn.dataset.running = '0';
      update();
      clear(checks);
      if (s.error || !s.report) {
        checks.append(h('div', h('span.bad', '✗'), s.error || 'build failed'));
        results.append(h('div.note.warn', 'No package was written. Nothing to flash.'));
        return;
      }
      const r = s.report;
      for (const st of r.steps) checks.append(h('div', h('span.ok', '✓'), st.step + (st.note ? ': ' + st.note : '')));
      checks.append(h('div', h('span' + (r.verify.ok ? '.ok' : '.bad'), r.verify.ok ? '✓' : '✗'), 'bootloader check ' + (r.verify.ok ? 'PASS (te-boot.ldr absent)' : 'FAIL')));
      checks.append(h('div', h('span.ok', '✓'), 'package contents equal the work dir (' + r.contents.files + ' files)'));
      checks.append(h('div', h('span.ok', '✓'), 'differs from stock only in: ' + [...r.diff.changed, ...r.diff.added.map(a => a.replace('content/display/', ''))].join(', ')));
      results.append(h('div.note.ok', h('b', 'Ready: '), r.out));
      results.append(h('div.note', r.next));
      toast('Built ' + r.out.split(/[\\/]/).pop(), 'ok');
    }, 700);
  }

  // ---- assemble ----
  clear(main);
  main.append(h('section.view',
    h('header', h('h2', 'Builder'), h('p', 'Tick the modules and mods you want in your firmware and press Build. The app takes your stock op1_246.op1, adds the chosen set, verifies the package and tells you how to install it.')),
    h('div.cols', grid, panel)));
  loadStock();
  loadSets();
  update();
  return { destroy() { if (polling) clearInterval(polling); } };
}
