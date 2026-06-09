'use strict';

const devices = new Map();
const treeEl = document.getElementById('tree');
const emptyEl = document.getElementById('empty');
const connEl = document.getElementById('conn');
const connText = document.getElementById('conn-text');

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

// Pure: group devices into the broker → {direct nodes, gateways→leaves}, plus
// any leaves whose gateway we don't (yet) know as a device.
function buildTopology(list) {
  const byGateway = new Map();   // gatewayId -> leaf[]
  const direct = [];
  for (const d of list) {
    if (d.via) {
      if (!byGateway.has(d.via)) byGateway.set(d.via, []);
      byGateway.get(d.via).push(d);
    } else {
      direct.push(d);
    }
  }
  const known = new Set(list.map((d) => d.deviceId));
  const directSorted = direct.sort((a, b) => a.deviceId.localeCompare(b.deviceId));
  const nodes = directSorted.map((d) => ({ device: d, leaves: (byGateway.get(d.deviceId) || []).sort((a, b) => a.deviceId.localeCompare(b.deviceId)) }));
  const orphans = [...byGateway.entries()]
    .filter(([gw]) => !known.has(gw))
    .map(([gw, leaves]) => ({ via: gw, leaves: leaves.sort((a, b) => a.deviceId.localeCompare(b.deviceId)) }));
  return { nodes, orphans };
}

function dot(online) { return `<span class="status-dot ${online ? 'online' : 'offline'}"></span>`; }
function label(d) {
  const name = escapeHtml(d.name || d.deviceId);
  const meta = [d.board, d.ip].filter(Boolean).map(escapeHtml).join(', ');
  return `${dot(d.online)} <a class="devlink" href="device.html?id=${encodeURIComponent(d.deviceId)}">${name}</a>${meta ? ` <span class="hint">(${meta})</span>` : ''}`;
}
function leafLine(d) {
  return `<div class="topo-leaf">${dot(d.online)} <a class="devlink" href="device.html?id=${encodeURIComponent(d.deviceId)}">${escapeHtml(d.name || d.deviceId)}</a> <span class="tag">esp-now</span></div>`;
}

function render() {
  const list = [...devices.values()];
  emptyEl.classList.toggle('hidden', list.length > 0);
  const { nodes, orphans } = buildTopology(list);

  let html = '<div class="topo-root">◉ MQTT broker / CMS</div>';
  for (const n of nodes) {
    const isGw = n.leaves.length > 0;
    html += `<div class="topo-node">${isGw ? '◆' : '●'} ${label(n.device)}${isGw ? ' <span class="tag">gateway</span>' : ''}</div>`;
    for (const leaf of n.leaves) html += leafLine(leaf);
  }
  for (const o of orphans) {
    html += `<div class="topo-node">◆ <span class="hint">unknown gateway ${escapeHtml(o.via)}</span></div>`;
    for (const leaf of o.leaves) html += leafLine(leaf);
  }
  treeEl.innerHTML = html;
}

// Exposed for tests / external drivers.
window.renderTopology = (list) => {
  devices.clear();
  for (const d of list) devices.set(d.deviceId, d);
  render();
};

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
      devices.set(msg.device.deviceId, msg.device);
      render();
    }
  };
}

connect();
