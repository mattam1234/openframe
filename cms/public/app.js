'use strict';

const devices = new Map();
const selected = new Set();

const rowsEl = document.getElementById('rows');
const emptyEl = document.getElementById('empty');
const summaryEl = document.getElementById('summary');
const connEl = document.getElementById('conn');
const connText = document.getElementById('conn-text');
const selcountEl = document.getElementById('selcount');
const onlineOnlyEl = document.getElementById('online-only');
const selectAllEl = document.getElementById('select-all');
const tplSelectEl = document.getElementById('tpl-select');
const bulkResultEl = document.getElementById('bulk-result');
const navBadge = document.getElementById('nav-badge');
const tagFilterEl = document.getElementById('tag-filter');
const searchEl = document.getElementById('search');

function filterTags() {
  return tagFilterEl.value.split(',').map((t) => t.trim()).filter(Boolean);
}

// Free-text search across the visible/identifying fields, incl. notes (#64).
function matchesSearch(d) {
  const q = (searchEl?.value || '').trim().toLowerCase();
  if (!q) return true;
  const hay = [d.deviceId, d.name, d.board, d.ip, d.notes, (d.tags || []).join(' ')]
    .filter(Boolean).join(' ').toLowerCase();
  return hay.includes(q);
}

const activeAlerts = new Map();
function updateAlertBadge() {
  if (!navBadge) return;
  navBadge.textContent = String(activeAlerts.size);
  navBadge.classList.toggle('hidden', activeAlerts.size === 0);
}

// Coerce a device-supplied value to a finite number, or null. Numeric fields
// arrive from (semi-trusted) device heartbeats and must never reach innerHTML as
// raw strings — this guarantees only computed numbers are interpolated.
function asNum(v) { const n = Number(v); return Number.isFinite(n) ? n : null; }
function fmtUptime(ms) {
  const n = asNum(ms);
  if (n == null) return '—';
  const s = Math.floor(n / 1000), d = Math.floor(s / 86400), h = Math.floor((s % 86400) / 3600), m = Math.floor((s % 3600) / 60);
  if (d) return `${d}d ${h}h`;
  if (h) return `${h}h ${m}m`;
  return `${m}m`;
}
function fmtHeap(b) { const n = asNum(b); return n == null ? '—' : `${(n / 1024).toFixed(0)} KB`; }
function fmtAgo(ts) {
  const n = asNum(ts);
  if (!n) return 'never';
  const s = Math.floor((Date.now() - n) / 1000);
  if (s < 60) return `${s}s ago`;
  if (s < 3600) return `${Math.floor(s / 60)}m ago`;
  return `${Math.floor(s / 3600)}h ago`;
}
function rssiLabel(r) { const n = asNum(r); return n == null || n === 0 ? '—' : `${n} dBm`; }
function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

async function sendCommand(deviceId, type, btn) {
  const original = btn.textContent;
  btn.disabled = true; btn.textContent = '…';
  try {
    const res = await fetch(`/api/devices/${encodeURIComponent(deviceId)}/cmd`, {
      method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ type }),
    });
    const body = await res.json();
    btn.textContent = res.ok && body.ok ? '✓' : '✗';
  } catch { btn.textContent = '✗'; }
  finally { setTimeout(() => { btn.textContent = original; btn.disabled = false; }, 1200); }
}

// Target spec for bulk operations. Precedence: explicit selection → tag filter →
// whole fleet (the online-only toggle applies to the latter two).
function targetSpec() {
  if (selected.size) return { deviceIds: [...selected] };
  const tags = filterTags();
  if (tags.length) return { tags, onlineOnly: onlineOnlyEl.checked };
  return { onlineOnly: onlineOnlyEl.checked };
}

async function bulkPost(url, extra) {
  bulkResultEl.textContent = 'working…';
  try {
    const res = await fetch(url, {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ...targetSpec(), ...extra }),
    });
    const body = await res.json();
    if (!res.ok) { bulkResultEl.textContent = `error: ${body.error || res.status}`; return; }
    const failed = body.results.filter((r) => !r.ok);
    bulkResultEl.textContent =
      `${extra.label || extra.type || 'deploy'}: ${body.ok}/${body.count} ok` +
      (failed.length ? ` — failed: ${failed.map((r) => r.deviceId).join(', ')}` : '');
  } catch (e) { bulkResultEl.textContent = `error: ${e.message}`; }
}

function updateSelectionUI() {
  const list = [...devices.values()];
  if (selected.size) {
    selcountEl.textContent = `${selected.size} selected`;
  } else {
    const tags = filterTags();
    selcountEl.textContent = tags.length
      ? `tagged: ${tags.join(', ')}`
      : `all devices (${list.filter((d) => d.online).length} online)`;
  }
  if (list.length) selectAllEl.checked = list.every((d) => selected.has(d.deviceId));
}

function render() {
  const all = [...devices.values()].sort((a, b) => a.deviceId.localeCompare(b.deviceId));
  const list = all.filter(matchesSearch);
  const online = list.filter((d) => d.online).length;
  const filtered = all.length - list.length;
  summaryEl.textContent = `${list.length} device(s) · ${online} online · ${list.length - online} offline`
    + (filtered ? ` · ${filtered} hidden by search` : '');
  emptyEl.classList.toggle('hidden', list.length > 0);

  rowsEl.innerHTML = '';
  for (const d of list) {
    const tr = document.createElement('tr');
    if (!d.online) tr.className = 'offline-row';
    tr.innerHTML = `
      <td><input type="checkbox" class="sel" ${selected.has(d.deviceId) ? 'checked' : ''} /></td>
      <td><span class="status-dot ${d.online ? 'online' : 'offline'}" title="${d.online ? 'online' : 'offline'}"></span></td>
      <td><a class="devlink" href="device.html?id=${encodeURIComponent(d.deviceId)}">${escapeHtml(d.name || '—')}</a></td>
      <td class="mono">${escapeHtml(d.deviceId)}</td>
      <td>${escapeHtml(d.board || '—')}</td>
      <td class="mono">${escapeHtml(d.version || '—')}</td>
      <td class="mono">${escapeHtml(d.ip || '—')}</td>
      <td>${rssiLabel(d.rssi)}</td>
      <td>${fmtHeap(d.freeHeap)}</td>
      <td>${fmtUptime(d.uptimeMs)}</td>
      <td>${escapeHtml(d.activeProfileId || '—')}</td>
      <td>${(d.tags || []).map((t) => `<span class="tag">${escapeHtml(t)}</span>`).join(' ') || '—'}</td>
      <td>${fmtAgo(d.lastSeen)}</td>
      <td></td>`;
    tr.querySelector('.sel').onchange = (e) => {
      if (e.target.checked) selected.add(d.deviceId); else selected.delete(d.deviceId);
      updateSelectionUI();
    };
    const actions = tr.lastElementChild;
    for (const [type, label] of [['identify', 'Identify'], ['get_status', 'Refresh'], ['reboot', 'Reboot']]) {
      const btn = document.createElement('button');
      btn.textContent = label; btn.disabled = !d.online;
      btn.onclick = () => sendCommand(d.deviceId, type, btn);
      actions.appendChild(btn);
    }
    rowsEl.appendChild(tr);
  }
  updateSelectionUI();
}

function upsert(device) { devices.set(device.deviceId, device); render(); }

async function loadTemplates() {
  try {
    const { templates } = await (await fetch('/api/templates')).json();
    tplSelectEl.innerHTML = '<option value="">Template…</option>' +
      templates.map((t) => `<option value="${escapeHtml(t.id)}">${escapeHtml(t.name)} (${escapeHtml(t.command.type)})</option>`).join('');
  } catch { /* templates are optional */ }
}

// Bulk action wiring
document.querySelectorAll('button[data-bulk]').forEach((btn) => {
  btn.onclick = () => bulkPost('/api/commands', { type: btn.dataset.bulk, label: btn.dataset.bulk });
});
document.getElementById('deploy-tpl').onclick = () => {
  const id = tplSelectEl.value;
  if (!id) { bulkResultEl.textContent = 'pick a template first'; return; }
  bulkPost(`/api/templates/${encodeURIComponent(id)}/deploy`, { label: `deploy ${tplSelectEl.selectedOptions[0].textContent}` });
};
selectAllEl.onchange = () => {
  if (selectAllEl.checked) for (const id of devices.keys()) selected.add(id);
  else selected.clear();
  render();
};
tagFilterEl.oninput = updateSelectionUI;
if (searchEl) searchEl.oninput = render;

function connect() {
  const proto = location.protocol === 'https:' ? 'wss' : 'ws';
  const ws = new WebSocket(`${proto}://${location.host}/ws`);
  ws.onopen = () => { connEl.className = 'conn up'; connText.textContent = 'live'; };
  ws.onclose = () => { connEl.className = 'conn down'; connText.textContent = 'reconnecting…'; setTimeout(connect, 2000); };
  ws.onmessage = (ev) => {
    const msg = JSON.parse(ev.data);
    if (msg.type === 'snapshot') {
      devices.clear();
      for (const d of msg.devices) devices.set(d.deviceId, d);
      render();
    } else if (msg.type === 'device') {
      upsert(msg.device);
    } else if (msg.type === 'alerts') {
      activeAlerts.clear();
      for (const a of msg.active) activeAlerts.set(a.id, a);
      updateAlertBadge();
    } else if (msg.type === 'alert') {
      if (msg.alert.resolvedAt) activeAlerts.delete(msg.alert.id);
      else activeAlerts.set(msg.alert.id, msg.alert);
      updateAlertBadge();
    }
  };
}

setInterval(render, 10_000);
loadTemplates();
connect();
