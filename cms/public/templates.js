'use strict';

const idEl = document.getElementById('tpl-id');
const nameEl = document.getElementById('tpl-name');
const descEl = document.getElementById('tpl-desc');
const typeEl = document.getElementById('tpl-type');
const payloadEl = document.getElementById('tpl-payload');
const titleEl = document.getElementById('form-title');
const msgEl = document.getElementById('form-msg');
const listEl = document.getElementById('tpl-list');
const emptyEl = document.getElementById('tpl-empty');

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

function resetForm() {
  idEl.value = ''; nameEl.value = ''; descEl.value = ''; typeEl.value = ''; payloadEl.value = '';
  titleEl.textContent = 'New template';
  msgEl.textContent = '';
}

function editTemplate(t) {
  idEl.value = t.id;
  nameEl.value = t.name;
  descEl.value = t.description || '';
  typeEl.value = t.command.type;
  payloadEl.value = t.command.payload == null ? '' : JSON.stringify(t.command.payload, null, 2);
  titleEl.textContent = `Edit: ${t.name}`;
  msgEl.textContent = '';
  window.scrollTo(0, 0);
}

async function loadList() {
  const { templates } = await (await fetch('/api/templates')).json();
  emptyEl.classList.toggle('hidden', templates.length > 0);
  listEl.innerHTML = '';
  for (const t of templates) {
    const tr = document.createElement('tr');
    tr.innerHTML = `
      <th>${escapeHtml(t.name)}<br><span class="mono">${escapeHtml(t.command.type)}</span></th>
      <td>${t.description ? escapeHtml(t.description) + '<br>' : ''}<span class="actions"></span></td>`;
    const actions = tr.querySelector('.actions');
    const edit = document.createElement('button');
    edit.textContent = 'Edit'; edit.onclick = () => editTemplate(t);
    const del = document.createElement('button');
    del.textContent = 'Delete'; del.onclick = () => removeTemplate(t.id);
    actions.append(edit, del);
    listEl.appendChild(tr);
  }
}

async function removeTemplate(id) {
  await fetch(`/api/templates/${encodeURIComponent(id)}`, { method: 'DELETE' });
  if (idEl.value === id) resetForm();
  loadList();
}

async function save() {
  const name = nameEl.value.trim();
  const type = typeEl.value.trim();
  if (!name || !type) { msgEl.textContent = 'name and command type are required'; return; }

  let payload = null;
  const raw = payloadEl.value.trim();
  if (raw) {
    try { payload = JSON.parse(raw); }
    catch (e) { msgEl.textContent = `invalid payload JSON: ${e.message}`; return; }
  }

  const body = { name, description: descEl.value.trim() || undefined, command: { type, payload } };
  const id = idEl.value;
  const res = await fetch(id ? `/api/templates/${encodeURIComponent(id)}` : '/api/templates', {
    method: id ? 'PUT' : 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  if (!res.ok) { const b = await res.json().catch(() => ({})); msgEl.textContent = `error: ${b.error || res.status}`; return; }
  const saved = await res.json();
  idEl.value = saved.id;
  titleEl.textContent = `Edit: ${saved.name}`;
  msgEl.textContent = 'saved ✓';
  loadList();
}

document.getElementById('save-tpl').onclick = save;
document.getElementById('reset-tpl').onclick = resetForm;
loadList();
