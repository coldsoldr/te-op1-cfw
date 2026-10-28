// The preset editor (synth and drum), shared by the librarian and the factory tab.
// makeEditor({patch, audioUrl, peaks, audio, onsave, onrevert}) -> element with .getPatch()
import { api, app, h, clear, toggle, seg, toast } from './app.js';

const PITCH_PER_SEMITONE = 512;   // the drum key pitch unit is unverified; used for the preview only

export function makeEditor(opts) {
  const K = app.knobs;
  let patch = JSON.parse(JSON.stringify(opts.patch));
  const original = JSON.stringify(patch);
  const isDrum = patch.type === 'drum' || patch.type === 'dbox';
  const root = h('div.editor');
  const vfoot = h('div.vfoot');
  let validateTimer = null;

  function changed() {
    clearTimeout(validateTimer);
    validateTimer = setTimeout(validate, 150);
  }

  async function validate() {
    try {
      const v = await api.post('/api/validate', { patch });
      clear(vfoot);
      if (!v.problems.length) vfoot.append(h('span.ok', '✓ passes the validator'));
      for (const p of v.problems) vfoot.append(h('span.bad', '✗ ' + p));
      for (const w of v.warnings) vfoot.append(h('span.dim', '· ' + w));
      for (const c of v.custom) vfoot.append(h('span.tag.new', 'needs the custom firmware: ' + c));
      saveBtn.disabled = v.problems.length > 0;
    } catch (e) { toast(e.message, 'err'); }
  }

  // ---- header ----
  const nameInput = h('input.mono', { type: 'text', value: patch.name, maxlength: 13 });
  const nameCount = h('span.dim.mono', patch.name.length + '/13');
  nameInput.addEventListener('input', () => { patch.name = nameInput.value; nameCount.textContent = patch.name.length + '/13'; changed(); });
  const octave = h('input', { type: 'number', min: -4, max: 4, value: patch.octave, style: { width: '70px' } });
  octave.addEventListener('change', () => { patch.octave = parseInt(octave.value, 10) || 0; changed(); });
  const header = h('div.row',
    h('label.field', 'Name', h('div.row', nameInput, nameCount)),
    h('label.field', 'Octave', octave));
  if (!isDrum) {
    const engineSel = h('select');
    for (const [t, e] of Object.entries(K.synth_engines)) engineSel.append(h('option', { value: t, selected: t === patch.type }, e.name));
    engineSel.addEventListener('change', () => { patch.type = engineSel.value; if (patch.type !== 'sampler') delete patch.base_freq; else patch.base_freq = patch.base_freq || 261.6253662109375; rebuildEngine(); changed(); });
    header.append(h('label.field', 'Engine', engineSel));
  } else {
    header.append(h('label.field', 'Kit', h('div', seg(['drum', 'dbox'].filter(t => t === patch.type), patch.type, () => {}))));
  }
  if (opts.folder !== undefined) {
    const folder = h('input.mono', { type: 'text', value: opts.folder });
    folder.addEventListener('input', () => { root.folder = folder.value; });
    root.folder = opts.folder;
    header.append(h('label.field', 'Folder', folder));
  }
  root.append(header);

  // ---- a knob row: colour, name, slider, number, hint ----
  function knobRow(color, name, arr, i, hint, min = -32768, max = 32767) {
    const range = h('input', { type: 'range', min, max, value: arr[i] });
    const num = h('input', { type: 'number', min, max, value: arr[i] });
    const row = h('div.knob.' + color, h('i.dot.' + color), h('b', name), range, num, h('span.hint', hint || ''));
    range.addEventListener('input', () => { arr[i] = parseInt(range.value, 10); num.value = arr[i]; changed(); });
    num.addEventListener('change', () => { arr[i] = Math.max(min, Math.min(max, parseInt(num.value, 10) || 0)); range.value = arr[i]; changed(); });
    row.refresh = () => { range.value = arr[i]; num.value = arr[i]; };
    return row;
  }
  const COLORS = ['blue', 'green', 'white', 'orange'];

  // ---- ENGINE (T1) ----
  const engineBox = h('div.stack');
  function rebuildEngine() {
    clear(engineBox);
    if (isDrum) {
      const e = K.drum_types[patch.type];
      engineBox.append(h('div.dim', e.name + (patch.type === 'drum' ? ': per-key settings below' : ': per-key oscillator data (8 values per key)')));
      return;
    }
    const e = K.synth_engines[patch.type];
    let names = e.knobs;
    if (patch.type === 'phase') {
      const rings = decode(patch.knobs[6], 2) === 1;
      const engSeg = seg([{ value: 0, label: 'phase' }, { value: 1, label: 'Rings' }], rings ? 1 : 0, (v) => { patch.knobs[6] = v * 4096; rebuildEngine(); changed(); }, 'orange');
      const row = h('div.row', h('span.eyebrow', 'multi'), engSeg);
      if (rings) {
        const model = decode(patch.knobs[4], K.rings.models.length);
        const dots = h('div.dots');
        K.rings.models.forEach((m, i) => dots.append(h('button', { class: i === model ? 'on' : '', title: m, 'aria-label': m, onclick: () => { patch.knobs[4] = i * 4096; rebuildEngine(); changed(); } })));
        row.append(h('span.eyebrow', 'model'), dots, h('span.dim', K.rings.models[model]));
        const poly = decode(patch.knobs[5], K.rings.polyphony.length);
        row.append(h('span.eyebrow', 'polyphony'), seg(K.rings.polyphony.map((p, i) => ({ value: i, label: p })), poly, (v) => { patch.knobs[5] = v * 4096; changed(); }, 'green'));
        names = K.rings.knobs;
      }
      engineBox.append(row);
    }
    const knobs = h('div.knobs');
    names.forEach((n, i) => knobs.append(knobRow(COLORS[i], n, patch.knobs, i, hintFor(patch.type, i, patch.knobs[i]))));
    engineBox.append(knobs);
    if (patch.type !== 'phase') engineBox.append(h('div.dim', { style: { fontSize: '0.82rem' } }, 'knobs 5–8 (shift page or hidden): ' + patch.knobs.slice(4).join(', ')));
    if (patch.type === 'sampler') {
      const bf = h('input', { type: 'number', step: 'any', value: patch.base_freq, style: { width: '160px' } });
      bf.addEventListener('change', () => { patch.base_freq = parseFloat(bf.value) || 261.6253662109375; changed(); });
      engineBox.append(h('label.field', 'base frequency (Hz)', bf));
    }
  }
  function hintFor(type, i, v) {
    if (type === 'phase' && decode(patch.knobs[6], 2) === 1 && i === 1) return v > 29491 ? 'above the shown scale' : '';
    return Math.round((v + 32768) / 65535 * 100) + '%';
  }
  root.append(section('Engine', 'T1', engineBox));
  rebuildEngine();

  // ---- ENVELOPE (T2) ----
  const envArr = isDrum ? patch.dyna_env : patch.adsr;
  const envNames = isDrum ? K.dyna_env : K.adsr;
  const envKnobs = h('div.knobs');
  envNames.slice(0, 4).forEach((n, i) => envKnobs.append(knobRow(COLORS[i], n, envArr, i)));
  const envSvg = envelopeSvg(envArr);
  root.append(section(isDrum ? 'Drum envelope' : 'Envelope', 'T2', h('div.row', { style: { alignItems: 'flex-start' } }, h('div.grow', envKnobs), envSvg)));
  const envRefresh = () => envSvg.replaceWith(envelopeSvg(envArr));

  // ---- EFFECT (T3) ----
  const fxBox = h('div.stack');
  function rebuildFx() {
    clear(fxBox);
    const catSel = h('select');
    const typeNames = Object.entries(K.fx_types).map(([t, e]) => [t, K.fx_categories[t] ? K.fx_categories[t].category : e.name]);
    for (const [t, label] of typeNames) catSel.append(h('option', { value: t, selected: t === patch.fx_type }, label));
    catSel.addEventListener('change', () => { patch.fx_type = catSel.value; patch.fx_params[4] = 0; rebuildFx(); changed(); });
    const row = h('div.row', h('label.field', 'Effect', catSel));
    const cat = K.fx_categories[patch.fx_type];
    let names = K.fx_types[patch.fx_type].knobs;
    let engineName = K.fx_types[patch.fx_type].name;
    if (cat) {
      const id = engineId(patch.fx_params);
      const engSel = h('select');
      engSel.append(h('option', { value: 0, selected: id === 0 }, K.fx_types[patch.fx_type].name + ' (stock)'));
      for (const [eid, e] of Object.entries(cat.engines)) engSel.append(h('option', { value: eid, selected: String(id) === eid }, e.name));
      engSel.addEventListener('change', () => { setEngineId(patch.fx_params, parseInt(engSel.value, 10)); rebuildFx(); changed(); });
      row.append(h('label.field', cat.category + ' → engine', engSel));
      if (cat.engines[String(id)]) { names = cat.engines[String(id)].knobs; engineName = cat.engines[String(id)].name; }
    }
    const act = toggle(patch.fx_active, (on) => { patch.fx_active = on; changed(); }, 'green');
    row.append(h('label.field', 'active', act));
    fxBox.append(row);
    const knobs = h('div.knobs');
    names.forEach((n, i) => knobs.append(knobRow(COLORS[i], n, patch.fx_params, i, '', 0, 32767)));
    fxBox.append(knobs);
    const id = cat ? engineId(patch.fx_params) : 0;
    if (id) fxBox.append(h('div.row', h('span.tag.new', 'needs the custom firmware'), h('span.dim', 'on stock, switch it to ' + K.fx_types[patch.fx_type].name + ' first. Words 5–8 hold ' + engineName + "'s page-2 settings and its marker: " + patch.fx_params.slice(4).map(x => '0x' + x.toString(16)).join(' '))));
    else fxBox.append(h('div.dim', { style: { fontSize: '0.82rem' } }, 'words 5–8: ' + patch.fx_params.slice(4).join(', ')));
  }
  root.append(section('Effect', 'T3', fxBox));
  rebuildFx();

  // ---- LFO (T4) ----
  const lfoBox = h('div.stack');
  function rebuildLfo() {
    clear(lfoBox);
    const sel = h('select');
    for (const [t, e] of Object.entries(K.lfo_types)) sel.append(h('option', { value: t, selected: t === patch.lfo_type }, e.name));
    sel.addEventListener('change', () => { patch.lfo_type = sel.value; rebuildLfo(); changed(); });
    const act = toggle(patch.lfo_active, (on) => { patch.lfo_active = on; changed(); }, 'green');
    lfoBox.append(h('div.row', h('label.field', 'LFO', sel), h('label.field', 'active', act)));
    const knobs = h('div.knobs');
    (K.lfo_types[patch.lfo_type] || { knobs: ['1', '2', '3', '4'] }).knobs.forEach((n, i) => knobs.append(knobRow(COLORS[i], n, patch.lfo_params, i)));
    lfoBox.append(knobs);
  }
  root.append(section('LFO', 'T4', lfoBox));
  rebuildLfo();

  // ---- DRUM keys and preview ----
  let player = null;
  if (isDrum && patch.type === 'drum') {
    const keysBox = h('div.stack');
    root.insertBefore(section('Keys and sample', 'T1', keysBox), root.children[1]);
    player = drumPanel(keysBox, patch, opts, changed);
  } else if (!isDrum && patch.type === 'sampler' && opts.audioUrl) {
    const box = h('div.stack');
    root.insertBefore(section('Sample', 'preview', box), root.children[2]);
    player = samplePanel(box, opts);
  }

  // ---- raw JSON, footer ----
  const raw = h('textarea.mono');
  const rawDetails = h('details', h('summary', 'raw JSON'), raw, h('div.row', h('button.small', { onclick: () => {
    try { const p = JSON.parse(raw.value); Object.keys(patch).forEach(k => delete patch[k]); Object.assign(patch, p); rebuildAll(); changed(); toast('JSON applied'); } catch (e) { toast('bad JSON: ' + e.message, 'err'); }
  } }, 'Apply JSON')));
  rawDetails.addEventListener('toggle', () => { if (rawDetails.open) raw.value = JSON.stringify(patch, Object.keys(patch).sort(), 1); });
  root.append(rawDetails);
  const saveBtn = h('button.primary', { onclick: () => opts.onsave && opts.onsave(getPatch(), root.folder) }, opts.saveLabel || 'Save');
  const revertBtn = h('button', { onclick: () => { patch = JSON.parse(original); rebuildAll(); changed(); } }, 'Revert');
  root.append(h('div.row.between', vfoot, h('div.row', revertBtn, saveBtn)));

  function rebuildAll() { rebuildEngine(); rebuildFx(); rebuildLfo(); envRefresh(); nameInput.value = patch.name; octave.value = patch.octave; }
  function getPatch() { return JSON.parse(JSON.stringify(patch)); }
  root.getPatch = getPatch;
  root.destroy = () => { if (player && player.stop) player.stop(); };
  validate();
  return root;
}

function section(title, tag, body) {
  return h('div.section', h('div.shead', h('h3', title), h('span.tag', tag)), body);
}

function decode(v, n) { return Math.max(0, Math.min(n - 1, Math.floor((v + 2048) / 4096))); }
function engineId(fx) { const w = fx[4] & 0xffff; return (w & 0xf000) === 0x6000 ? (w >> 8) & 0xf : 0; }
function setEngineId(fx, id) {
  if (!id) { fx[4] = 0; fx[5] = 0; fx[6] = 0; fx[7] = 0; return; }
  fx[4] = 0x6000 | (id << 8); fx[5] = 0; fx[6] = 0; fx[7] = 0;
}

function envelopeSvg(env) {
  const [a, d, s, r] = env.map(v => Math.max(0, Math.min(1, (v + 32768) / 65535)));
  const W = 220, H = 90, pad = 6;
  const x0 = pad, x1 = x0 + a * 60, x2 = x1 + d * 60, x3 = W - pad - r * 60;
  const ys = H - pad - s * (H - 2 * pad);
  const svg = h('svg', { viewBox: '0 0 ' + W + ' ' + H, width: W, height: H, style: { flex: 'none', background: 'var(--screen)', borderRadius: '6px' }, role: 'img', 'aria-label': 'envelope' });
  const seg = (xa, ya, xb, yb, color) => { const l = document.createElementNS('http://www.w3.org/2000/svg', 'line'); l.setAttribute('x1', xa); l.setAttribute('y1', ya); l.setAttribute('x2', xb); l.setAttribute('y2', yb); l.setAttribute('stroke', color); l.setAttribute('stroke-width', '1.5'); svg.append(l); };
  seg(x0, H - pad, x1, pad, 'var(--blue)');
  seg(x1, pad, x2, ys, 'var(--green)');
  seg(x2, ys, x3, ys, 'var(--white)');
  seg(x3, ys, W - pad, H - pad, 'var(--orange)');
  return svg;
}

// ---- audio ----
let ctx = null;
function audioContext() { if (!ctx) ctx = new (window.AudioContext || window.webkitAudioContext)(); return ctx; }

async function loadBuffer(url) {
  const r = await fetch(url);
  if (!r.ok) throw new Error('no audio');
  const ab = await r.arrayBuffer();
  return await audioContext().decodeAudioData(ab);
}

function drawWave(canvas, peaks, regions, selected, playhead) {
  const c = canvas.getContext('2d');
  const W = canvas.width = canvas.clientWidth * (window.devicePixelRatio || 1);
  const H = canvas.height = canvas.clientHeight * (window.devicePixelRatio || 1);
  c.fillStyle = '#000'; c.fillRect(0, 0, W, H);
  if (regions) regions.forEach((rg, i) => {
    if (rg.end <= rg.start) return;
    c.fillStyle = i === selected ? 'rgba(255,57,90,0.28)' : 'rgba(99,139,255,0.12)';
    c.fillRect(rg.start * W, 0, (rg.end - rg.start) * W, H);
    c.strokeStyle = i === selected ? '#ff395a' : 'rgba(99,139,255,0.5)';
    c.beginPath(); c.moveTo(rg.start * W, 0); c.lineTo(rg.start * W, H); c.stroke();
  });
  if (peaks && peaks.length) {
    c.strokeStyle = '#ded7ff'; c.lineWidth = 1;
    c.beginPath();
    peaks.forEach(([lo, hi], i) => {
      const x = i / peaks.length * W;
      c.moveTo(x, H / 2 - hi / 32768 * H / 2);
      c.lineTo(x, H / 2 - lo / 32768 * H / 2 + 1);
    });
    c.stroke();
  }
  if (playhead != null) { c.strokeStyle = '#00ee92'; c.lineWidth = 2; c.beginPath(); c.moveTo(playhead * W, 0); c.lineTo(playhead * W, H); c.stroke(); }
}

function samplePanel(box, opts) {
  const canvas = h('canvas.wave');
  const info = h('span.dim.mono', opts.audio ? opts.audio.seconds.toFixed(2) + ' s · ' + opts.audio.rate + ' Hz' : '');
  let buffer = null, source = null, startedAt = 0, raf = null;
  const play = h('button.green', { onclick: async () => {
    try { if (!buffer) buffer = await loadBuffer(opts.audioUrl); } catch (e) { toast('no audio to preview', 'err'); return; }
    stop();
    source = audioContext().createBufferSource(); source.buffer = buffer; source.connect(audioContext().destination); source.start();
    startedAt = audioContext().currentTime;
    const tick = () => { const t = (audioContext().currentTime - startedAt) / buffer.duration; if (t < 1) { drawWave(canvas, opts.peaks, null, -1, t); raf = requestAnimationFrame(tick); } else drawWave(canvas, opts.peaks); };
    tick();
  } }, '▶ Play');
  function stop() { if (source) { try { source.stop(); } catch (e) { /* ended */ } source = null; } if (raf) cancelAnimationFrame(raf); drawWave(canvas, opts.peaks); }
  box.append(canvas, h('div.transport', play, h('button', { onclick: stop }, '■ Stop'), info));
  setTimeout(() => drawWave(canvas, opts.peaks), 0);
  return { stop };
}

function drumPanel(box, patch, opts, changed) {
  const K = app.knobs;
  const unit = K.drum_unit, rate = K.sample_rate;
  const frames = opts.audio ? opts.audio.frames : 0;
  const total = frames ? frames : 12 * rate;
  const canvas = h('canvas.wave');
  let selected = 0, buffer = null, source = null, raf = null, startedAt = 0, playLen = 0, playStart = 0;
  const regions = () => patch.start.map((s, i) => ({ start: s / unit / total, end: patch.end[i] / unit / total }));
  const redraw = (ph) => drawWave(canvas, opts.peaks, regions(), selected, ph);
  const keys = h('div.keys');
  const keyButtons = K.drum_keys.map((name, i) => h('button', { class: (name.includes('#') ? 'black ' : '') + (i === selected ? 'on' : ''), onclick: () => { selected = i; refreshKeys(); rebuildKey(); redraw(); playKey(i); } }, name));
  keys.append(...keyButtons);
  function refreshKeys() { keyButtons.forEach((b, i) => b.classList.toggle('on', i === selected)); }

  const keyBox = h('div.stack');
  function rebuildKey() {
    clear(keyBox);
    const i = selected;
    const secs = (v) => (v / unit / rate).toFixed(3) + ' s';
    const row = (color, name, arr, min, max, hint) => {
      const range = h('input', { type: 'range', min, max, value: arr[i] });
      const num = h('input', { type: 'number', min, max, value: arr[i] });
      const hintEl = h('span.hint', hint ? hint(arr[i]) : '');
      const r = h('div.knob.' + color, h('i.dot.' + color), h('b', name), range, num, hintEl);
      const set = (v) => { arr[i] = Math.max(min, Math.min(max, v)); range.value = arr[i]; num.value = arr[i]; hintEl.textContent = hint ? hint(arr[i]) : ''; redraw(); changed(); };
      range.addEventListener('input', () => set(parseInt(range.value, 10)));
      num.addEventListener('change', () => set(parseInt(num.value, 10) || 0));
      return r;
    };
    const maxPos = Math.round(total * unit);
    keyBox.append(h('div.row', h('h4', 'Key ' + K.drum_keys[i] + ' (' + (i + 1) + ')'), h('span.dim', 'click a key to hear its region')));
    keyBox.append(h('div.knobs',
      row('blue', 'pitch', patch.pitch, -32768, 32767, (v) => (v / PITCH_PER_SEMITONE).toFixed(2) + ' semitones (unit unverified)'),
      row('green', 'start', patch.start, 0, maxPos, secs),
      row('white', 'end', patch.end, 0, maxPos, secs),
      row('orange', 'volume', patch.volume, 0, 32767, (v) => Math.round(v / 8192 * 100) + '%')));
    const pm = patch.playmode[i];
    keyBox.append(h('div.row',
      h('span.eyebrow', 'play mode'), seg([{ value: 8192, label: 'once' }, { value: 9216, label: 'loop' }, { value: 10240, label: 'hold' }], [8192, 9216, 10240].includes(pm) ? pm : 8192, (v) => { patch.playmode[i] = v; changed(); }, 'orange'),
      h('span.dim.mono', 'raw ' + pm),
      h('span.eyebrow', 'reverse'), toggle(patch.reverse[i] !== 8192, (on) => { patch.reverse[i] = on ? 24576 : 8192; changed(); }, 'green'),
      h('span.dim.mono', 'raw ' + patch.reverse[i])));
  }

  async function ensureBuffer() {
    if (buffer || !opts.audioUrl) return !!buffer;
    try { buffer = await loadBuffer(opts.audioUrl); return true; } catch (e) { toast('no audio to preview', 'err'); return false; }
  }
  function stop() { if (source) { try { source.stop(); } catch (e) { /* ended */ } source = null; } if (raf) cancelAnimationFrame(raf); redraw(); keyButtons.forEach(b => b.classList.remove('playing')); }
  async function playKey(i) {
    if (!(await ensureBuffer())) return;
    stop();
    const s = Math.max(0, Math.min(1, patch.start[i] / unit / total)) * buffer.duration;
    const e = Math.max(0, Math.min(1, patch.end[i] / unit / total)) * buffer.duration;
    if (e <= s) return;
    const reverse = patch.reverse[i] !== 8192;
    let buf = buffer;
    if (reverse) {
      buf = audioContext().createBuffer(buffer.numberOfChannels, buffer.length, buffer.sampleRate);
      for (let ch = 0; ch < buffer.numberOfChannels; ch++) { const d = buffer.getChannelData(ch).slice().reverse(); buf.copyToChannel(d, ch); }
    }
    source = audioContext().createBufferSource(); source.buffer = buf;
    source.playbackRate.value = Math.pow(2, patch.pitch[i] / PITCH_PER_SEMITONE / 12);
    const gain = audioContext().createGain(); gain.gain.value = patch.volume[i] / 8192;
    source.connect(gain); gain.connect(audioContext().destination);
    const offset = reverse ? buffer.duration - e : s;
    source.start(0, offset, e - s);
    startedAt = audioContext().currentTime; playStart = s / buffer.duration; playLen = (e - s) / buffer.duration;
    keyButtons[i].classList.add('playing');
    const tick = () => {
      const t = (audioContext().currentTime - startedAt) * source.playbackRate.value / buffer.duration;
      if (t < playLen) { redraw(reverse ? playStart + playLen - t : playStart + t); raf = requestAnimationFrame(tick); } else stop();
    };
    tick();
  }
  async function playAll() {
    if (!(await ensureBuffer())) return;
    stop();
    source = audioContext().createBufferSource(); source.buffer = buffer; source.connect(audioContext().destination); source.start();
    startedAt = audioContext().currentTime;
    const tick = () => { const t = (audioContext().currentTime - startedAt) / buffer.duration; if (t < 1) { redraw(t); raf = requestAnimationFrame(tick); } else stop(); };
    tick();
  }
  canvas.addEventListener('click', (e) => {
    const x = (e.clientX - canvas.getBoundingClientRect().left) / canvas.clientWidth;
    const rg = regions();
    const hit = rg.findIndex(r => x >= r.start && x < r.end);
    if (hit >= 0) { selected = hit; refreshKeys(); rebuildKey(); redraw(); playKey(hit); }
  });
  const info = h('span.dim.mono', opts.audio ? opts.audio.seconds.toFixed(2) + ' s · ' + opts.audio.rate + ' Hz · ' + (opts.audio.channels === 2 ? 'stereo' : 'mono') : 'no audio in this file');
  box.append(canvas,
    h('div.transport', h('button.green', { onclick: playAll }, '▶ Play whole sample'), h('button', { onclick: stop }, '■ Stop'), info),
    h('div.dim', { style: { fontSize: '0.82rem' } }, 'Preview plays the stored audio through the browser; start and end are in the device’s units (12 s = the full range).'),
    keys, keyBox);
  rebuildKey();
  setTimeout(() => redraw(), 0);
  return { stop };
}
