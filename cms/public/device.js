'use strict';

const deviceId = new URLSearchParams(location.search).get('id') || '';
const fieldsEl = document.getElementById('fields');
const titleEl = document.getElementById('title');
const notfoundEl = document.getElementById('notfound');
const resultEl = document.getElementById('result');
const connEl = document.getElementById('conn');
const connText = document.getElementById('conn-text');

const FIELDS = [
  ['online', (v) => `<span class="status-dot ${v ? 'online' : 'offline'}"></span> ${v ? 'online' : 'offline'}`],
  ['name', text], ['deviceId', mono], ['board', text], ['version', mono], ['ip', mono],
  ['rssi', (v) => (v ? `${v} dBm` : '—')],
  ['freeHeap', (v) => (v != null ? `${(v / 1024).toFixed(0)} KB` : '—')],
  ['uptimeMs', fmtUptime], ['cpuLoadPercent', (v) => (v != null ? `${v}%` : '—')],
  ['activeProfileId', text], ['link', text], ['via', mono],
  ['lastSeen', fmtTs], ['lastPresence', text], ['presenceAt', fmtTs],
];

function text(v) { return v == null || v === '' ? '—' : escapeHtml(v); }
function mono(v) { return v == null || v === '' ? '—' : `<span class="mono">${escapeHtml(v)}</span>`; }
function fmtTs(v) { return v ? new Date(v).toLocaleString() : '—'; }
function fmtUptime(ms) {
  if (ms == null) return '—';
  const s = Math.floor(ms / 1000), d = Math.floor(s / 86400), h = Math.floor((s % 86400) / 3600), m = Math.floor((s % 3600) / 60);
  return d ? `${d}d ${h}h` : h ? `${h}h ${m}m` : `${m}m`;
}
function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

let currentTags = [];

function render(device) {
  if (!device) { notfoundEl.classList.remove('hidden'); return; }
  notfoundEl.classList.add('hidden');
  titleEl.textContent = device.name || device.deviceId;
  fieldsEl.innerHTML = FIELDS.map(([key, fmt]) =>
    `<tr><th>${key}</th><td>${fmt(device[key])}</td></tr>`).join('');
  currentTags = device.tags || [];
  renderTags();
}

function renderTags() {
  const el = document.getElementById('tag-chips');
  el.innerHTML = currentTags.length
    ? currentTags.map((t) => `<span class="tag">${escapeHtml(t)}<button class="tag-x" data-tag="${escapeHtml(t)}">×</button></span>`).join(' ')
    : '<span class="hint">no tags</span>';
  el.querySelectorAll('.tag-x').forEach((b) => { b.onclick = () => saveTags(currentTags.filter((t) => t !== b.dataset.tag)); });
}

async function saveTags(tags) {
  try {
    const res = await fetch(`/api/devices/${encodeURIComponent(deviceId)}/tags`, {
      method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ tags }),
    });
    if (res.ok) { currentTags = (await res.json()).tags || []; renderTags(); }
  } catch { /* ignore */ }
}

async function sendCommand(type, payload) {
  resultEl.textContent = `sending ${type}…`;
  try {
    const res = await fetch(`/api/devices/${encodeURIComponent(deviceId)}/cmd`, {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ type, payload }),
    });
    const body = await res.json();
    resultEl.textContent = JSON.stringify(body, null, 2);
  } catch (e) {
    resultEl.textContent = `error: ${e.message}`;
  }
}

document.querySelectorAll('button[data-cmd]').forEach((btn) => {
  btn.onclick = () => sendCommand(btn.dataset.cmd);
});
document.getElementById('add-tag').onclick = () => {
  const input = document.getElementById('new-tag');
  const tag = input.value.trim();
  if (tag && !currentTags.includes(tag)) saveTags([...currentTags, tag]);
  input.value = '';
};
document.getElementById('activate-profile').onclick = () => {
  const id = document.getElementById('profile-id').value.trim();
  if (!id) { resultEl.textContent = 'enter a profile id'; return; }
  sendCommand('activate_profile', { id });
};

async function loadProfiles() {
  const msg = document.getElementById('profiles-msg');
  const list = document.getElementById('profiles-list');
  msg.textContent = 'loading…';
  list.innerHTML = '';
  try {
    const res = await fetch(`/api/devices/${encodeURIComponent(deviceId)}/cmd`, {
      method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ type: 'get_profiles' }),
    });
    const body = await res.json();
    if (!res.ok || !body.ok) { msg.textContent = `error: ${body.error || res.status}`; return; }
    const profiles = body.profiles || [];
    msg.textContent = profiles.length ? '' : 'no profiles';
    for (const p of profiles) {
      const row = document.createElement('div');
      row.className = 'row';
      const active = p.id === body.active;
      row.innerHTML = `<span class="${active ? 'warn-text' : ''}">${escapeHtml(p.name || p.id)}${active ? ' (active)' : ''}</span>`;
      const btn = document.createElement('button');
      btn.textContent = 'Activate';
      btn.disabled = active;
      btn.onclick = () => sendCommand('activate_profile', { id: p.id }).then(loadProfiles);
      row.appendChild(btn);
      list.appendChild(row);
    }
  } catch (e) { msg.textContent = `error: ${e.message}`; }
}
document.getElementById('load-profiles').onclick = loadProfiles;
document.getElementById('push-config').onclick = () => {
  const raw = document.getElementById('config-json').value.trim();
  if (!raw) { resultEl.textContent = 'enter config JSON'; return; }
  let parsed;
  try { parsed = JSON.parse(raw); } catch (e) { resultEl.textContent = `invalid JSON: ${e.message}`; return; }
  sendCommand('apply_config', parsed);
};

function drawSpark(canvasId, samples, pick) {
  const canvas = document.getElementById(canvasId);
  const values = samples.map(pick).filter((v) => v != null && !Number.isNaN(v));
  const ctx = canvas.getContext('2d');
  const w = canvas.width = canvas.clientWidth || 300;
  const h = canvas.height = 40;
  ctx.clearRect(0, 0, w, h);
  if (values.length < 2) return;

  const min = Math.min(...values), max = Math.max(...values);
  const range = max - min || 1;
  const stepX = w / (values.length - 1);
  ctx.beginPath();
  values.forEach((v, i) => {
    const x = i * stepX;
    const y = h - 3 - ((v - min) / range) * (h - 6);
    i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
  });
  ctx.strokeStyle = '#60a5fa';
  ctx.lineWidth = 1.5;
  ctx.stroke();
}

async function loadHistory() {
  try {
    const { samples } = await (await fetch(`/api/devices/${encodeURIComponent(deviceId)}/history`)).json();
    document.getElementById('history-empty').classList.toggle('hidden', samples.length >= 2);
    drawSpark('spark-heap', samples, (s) => s.freeHeap);
    drawSpark('spark-rssi', samples, (s) => s.rssi);
  } catch { /* history is optional */ }
}

async function load() {
  try {
    const res = await fetch(`/api/devices/${encodeURIComponent(deviceId)}`);
    render(res.ok ? await res.json() : null);
  } catch { render(null); }
  loadHistory();
}

function connect() {
  const proto = location.protocol === 'https:' ? 'wss' : 'ws';
  const ws = new WebSocket(`${proto}://${location.host}/ws`);
  ws.onopen = () => { connEl.className = 'conn up'; connText.textContent = 'live'; };
  ws.onclose = () => { connEl.className = 'conn down'; connText.textContent = 'reconnecting…'; setTimeout(connect, 2000); };
  ws.onmessage = (ev) => {
    const msg = JSON.parse(ev.data);
    if (msg.type === 'snapshot') {
      render(msg.devices.find((d) => d.deviceId === deviceId) || null);
    } else if (msg.type === 'device' && msg.device.deviceId === deviceId) {
      render(msg.device);
    }
  };
}

load();
connect();
