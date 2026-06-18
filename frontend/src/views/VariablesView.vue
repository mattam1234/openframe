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

    <!-- Local variables -->
    <v-card class="mb-4" border flat>
      <v-card-title class="text-subtitle-1 d-flex align-center">
        <v-icon class="mr-2" size="small" color="primary">mdi-chip</v-icon>
        This node
        <v-chip size="x-small" class="ml-2">{{ localVars.length }}</v-chip>
      </v-card-title>
      <VarTable :rows="localVars" empty="No local variables." />
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

    <v-alert v-if="!localVars.length && !meshNodes.length" type="info" variant="tonal">
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

const localVars = computed(() =>
  filtered.value.filter((v) => !v.id.startsWith('node/')).sort((a, b) => a.id.localeCompare(b.id)),
)

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
    for (const v of data.variables || []) if (v && v.id) map[v.id] = v
    seed.value = map
  } catch { /* live store still fills in */ }
  finally { loading.value = false }
}

onMounted(refresh)
</script>

<style scoped>
.mono { font-family: 'Roboto Mono', 'Consolas', monospace; }
</style>
