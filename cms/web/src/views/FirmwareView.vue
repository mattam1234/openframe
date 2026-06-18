<template>
  <div>
    <v-card border flat class="mb-4">
      <v-card-title class="text-subtitle-1">Upload firmware</v-card-title>
      <v-card-text class="d-flex flex-wrap align-center ga-3">
        <v-file-input
          v-model="file"
          label="Choose a .bin file"
          density="compact"
          variant="outlined"
          hide-details
          accept=".bin"
          style="max-width:360px"
        />
        <v-btn color="primary" :loading="uploading" @click="upload">Upload</v-btn>
        <span class="text-caption text-medium-emphasis">{{ uploadMsg }}</span>
      </v-card-text>
    </v-card>

    <v-card border flat>
      <v-card-title class="text-subtitle-1 d-flex flex-wrap align-center ga-3">
        <span>Images</span>
        <v-spacer />
        <v-text-field v-model="tagFilter" placeholder="deploy to tag(s)" density="compact" variant="outlined" hide-details style="max-width:160px" />
        <v-text-field v-model.number="canary" type="number" min="0" label="canary N" density="compact" variant="outlined" hide-details style="max-width:110px" />
        <v-checkbox v-model="onlineOnly" label="online only" density="compact" hide-details />
      </v-card-title>
      <v-table density="compact">
        <thead><tr><th>Name</th><th>Size</th><th>Modified</th><th class="text-right">Actions</th></tr></thead>
        <tbody>
          <tr v-for="f in files" :key="f.name">
            <td class="mono">{{ f.name }}</td>
            <td>{{ fmtSize(f.size) }}</td>
            <td>{{ fmtTs(f.mtime) }}</td>
            <td class="text-right">
              <v-btn size="x-small" variant="tonal" color="primary" :loading="deploying === f.name" @click="deploy(f.name)">Deploy</v-btn>
              <v-btn size="x-small" variant="text" color="error" @click="remove(f.name)">Delete</v-btn>
            </td>
          </tr>
          <tr v-if="!files.length"><td colspan="4" class="text-medium-emphasis">No firmware uploaded.</td></tr>
        </tbody>
      </v-table>
      <v-card-text v-if="deployResult" class="text-caption">{{ deployResult }}</v-card-text>
    </v-card>
  </div>
</template>

<script setup>
import { onMounted, ref } from 'vue'
import api from '../api'

const files = ref([])
const file = ref(null)
const uploading = ref(false)
const uploadMsg = ref('')
const tagFilter = ref('')
const canary = ref(0)
const onlineOnly = ref(true)
const deploying = ref('')
const deployResult = ref('')

const fmtSize = (b) => (b >= 1048576 ? `${(b / 1048576).toFixed(2)} MB` : `${(b / 1024).toFixed(1)} KB`)
const fmtTs = (t) => (t ? new Date(t).toLocaleString() : '—')

function targetSpec() {
  const tags = tagFilter.value.split(',').map((t) => t.trim()).filter(Boolean)
  const base = tags.length ? { tags, onlineOnly: onlineOnly.value } : { onlineOnly: onlineOnly.value }
  return canary.value > 0 ? { ...base, canary: canary.value } : base
}

async function load() {
  try { files.value = (await api.get('/api/firmware')).files || [] } catch { /* ignore */ }
}

async function upload() {
  // v-file-input gives a File (or array depending on Vuetify version).
  const f = Array.isArray(file.value) ? file.value[0] : file.value
  if (!f) { uploadMsg.value = 'choose a .bin file'; return }
  uploading.value = true
  uploadMsg.value = 'uploading…'
  try {
    const fd = new FormData()
    fd.append('file', f)
    const res = await fetch('/api/firmware', { method: 'POST', body: fd })
    const body = await res.json()
    uploadMsg.value = res.ok ? `uploaded ${body.name} (${fmtSize(body.size)})` : `error: ${body.error || res.status}`
    file.value = null
    load()
  } catch (e) { uploadMsg.value = `error: ${e.message}` }
  finally { uploading.value = false }
}

async function deploy(name) {
  deploying.value = name
  deployResult.value = `deploying ${name}…`
  try {
    const body = await api.post(`/api/firmware/${encodeURIComponent(name)}/deploy`, targetSpec())
    if (body.halted) {
      const failed = (body.canary || []).filter((r) => !r.ok).map((r) => r.deviceId)
      deployResult.value = `${name}: ${body.reason}` + (failed.length ? ` — no ack: ${failed.join(', ')}` : '')
    } else {
      const failed = (body.results || []).filter((r) => !r.ok)
      deployResult.value = `${name}: ${body.staged ? 'staged, ' : ''}accepted by ${body.ok}/${body.count}`
        + (failed.length ? ` — no ack: ${failed.map((r) => r.deviceId).join(', ')}` : '')
        + '. Watch the Fleet FW column.'
    }
  } catch (e) { deployResult.value = `error: ${e.message}` }
  finally { deploying.value = '' }
}

async function remove(name) {
  try { await fetch(`/api/firmware/${encodeURIComponent(name)}`, { method: 'DELETE' }); load() } catch { /* ignore */ }
}

onMounted(load)
</script>

<style scoped>
.mono { font-family: 'Roboto Mono', monospace; }
</style>
