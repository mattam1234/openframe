'use strict';

const rowsEl = document.getElementById('rows');
const emptyEl = document.getElementById('empty');
const fileEl = document.getElementById('file');
const uploadMsg = document.getElementById('upload-msg');
const tagFilterEl = document.getElementById('tag-filter');
const onlineOnlyEl = document.getElementById('online-only');
const deployResult = document.getElementById('deploy-result');

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}
function fmtSize(b) { return b >= 1024 * 1024 ? `${(b / 1048576).toFixed(2)} MB` : `${(b / 1024).toFixed(1)} KB`; }
function fmtTs(t) { return t ? new Date(t).toLocaleString() : '—'; }

function targetSpec() {
  const tags = tagFilterEl.value.split(',').map((t) => t.trim()).filter(Boolean);
  const canaryEl = document.getElementById('canary');
  const canary = canaryEl ? Math.max(0, parseInt(canaryEl.value, 10) || 0) : 0;
  const base = tags.length ? { tags, onlineOnly: onlineOnlyEl.checked } : { onlineOnly: onlineOnlyEl.checked };
  return canary > 0 ? { ...base, canary } : base;
}

async function load() {
  const { files } = await (await fetch('/api/firmware')).json();
  emptyEl.classList.toggle('hidden', files.length > 0);
  rowsEl.innerHTML = '';
  for (const f of files) {
    const tr = document.createElement('tr');
    tr.innerHTML = `<td class="mono">${escapeHtml(f.name)}</td><td>${fmtSize(f.size)}</td><td>${fmtTs(f.mtime)}</td><td class="actions"></td>`;
    const actions = tr.querySelector('.actions');
    const deploy = document.createElement('button');
    deploy.textContent = 'Deploy';
    deploy.onclick = () => deployFirmware(f.name, deploy);
    const del = document.createElement('button');
    del.textContent = 'Delete';
    del.onclick = () => removeFirmware(f.name);
    actions.append(deploy, del);
    rowsEl.appendChild(tr);
  }
}

async function deployFirmware(name, btn) {
  btn.disabled = true;
  deployResult.textContent = `deploying ${name}…`;
  try {
    const res = await fetch(`/api/firmware/${encodeURIComponent(name)}/deploy`, {
      method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(targetSpec()),
    });
    const body = await res.json();
    if (!res.ok) { deployResult.textContent = `error: ${body.error || res.status}`; return; }
    if (body.halted) {
      const failed = (body.canary || []).filter((r) => !r.ok).map((r) => r.deviceId);
      deployResult.textContent = `${name}: ${body.reason}` + (failed.length ? ` — no ack: ${failed.join(', ')}` : '');
      return;
    }
    const results = body.results || [];
    const failed = results.filter((r) => !r.ok);
    deployResult.textContent = `${name}: ${body.staged ? 'staged, ' : ''}accepted by ${body.ok}/${body.count}` +
      (failed.length ? ` — no ack: ${failed.map((r) => r.deviceId).join(', ')}` : '') +
      `. Watch the Fleet version column.`;
  } catch (e) { deployResult.textContent = `error: ${e.message}`; }
  finally { btn.disabled = false; }
}

async function removeFirmware(name) {
  await fetch(`/api/firmware/${encodeURIComponent(name)}`, { method: 'DELETE' });
  load();
}

document.getElementById('upload').onclick = async () => {
  const file = fileEl.files[0];
  if (!file) { uploadMsg.textContent = 'choose a .bin file'; return; }
  uploadMsg.textContent = 'uploading…';
  try {
    const fd = new FormData();
    fd.append('file', file);
    const res = await fetch('/api/firmware', { method: 'POST', body: fd });
    const body = await res.json();
    uploadMsg.textContent = res.ok ? `uploaded ${body.name} (${fmtSize(body.size)})` : `error: ${body.error || res.status}`;
    fileEl.value = '';
    load();
  } catch (e) { uploadMsg.textContent = `error: ${e.message}`; }
};

load();
