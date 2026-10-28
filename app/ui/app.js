// The app shell: tabs, API helpers, DOM helpers, toasts and modals.
import { builderView } from './builder.js';
import { librarianView } from './librarian.js';
import { factoryView } from './factory.js';
import { settingsView } from './settings.js';

export const api = {
  async get(path, params) {
    const url = params ? path + '?' + new URLSearchParams(params) : path;
    const r = await fetch(url);
    return handle(r);
  },
  async post(path, body) {
    const r = await fetch(path, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body || {}) });
    return handle(r);
  },
  url(path, params) { return params ? path + '?' + new URLSearchParams(params) : path; },
};

async function handle(r) {
  const text = await r.text();
  let data;
  try { data = JSON.parse(text); } catch (e) { data = { error: text || r.statusText }; }
  if (!r.ok || data.error) throw new Error(data.error || ('HTTP ' + r.status));
  return data;
}

// h('div.card.selected', {onclick, title}, child, ...) -> element
export function h(tag, attrs, ...children) {
  const [name, ...classes] = tag.split('.');
  const el = document.createElement(name || 'div');
  if (classes.length) el.className = classes.join(' ');
  if (attrs && typeof attrs === 'object' && !(attrs instanceof Node) && !Array.isArray(attrs)) {
    for (const [k, v] of Object.entries(attrs)) {
      if (v == null || v === false) continue;
      if (k.startsWith('on') && typeof v === 'function') el.addEventListener(k.slice(2), v);
      else if (k === 'dataset') Object.assign(el.dataset, v);
      else if (k === 'style' && typeof v === 'object') Object.assign(el.style, v);
      else if (k in el && k !== 'list' && typeof v !== 'string') el[k] = v;
      else el.setAttribute(k, v === true ? '' : v);
    }
  } else if (attrs != null) {
    children.unshift(attrs);
  }
  append(el, children);
  return el;
}

function append(el, children) {
  for (const c of children.flat(Infinity)) {
    if (c == null || c === false) continue;
    el.append(c instanceof Node ? c : document.createTextNode(String(c)));
  }
}

export function clear(el) { while (el.firstChild) el.removeChild(el.firstChild); return el; }

export function toggle(checked, onchange, extraClass) {
  const b = h('button.toggle' + (extraClass ? '.' + extraClass : ''), { role: 'switch', 'aria-checked': String(!!checked), type: 'button' });
  b.addEventListener('click', () => {
    const on = b.getAttribute('aria-checked') !== 'true';
    b.setAttribute('aria-checked', String(on));
    onchange(on);
  });
  b.set = (on) => b.setAttribute('aria-checked', String(!!on));
  return b;
}

export function seg(options, value, onchange, color) {
  const box = h('div.seg' + (color ? '.' + color : ''));
  const buttons = options.map((o, i) => {
    const v = typeof o === 'object' ? o.value : o;
    const label = typeof o === 'object' ? o.label : o;
    const b = h('button', { type: 'button', class: v === value ? 'on' : '' }, label);
    b.addEventListener('click', () => { box.set(v); onchange(v); });
    return b;
  });
  box.set = (v) => buttons.forEach((b, i) => b.classList.toggle('on', (typeof options[i] === 'object' ? options[i].value : options[i]) === v));
  box.append(...buttons);
  return box;
}

export function toast(message, kind) {
  const root = document.getElementById('toasts');
  const t = h('div.toast' + (kind ? '.' + kind : ''), message);
  root.append(t);
  setTimeout(() => t.remove(), kind === 'err' ? 9000 : 4500);
}

export function modal(content, { narrow = false, onclose } = {}) {
  const root = document.getElementById('modal-root');
  const box = h('div.modal' + (narrow ? '.narrow' : ''), content);
  const bg = h('div.modal-bg', { onclick: (e) => { if (e.target === bg) close(); } }, box);
  function close() { bg.remove(); if (onclose) onclose(); }
  const onkey = (e) => { if (e.key === 'Escape') { close(); document.removeEventListener('keydown', onkey); } };
  document.addEventListener('keydown', onkey);
  root.append(bg);
  return { close, box };
}

export function confirmDialog(title, text, okLabel = 'OK', danger = false) {
  return new Promise((resolve) => {
    const m = modal([
      h('h3', title),
      h('p.dim', text),
      h('div.row', { style: { justifyContent: 'flex-end' } },
        h('button', { onclick: () => { m.close(); resolve(false); } }, 'Cancel'),
        h('button' + (danger ? '.danger' : '.primary'), { onclick: () => { m.close(); resolve(true); } }, okLabel)),
    ], { narrow: true });
  });
}

export function promptDialog(title, label, value = '', okLabel = 'OK') {
  return new Promise((resolve) => {
    const input = h('input', { type: 'text', value, class: 'mono' });
    const m = modal([
      h('h3', title),
      h('label.field', label, input),
      h('div.row', { style: { justifyContent: 'flex-end' } },
        h('button', { onclick: () => { m.close(); resolve(null); } }, 'Cancel'),
        h('button.primary', { onclick: () => { m.close(); resolve(input.value); } }, okLabel)),
    ], { narrow: true });
    input.focus();
    input.addEventListener('keydown', (e) => { if (e.key === 'Enter') { m.close(); resolve(input.value); } });
  });
}

// A folder/file picker over /api/browse. kind: 'file' (returns a file path) or 'dir'.
export function browseDialog(title, start, kind = 'file', filter) {
  return new Promise((resolve) => {
    const list = h('div.browse');
    const pathEl = h('input.mono', { type: 'text', value: start || '' });
    let current = start || '';
    async function load(p) {
      let data;
      try { data = await api.get('/api/browse', { path: p || '' }); } catch (e) { toast(e.message, 'err'); return; }
      current = data.path; pathEl.value = data.path;
      clear(list);
      for (const d of data.drives) list.append(h('button', { onclick: () => load(d) }, d));
      list.append(h('button', { onclick: () => load(data.parent) }, '..'));
      for (const d of data.dirs) list.append(h('button', { onclick: () => load(data.path + (data.path.endsWith('\\') || data.path.endsWith('/') ? '' : '\\') + d) }, d + '/'));
      if (kind === 'file') for (const f of data.files) {
        if (filter && !filter(f.name)) continue;
        list.append(h('button.file', { onclick: () => { m.close(); resolve(join(data.path, f.name)); } }, f.name));
      }
    }
    const m = modal([
      h('h3', title),
      h('div.row', pathEl, h('button', { onclick: () => load(pathEl.value) }, 'Go')),
      list,
      h('div.row', { style: { justifyContent: 'flex-end' } },
        h('button', { onclick: () => { m.close(); resolve(null); } }, 'Cancel'),
        kind === 'dir' ? h('button.primary', { onclick: () => { m.close(); resolve(current); } }, 'Use this folder') : null),
    ]);
    load(start);
  });
}

export function join(dir, name) { return dir + (dir.endsWith('\\') || dir.endsWith('/') ? '' : (dir.includes('\\') ? '\\' : '/')) + name; }

// Sorting a table by its columns. sorter('name') keeps the sort key and
// direction; th(label, key) makes a clickable header (click again to flip),
// sort(list, getters) orders a copy by getters[key] (numbers numerically,
// text case-insensitively, missing values last; ties keep their order).
export function sorter(key, dir = 1, onchange = null) {
  const s = { key, dir, onchange };
  s.th = (label, k, cls) => {
    const on = k && s.key === k;
    const th = h('th' + (cls ? '.' + cls : '') + (k ? '.sortable' : '') + (on ? '.sorted' : ''), k ? { title: 'sort by ' + label, onclick: () => s.set(k) } : null, label);
    if (on) th.append(h('span.arrow', s.dir > 0 ? '\u25b4' : '\u25be'));
    return th;
  };
  s.set = (k) => { if (s.key === k) s.dir = -s.dir; else { s.key = k; s.dir = 1; } if (s.onchange) s.onchange(); };
  s.sort = (list, getters) => {
    const g = getters[s.key];
    if (!g) return list;
    const cmp = (a, b) => {
      let x = g(a), y = g(b);
      const nx = x == null || x === '', ny = y == null || y === '';
      if (nx || ny) return nx && ny ? 0 : nx ? 1 : -1;     // missing last, either way
      if (typeof x === 'number' && typeof y === 'number') return (x - y) * s.dir;
      return String(x).localeCompare(String(y), undefined, { sensitivity: 'base', numeric: true }) * s.dir;
    };
    return list.map((e, i) => [e, i]).sort((a, b) => cmp(a[0], b[0]) || a[1] - b[1]).map(x => x[0]);
  };
  return s;
}

export function fmtBytes(n) { return n > 1e6 ? (n / 1e6).toFixed(1) + ' MB' : n > 1e3 ? (n / 1e3).toFixed(0) + ' KB' : n + ' B'; }
export function fmtDate(ts) { return new Date(ts * 1000).toISOString().slice(0, 10); }

// shared state across views
export const app = {
  state: null,    // /api/state
  knobs: null,    // /api/knobs
  device: null,   // the chosen OP-1 root
  async refreshState() {
    app.state = await api.get('/api/state');
    if (!app.device && app.state.disks.length) app.device = app.state.disks[0];
    if (app.device && !app.state.disks.includes(app.device)) app.device = app.state.disks[0] || null;
    const pill = document.getElementById('device-pill');
    pill.textContent = app.device ? 'OP-1 disk at ' + app.device : 'OP-1: not connected';
    pill.classList.toggle('on', !!app.device);
    return app.state;
  },
};

const views = { builder: builderView, librarian: librarianView, factory: factoryView, settings: settingsView };
let current = null;

async function route() {
  const name = (location.hash || '#builder').slice(1).split('/')[0];
  const view = views[name] || views.builder;
  document.querySelectorAll('nav.tabs a, a.gear').forEach(a => a.classList.toggle('active', a.dataset.tab === name));
  const main = document.getElementById('view');
  if (current && current.destroy) current.destroy();
  clear(main);
  main.append(h('p.dim', 'Loading…'));
  try {
    if (!app.state) await app.refreshState();
    if (!app.knobs) app.knobs = await api.get('/api/knobs');
    current = await view(main);
  } catch (e) {
    clear(main);
    main.append(h('div.note.warn', 'Could not load: ' + e.message));
  }
}

window.addEventListener('hashchange', route);
route();
