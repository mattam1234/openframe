<template>
  <v-row>
    <v-col cols="12" md="5">
      <v-card border flat>
        <v-card-title class="text-subtitle-1">{{ form.id ? `Edit: ${form.name}` : 'New job' }}</v-card-title>
        <v-card-text>
          <v-text-field v-model="form.name" label="Name" density="compact" variant="outlined" class="mb-2" hide-details />
          <v-text-field v-model="form.type" label="Command type" placeholder="reboot" density="compact" variant="outlined" class="mb-2" hide-details />
          <v-textarea v-model="form.payload" label="Payload JSON (optional)" rows="3" density="compact" variant="outlined" class="mb-2 mono" hide-details />
          <v-select v-model="form.mode" :items="['interval', 'daily']" label="Schedule" density="compact" variant="outlined" class="mb-2" hide-details />
          <v-text-field v-if="form.mode === 'interval'" v-model.number="form.intervalMin" type="number" min="1" label="Every N minutes" density="compact" variant="outlined" class="mb-2" hide-details />
          <v-text-field v-else v-model="form.time" type="time" label="Daily at (UTC)" density="compact" variant="outlined" class="mb-2" hide-details />
          <v-text-field v-model="form.tags" label="Target tags (comma-sep; blank = all)" density="compact" variant="outlined" class="mb-2" hide-details />
          <v-checkbox v-model="form.onlineOnly" label="online only" density="compact" class="mb-2" hide-details />
          <div class="d-flex align-center ga-2">
            <v-btn color="primary" @click="save">Save</v-btn>
            <v-btn variant="text" @click="reset">Reset</v-btn>
            <span class="text-caption text-medium-emphasis">{{ msg }}</span>
          </div>
        </v-card-text>
      </v-card>
    </v-col>

    <v-col cols="12" md="7">
      <v-card border flat>
        <v-card-title class="text-subtitle-1">Scheduled jobs</v-card-title>
        <v-table density="compact">
          <tbody>
            <tr v-for="j in jobs" :key="j.id">
              <th>
                {{ j.name }}<span v-if="!j.enabled" class="text-medium-emphasis"> (disabled)</span><br>
                <span class="mono text-medium-emphasis">{{ j.command.type }}</span>
              </th>
              <td>
                <div class="text-caption">{{ describeSchedule(j.schedule) }} · {{ describeTarget(j.target) }}</div>
                <div class="text-caption text-medium-emphasis mb-1">{{ lastRun(j) }}</div>
                <v-btn size="x-small" variant="tonal" class="mr-1" @click="run(j.id)">Run now</v-btn>
                <v-btn size="x-small" variant="tonal" class="mr-1" @click="edit(j)">Edit</v-btn>
                <v-btn size="x-small" variant="text" color="error" @click="remove(j.id)">Delete</v-btn>
              </td>
            </tr>
            <tr v-if="!jobs.length"><td class="text-medium-emphasis">No jobs yet.</td></tr>
          </tbody>
        </v-table>
      </v-card>
    </v-col>
  </v-row>
</template>

<script setup>
import { onMounted, reactive, ref } from 'vue'
import api from '../api'

const jobs = ref([])
const msg = ref('')
const form = reactive({ id: '', name: '', type: '', payload: '', mode: 'interval', intervalMin: 1440, time: '03:00', tags: '', onlineOnly: false })

const hhmmToMin = (t) => { const [h, m] = String(t || '').split(':').map(Number); return (h || 0) * 60 + (m || 0) }
const minToHhmm = (n) => `${String(Math.floor((n || 0) / 60)).padStart(2, '0')}:${String((n || 0) % 60).padStart(2, '0')}`
const describeSchedule = (s) => s.mode === 'interval' ? `every ${s.intervalMin} min` : s.mode === 'daily' ? `daily at ${minToHhmm(s.dailyMinute)} UTC` : '—'
function describeTarget(t) {
  if (t.deviceIds && t.deviceIds.length) return `${t.deviceIds.length} device(s)`
  if (t.tags && t.tags.length) return `tags: ${t.tags.join(', ')}${t.onlineOnly ? ' (online)' : ''}`
  return t.onlineOnly ? 'all online' : 'all devices'
}
const lastRun = (j) => j.lastResult ? `last: ${j.lastResult.ok} ok / ${j.lastResult.failed} failed` : 'never run'
const slugId = (name) => 'job-' + name.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '').slice(0, 32)

function reset() {
  Object.assign(form, { id: '', name: '', type: '', payload: '', mode: 'interval', intervalMin: 1440, time: '03:00', tags: '', onlineOnly: false })
  msg.value = ''
}
function edit(j) {
  Object.assign(form, {
    id: j.id, name: j.name, type: j.command.type,
    payload: j.command.payload == null ? '' : JSON.stringify(j.command.payload, null, 2),
    mode: j.schedule.mode, intervalMin: j.schedule.intervalMin || 1440, time: minToHhmm(j.schedule.dailyMinute || 0),
    tags: (j.target.tags || []).join(', '), onlineOnly: !!j.target.onlineOnly,
  })
  msg.value = ''
}

async function load() {
  try { jobs.value = (await api.get('/api/jobs')).jobs || [] } catch { /* ignore */ }
}

async function save() {
  if (!form.name.trim() || !form.type.trim()) { msg.value = 'name and command type are required'; return }
  let payload
  if (form.payload.trim()) {
    try { payload = JSON.parse(form.payload) } catch (e) { msg.value = `invalid payload JSON: ${e.message}`; return }
  }
  const tags = form.tags.split(',').map((t) => t.trim()).filter(Boolean)
  const body = {
    id: form.id || slugId(form.name), name: form.name.trim(), enabled: true,
    command: { type: form.type.trim(), payload },
    schedule: form.mode === 'interval'
      ? { mode: 'interval', intervalMin: Math.max(1, Number(form.intervalMin) || 0) }
      : { mode: 'daily', dailyMinute: hhmmToMin(form.time) },
    target: { tags: tags.length ? tags : undefined, onlineOnly: form.onlineOnly },
  }
  try {
    const saved = await api.post('/api/jobs', body)
    form.id = saved.id
    msg.value = 'saved ✓'
    load()
  } catch (e) { msg.value = `error: ${e.message}` }
}

async function run(id) {
  msg.value = 'running…'
  try {
    const b = await api.post(`/api/jobs/${encodeURIComponent(id)}/run`)
    msg.value = `ran: ${b.ok} ok / ${b.failed} failed`
    load()
  } catch (e) { msg.value = `error: ${e.message}` }
}

async function remove(id) {
  try {
    await fetch(`/api/jobs/${encodeURIComponent(id)}`, { method: 'DELETE' })
    if (form.id === id) reset()
    load()
  } catch { /* ignore */ }
}

onMounted(load)
</script>

<style scoped>
.mono :deep(textarea) { font-family: 'Roboto Mono', monospace; font-size: 0.85rem; }
.mono { font-family: 'Roboto Mono', monospace; }
</style>
