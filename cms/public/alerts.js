'use strict';

const active = new Map();   // id -> alert
const connEl = document.getElementById('conn');
const connText = document.getElementById('conn-text');
const activeRows = document.getElementById('active-rows');
const recentRows = document.getElementById('recent-rows');
const activeEmpty = document.getElementById('active-empty');
const recentEmpty = document.getElementById('recent-empty');
const navBadge = document.getElementById('nav-badge');

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}
function fmtTs(t) { return t ? new Date(t).toLocaleString() : '—'; }
function dot(sev) { return `<span class="status-dot ${sev === 'critical' ? 'offline' : 'warn'}"></span>`; }
function devCell(id) { return `<a class="devlink" href="device.html?id=${encodeURIComponent(id)}">${escapeHtml(id)}</a>`; }

function renderActive() {
  const list = [...active.values()].sort((a, b) => b.raisedAt - a.raisedAt);
  activeEmpty.classList.toggle('hidden', list.length > 0);
  navBadge.textContent = String(list.length);
  navBadge.classList.toggle('hidden', list.length === 0);
  activeRows.innerHTML = list.map((a) =>
    `<tr><td>${dot(a.severity)}</td><td>${devCell(a.deviceId)}</td><td>${escapeHtml(a.type)}</td><td>${escapeHtml(a.message)}</td><td>${fmtTs(a.raisedAt)}</td></tr>`
  ).join('');
}

function renderRecent(list) {
  recentEmpty.classList.toggle('hidden', list.length > 0);
  recentRows.innerHTML = list.map((a) =>
    `<tr class="${a.resolvedAt ? 'offline-row' : ''}"><td>${dot(a.severity)}</td><td>${devCell(a.deviceId)}</td><td>${escapeHtml(a.type)}</td><td>${escapeHtml(a.message)}</td><td>${fmtTs(a.raisedAt)}</td><td>${a.resolvedAt ? fmtTs(a.resolvedAt) : '<span class="warn-text">active</span>'}</td></tr>`
  ).join('');
}

async function loadRecent() {
  try {
    const { recent } = await (await fetch('/api/alerts')).json();
    renderRecent(recent);
  } catch { /* ignore */ }
}

function applyAlert(alert) {
  if (alert.resolvedAt) active.delete(alert.id);
  else active.set(alert.id, alert);
  renderActive();
  loadRecent();
}

function connect() {
  const proto = location.protocol === 'https:' ? 'wss' : 'ws';
  const ws = new WebSocket(`${proto}://${location.host}/ws`);
  ws.onopen = () => { connEl.className = 'conn up'; connText.textContent = 'live'; };
  ws.onclose = () => { connEl.className = 'conn down'; connText.textContent = 'reconnecting…'; setTimeout(connect, 2000); };
  ws.onmessage = (ev) => {
    const msg = JSON.parse(ev.data);
    if (msg.type === 'alerts') {
      active.clear();
      for (const a of msg.active) active.set(a.id, a);
      renderActive();
    } else if (msg.type === 'alert') {
      applyAlert(msg.alert);
    }
  };
}

loadRecent();
connect();
