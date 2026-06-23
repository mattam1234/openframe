<template>
  <div>
    <v-row align="center" class="mb-1">
      <v-col>
        <h1 class="text-h5 mb-0">
          <v-icon class="mr-2">mdi-variable</v-icon>
          Variables
        </h1>
        <div class="text-medium-emphasis text-caption">
          Live values on this node, including those mirrored from mesh peers (<code>node/&lt;id&gt;/&lt;name&gt;</code>)
        </div>
      </v-col>
      <v-col cols="auto" class="d-flex align-center ga-2">
        <v-text-field
          v-model="search"
          placeholder="filter by name / value"
          prepend-inner-icon="mdi-magnify"
          density="compact"
          variant="outlined"
          hide-details
          style="min-width:240px"
        />
        <v-btn :loading="loading" prepend-icon="mdi-refresh" variant="tonal" @click="refresh">Refresh</v-btn>
      </v-col>
    </v-row>

    <!-- Local variables, grouped by source (F3): Local / Sensors / Inputs / Outputs -->
    <v-card v-for="g in localGroups" :key="g.key" class="mb-4" border flat>
      <v-card-title class="text-subtitle-1 d-flex align-center">
        <v-icon class="mr-2" size="small" :color="g.color">{{ g.icon }}</v-icon>
        {{ g.title }}
        <v-chip size="x-small" class="ml-2">{{ g.rows.length }}</v-chip>
      </v-card-title>
      <VarTable :rows="g.rows" />
    </v-card>

    <!-- Mesh peers -->
    <v-card v-for="node in meshNodes" :key="node.id" class="mb-4" border flat>
      <v-card-title class="text-subtitle-1 d-flex align-center">
        <v-icon class="mr-2" size="small" color="teal">mdi-access-point</v-icon>
        Node <span class="mono ml-1">{{ node.id }}</span>
        <v-chip size="x-small" class="ml-2">{{ node.rows.length }}</v-chip>
      </v-card-title>
      <VarTable :rows="node.rows" />
    </v-card>

    <v-alert v-if="!localGroups.length && !meshNodes.length" type="info" variant="tonal">
      No variables{{ search ? ' match the filter' : ' yet' }}. Mesh peers appear here once a gateway relays their values.
    </v-alert>
  </div>
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import api from '../api/client'
import { useWebSocketStore } from '../stores/websocket'
import VarTable from '../components/VarTable.vue'

const wsStore = useWebSocketStore()
const seed = ref({})
const loading = ref(false)
const search = ref('')

// Live store values win over the initial REST seed.
const allVars = computed(() => Object.values({ ...seed.value, ...wsStore.variables }))

function matches(v) {
  const q = search.value.trim().toLowerCase()
  if (!q) return true
  return `${v.id} ${v.label ?? ''} ${v.value ?? ''}`.toLowerCase().includes(q)
}

const filtered = computed(() => allVars.value.filter(matches))

// F3 — categorise this node's variables by source. The firmware tags each variable
// with `source` (sensor/input/output/local); fall back to the id prefix if absent.
const SOURCE_GROUPS = [
  { key: 'local',  title: 'This node', icon: 'mdi-chip',               color: 'primary' },
  { key: 'sensor', title: 'Sensors',   icon: 'mdi-thermometer',        color: 'orange' },
  { key: 'input',  title: 'Inputs',    icon: 'mdi-gesture-tap-button', color: 'green' },
  { key: 'output', title: 'Outputs',   icon: 'mdi-led-on',             color: 'purple' },
]

function sourceOf(v) {
  if (v.source) return v.source
  if (v.id.startsWith('sensor.')) return 'sensor'
  if (v.id.startsWith('input.')) return 'input'
  if (v.id.startsWith('output.')) return 'output'
  return 'local'
}

const localGroups = computed(() => {
  const buckets = {}
  for (const v of filtered.value) {
    if (v.id.startsWith('node/')) continue
    const s = sourceOf(v)
    const key = ['sensor', 'input', 'output'].includes(s) ? s : 'local'
    ;(buckets[key] ||= []).push(v)
  }
  return SOURCE_GROUPS
    .map((g) => ({ ...g, rows: (buckets[g.key] || []).sort((a, b) => a.id.localeCompare(b.id)) }))
    .filter((g) => g.rows.length)
})

// Group mesh vars (node/<srcId>/<name>) by source node, exposing the bare name.
const meshNodes = computed(() => {
  const byNode = new Map()
  for (const v of filtered.value) {
    if (!v.id.startsWith('node/')) continue
    const parts = v.id.split('/')
    const nodeId = parts[1] || '?'
    const name = parts.slice(2).join('/') || v.id
    if (!byNode.has(nodeId)) byNode.set(nodeId, [])
    byNode.get(nodeId).push({ ...v, name })
  }
  return [...byNode.entries()]
    .sort(([a], [b]) => a.localeCompare(b))
    .map(([id, rows]) => ({ id, rows: rows.sort((a, b) => a.name.localeCompare(b.name)) }))
})

async function refresh() {
  loading.value = true
  try {
    const data = await api.get('/api/variables')
    const map = {}
    // /api/variables returns an id-keyed object (matching the live store), not
    // an array — normalize either shape before iterating.
    const list = Array.isArray(data.variables)
      ? data.variables
      : Object.values(data.variables || {})
    for (const v of list) if (v && v.id) map[v.id] = v
    seed.value = map
  } catch { /* live store still fills in */ }
  finally { loading.value = false }
}

onMounted(refresh)
</script>

<style scoped>
.mono { font-family: 'Roboto Mono', 'Consolas', monospace; }
</style>
