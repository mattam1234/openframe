'use strict';

const idEl = document.getElementById('job-id');
const nameEl = document.getElementById('job-name');
const typeEl = document.getElementById('job-type');
const payloadEl = document.getElementById('job-payload');
const modeEl = document.getElementById('job-mode');
const intervalEl = document.getElementById('job-interval');
const timeEl = document.getElementById('job-time');
const tagsEl = document.getElementById('job-tags');
const onlineEl = document.getElementById('job-online');
const titleEl = document.getElementById('form-title');
const msgEl = document.getElementById('form-msg');
const listEl = document.getElementById('job-list');
const emptyEl = document.getElementById('job-empty');

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

const hhmmToMin = (t) => { const [h, m] = String(t || '').split(':').map(Number); return (h || 0) * 60 + (m || 0); };
const minToHhmm = (n) => `${String(Math.floor((n || 0) / 60)).padStart(2, '0')}:${String((n || 0) % 60).padStart(2, '0')}`;

function describeSchedule(s) {
  if (s.mode === 'interval') return `every ${s.intervalMin} min`;
  if (s.mode === 'daily') return `daily at ${minToHhmm(s.dailyMinute)} UTC`;
  return '—';
}

function describeTarget(t) {
  if (t.deviceIds && t.deviceIds.length) return `${t.deviceIds.length} device(s)`;
  if (t.tags && t.tags.length) return `tags: ${t.tags.join(', ')}${t.onlineOnly ? ' (online)' : ''}`;
  return t.onlineOnly ? 'all online' : 'all devices';
}

function resetForm() {
  idEl.value = ''; nameEl.value = ''; typeEl.value = ''; payloadEl.value = '';
  modeEl.value = 'interval'; intervalEl.value = '1440'; timeEl.value = '03:00';
  tagsEl.value = ''; onlineEl.checked = false;
  titleEl.textContent = 'New job';
  msgEl.textContent = '';
}

function editJob(j) {
  idEl.value = j.id;
  nameEl.value = j.name;
  typeEl.value = j.command.type;
  payloadEl.value = j.command.payload == null ? '' : JSON.stringify(j.command.payload, null, 2);
  modeEl.value = j.schedule.mode;
  intervalEl.value = j.schedule.intervalMin || 1440;
  timeEl.value = minToHhmm(j.schedule.dailyMinute || 0);
  tagsEl.value = (j.target.tags || []).join(', ');
  onlineEl.checked = !!j.target.onlineOnly;
  titleEl.textContent = `Edit: ${j.name}`;
  msgEl.textContent = '';
  window.scrollTo(0, 0);
}

async function loadList() {
  const { jobs } = await (await fetch('/api/jobs')).json();
  emptyEl.classList.toggle('hidden', jobs.length > 0);
  listEl.innerHTML = '';
  for (const j of jobs) {
    const last = j.lastResult ? `${j.lastResult.ok} ok / ${j.lastResult.failed} failed` : 'never run';
    const tr = document.createElement('tr');
    tr.innerHTML = `
      <th>${escapeHtml(j.name)}${j.enabled ? '' : ' <span class="mono">(disabled)</span>'}<br>
        <span class="mono">${escapeHtml(j.command.type)}</span></th>
      <td>${escapeHtml(describeSchedule(j.schedule))} · ${escapeHtml(describeTarget(j.target))}<br>
        <span class="hint">${escapeHtml(last)}</span><br><span class="actions"></span></td>`;
    const actions = tr.querySelector('.actions');
    const run = document.createElement('button');
    run.textContent = 'Run now'; run.onclick = () => runJob(j.id);
    const edit = document.createElement('button');
    edit.textContent = 'Edit'; edit.onclick = () => editJob(j);
    const del = document.createElement('button');
    del.textContent = 'Delete'; del.onclick = () => removeJob(j.id);
    actions.append(run, edit, del);
    listEl.appendChild(tr);
  }
}

async function runJob(id) {
  msgEl.textContent = 'running…';
  const res = await fetch(`/api/jobs/${encodeURIComponent(id)}/run`, { method: 'POST' });
  const b = await res.json().catch(() => ({}));
  msgEl.textContent = res.ok ? `ran: ${b.ok} ok / ${b.failed} failed` : `error: ${b.error || res.status}`;
  loadList();
}

async function removeJob(id) {
  await fetch(`/api/jobs/${encodeURIComponent(id)}`, { method: 'DELETE' });
  if (idEl.value === id) resetForm();
  loadList();
}

function slugId(name) {
  return 'job-' + name.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '').slice(0, 32);
}

async function save() {
  const name = nameEl.value.trim();
  const type = typeEl.value.trim();
  if (!name || !type) { msgEl.textContent = 'name and command type are required'; return; }

  let payload;
  const raw = payloadEl.value.trim();
  if (raw) {
    try { payload = JSON.parse(raw); }
    catch (e) { msgEl.textContent = `invalid payload JSON: ${e.message}`; return; }
  }

  const mode = modeEl.value;
  const tags = tagsEl.value.split(',').map((t) => t.trim()).filter(Boolean);
  const body = {
    id: idEl.value || slugId(name),
    name,
    enabled: true,
    command: { type, payload },
    schedule: mode === 'interval'
      ? { mode, intervalMin: Math.max(1, parseInt(intervalEl.value, 10) || 0) }
      : { mode, dailyMinute: hhmmToMin(timeEl.value) },
    target: { tags: tags.length ? tags : undefined, onlineOnly: onlineEl.checked },
  };

  const res = await fetch('/api/jobs', {
    method: 'POST',
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

document.getElementById('save-job').onclick = save;
document.getElementById('reset-job').onclick = resetForm;
loadList();
