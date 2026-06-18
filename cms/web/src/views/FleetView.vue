<template>
  <div>
    <!-- Toolbar -->
    <v-card class="mb-3" border flat>
      <v-card-text class="d-flex flex-wrap align-center ga-3">
        <span class="text-body-2 text-medium-emphasis">{{ summary }}</span>
        <v-spacer />
        <span class="text-caption">{{ selectionLabel }}</span>
        <v-checkbox v-model="onlineOnly" label="online only" density="compact" hide-details />
        <v-divider vertical />
        <v-btn size="small" variant="tonal" :loading="busy === 'identify'" @click="bulk('identify')">Identify</v-btn>
        <v-btn size="small" variant="tonal" :loading="busy === 'get_status'" @click="bulk('get_status')">Refresh</v-btn>
        <v-btn size="small" variant="tonal" color="warning" :loading="busy === 'reboot'" @click="bulk('reboot')">Reboot</v-btn>
        <v-divider vertical />
        <v-text-field
          v-model="tagFilter"
          placeholder="filter by tag(s)"
          density="compact"
          variant="outlined"
          hide-details
          style="max-width:160px"
        />
        <v-text-field
          v-model="search"
          placeholder="search name / id / notes"
          prepend-inner-icon="mdi-magnify"
          density="compact"
          variant="outlined"
          hide-details
          style="max-width:220px"
        />
        <v-divider vertical />
        <v-select
          v-model="templateId"
          :items="templateItems"
          placeholder="Template…"
          density="compact"
          variant="outlined"
          hide-details
          style="max-width:200px"
        />
        <v-btn size="small" variant="tonal" color="primary" :loading="busy === 'deploy'" @click="deployTemplate">Deploy</v-btn>
      </v-card-text>
    </v-card>

    <v-alert v-if="bulkResult" type="info" variant="tonal" density="compact" class="mb-3" closable @click:close="bulkResult = ''">
      {{ bulkResult }}
    </v-alert>

    <v-data-table
      v-model="selectedIds"
      :headers="headers"
      :items="filteredDevices"
      :search="search"
      item-value="deviceId"
      show-select
      density="comfortable"
      class="fleet-table"
      :no-data-text="'No devices yet. Once a device with MQTT enabled connects, it appears here.'"
    >
      <template #[`item.online`]="{ item }">
        <span class="status-dot" :class="item.online ? 'online' : 'offline'" :title="item.online ? 'online' : 'offline'" />
      </template>
      <template #[`item.name`]="{ item }">
        <router-link :to="{ name: 'device', params: { id: item.deviceId } }" class="devlink">
          {{ item.name || '—' }}
        </router-link>
      </template>
      <template #[`item.rssi`]="{ item }">{{ rssiLabel(item.rssi) }}</template>
      <template #[`item.freeHeap`]="{ item }">{{ fmtHeap(item.freeHeap) }}</template>
      <template #[`item.heapTrend`]="{ item }">
        <Sparkline :values="heapHistory[item.deviceId] || []" :width="70" :height="22" color="#60a5fa" />
      </template>
      <template #[`item.uptimeMs`]="{ item }">{{ fmtUptime(item.uptimeMs) }}</template>
      <template #[`item.tags`]="{ item }">
        <v-chip v-for="t in item.tags || []" :key="t" size="x-small" class="mr-1" label>{{ t }}</v-chip>
        <span v-if="!(item.tags || []).length" class="text-medium-emphasis">—</span>
      </template>
      <template #[`item.lastSeen`]="{ item }">{{ fmtAgo(item.lastSeen) }}</template>
      <template #[`item.actions`]="{ item }">
        <v-btn size="x-small" variant="text" :disabled="!item.online" @click="cmd(item.deviceId, 'identify')">Identify</v-btn>
        <v-btn size="x-small" variant="text" :disabled="!item.online" @click="cmd(item.deviceId, 'get_status')">Refresh</v-btn>
        <v-btn size="x-small" variant="text" color="warning" :disabled="!item.online" @click="cmd(item.deviceId, 'reboot')">Reboot</v-btn>
      </template>
    </v-data-table>
  </div>
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import { Sparkline } from '@shared'
import api from '../api'
import { useFleet } from '../store'
import { fmtUptime, fmtHeap, fmtAgo, rssiLabel } from '../format'

const { devicesInSite, heapHistory } = useFleet()

const selectedIds = ref([])
const templates = ref([])
const templateId = ref(null)
const onlineOnly = ref(true)
const tagFilter = ref('')
const search = ref('')
const bulkResult = ref('')
const busy = ref('')

const headers = [
  { title: '', key: 'online', sortable: false, width: 36 },
  { title: 'Device', key: 'name' },
  { title: 'ID', key: 'deviceId' },
  { title: 'Board', key: 'board' },
  { title: 'FW', key: 'version' },
  { title: 'IP', key: 'ip' },
  { title: 'RSSI', key: 'rssi' },
  { title: 'Heap', key: 'freeHeap' },
  { title: 'Trend', key: 'heapTrend', sortable: false },
  { title: 'Uptime', key: 'uptimeMs' },
  { title: 'Profile', key: 'activeProfileId' },
  { title: 'Tags', key: 'tags', sortable: false },
  { title: 'Last seen', key: 'lastSeen' },
  { title: 'Actions', key: 'actions', sortable: false },
]

const templateItems = computed(() =>
  templates.value.map((t) => ({ title: `${t.name} (${t.command.type})`, value: t.id })),
)

const wantedTags = computed(() => tagFilter.value.split(',').map((t) => t.trim()).filter(Boolean))

const filteredDevices = computed(() => {
  if (!wantedTags.value.length) return devicesInSite.value
  return devicesInSite.value.filter((d) => (d.tags || []).some((t) => wantedTags.value.includes(t)))
})

const summary = computed(() => {
  const list = filteredDevices.value
  const online = list.filter((d) => d.online).length
  return `${list.length} device(s) · ${online} online · ${list.length - online} offline`
})

const selectionLabel = computed(() => {
  if (selectedIds.value.length) return `${selectedIds.value.length} selected`
  if (wantedTags.value.length) return `tagged: ${wantedTags.value.join(', ')}`
  return `all devices (${devicesInSite.value.filter((d) => d.online).length} online)`
})

// Target spec: explicit selection → tag filter → whole fleet (online-only applies
// to the latter two), mirroring the vanilla UI.
function targetSpec() {
  if (selectedIds.value.length) return { deviceIds: [...selectedIds.value] }
  if (wantedTags.value.length) return { tags: wantedTags.value, onlineOnly: onlineOnly.value }
  return { onlineOnly: onlineOnly.value }
}

async function bulk(type) {
  busy.value = type
  bulkResult.value = 'working…'
  try {
    const body = await api.post('/api/commands', { type, ...targetSpec() })
    const failed = (body.results || []).filter((r) => !r.ok)
    bulkResult.value = `${type}: ${body.ok}/${body.count} ok`
      + (failed.length ? ` — failed: ${failed.map((r) => r.deviceId).join(', ')}` : '')
  } catch (e) { bulkResult.value = `error: ${e.message}` }
  finally { busy.value = '' }
}

async function deployTemplate() {
  if (!templateId.value) { bulkResult.value = 'pick a template first'; return }
  busy.value = 'deploy'
  bulkResult.value = 'working…'
  try {
    const body = await api.post(`/api/templates/${encodeURIComponent(templateId.value)}/deploy`, targetSpec())
    bulkResult.value = `deploy: ${body.ok}/${body.count} ok`
  } catch (e) { bulkResult.value = `error: ${e.message}` }
  finally { busy.value = '' }
}

async function cmd(deviceId, type) {
  try { await api.post(`/api/devices/${encodeURIComponent(deviceId)}/cmd`, { type }) } catch { /* surfaced elsewhere */ }
}

onMounted(async () => {
  try { templates.value = (await api.get('/api/templates')).templates || [] } catch { /* optional */ }
})
</script>

<style scoped>
.status-dot {
  display: inline-block;
  width: 10px;
  height: 10px;
  border-radius: 50%;
}
.status-dot.online { background: #22c55e; box-shadow: 0 0 6px rgba(34, 197, 94, 0.6); }
.status-dot.offline { background: #6b7280; }
.devlink { color: rgb(var(--v-theme-primary)); text-decoration: none; font-weight: 600; }
.devlink:hover { text-decoration: underline; }
.fleet-table :deep(td) { white-space: nowrap; }
</style>
