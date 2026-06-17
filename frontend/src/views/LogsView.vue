<template>
  <div>
    <v-row align="center">
      <v-col>
        <h1 class="text-h5 mb-0">
          <v-icon class="mr-2">mdi-text-box-outline</v-icon>
          Logs
        </h1>
      </v-col>
      <v-col cols="auto" class="d-flex align-center ga-2">
        <v-chip size="small" :color="wsStore.connected ? 'success' : 'error'" variant="flat">
          <v-icon start size="x-small">{{ wsStore.connected ? 'mdi-lan-connect' : 'mdi-lan-disconnect' }}</v-icon>
          {{ wsStore.connected ? 'Live' : 'Offline' }}
        </v-chip>
        <v-btn
          :color="paused ? 'warning' : undefined"
          :prepend-icon="paused ? 'mdi-play' : 'mdi-pause'"
          variant="tonal"
          @click="paused = !paused"
        >
          {{ paused ? 'Resume' : 'Pause' }}
        </v-btn>
        <v-btn prepend-icon="mdi-broom" variant="tonal" @click="clearLogs">Clear</v-btn>
        <v-btn :loading="loading" prepend-icon="mdi-refresh" variant="tonal" @click="refreshLogs">
          Refresh
        </v-btn>
      </v-col>
    </v-row>

    <v-row v-if="errorMessage">
      <v-col>
        <v-alert type="error" variant="tonal">{{ errorMessage }}</v-alert>
      </v-col>
    </v-row>

    <v-row dense class="mt-1">
      <v-col cols="12" sm="5">
        <v-text-field
          v-model="search"
          label="Search messages & tags"
          prepend-inner-icon="mdi-magnify"
          density="compact"
          variant="outlined"
          clearable
          hide-details
        />
      </v-col>
      <v-col cols="12" sm="4">
        <v-select
          v-model="selectedLevels"
          :items="levelOptions"
          label="Levels"
          density="compact"
          variant="outlined"
          multiple
          chips
          closable-chips
          hide-details
        />
      </v-col>
      <v-col cols="12" sm="3">
        <v-select
          v-model="selectedTag"
          :items="tagOptions"
          label="Tag"
          density="compact"
          variant="outlined"
          clearable
          hide-details
        />
      </v-col>
    </v-row>

    <v-row>
      <v-col>
        <v-card>
          <v-card-title class="d-flex align-center justify-space-between">
            <span>Recent Entries</span>
            <v-chip size="small">{{ filteredLogs.length }} / {{ displayLogs.length }}</v-chip>
          </v-card-title>
          <v-card-text class="pa-0">
            <v-table density="compact" class="log-table">
              <thead>
                <tr>
                  <th class="text-left">Time</th>
                  <th class="text-left">Level</th>
                  <th class="text-left">Tag</th>
                  <th class="text-left">Message</th>
                </tr>
              </thead>
              <tbody>
                <tr v-for="(entry, i) in filteredLogs" :key="`${entry.timestamp}-${entry.tag}-${i}`">
                  <td class="text-no-wrap">{{ formatTimestamp(entry.timestamp) }}</td>
                  <td>
                    <v-chip :color="levelColor(entry.level)" size="x-small" variant="flat" label>
                      {{ (entry.level || '—').toUpperCase() }}
                    </v-chip>
                  </td>
                  <td class="text-no-wrap">{{ entry.tag || '—' }}</td>
                  <td class="log-message">{{ entry.message || '—' }}</td>
                </tr>
                <tr v-if="filteredLogs.length === 0">
                  <td colspan="4" class="text-medium-emphasis">
                    {{ displayLogs.length === 0 ? 'No log entries available.' : 'No entries match the current filters.' }}
                  </td>
                </tr>
              </tbody>
            </v-table>
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>
  </div>
</template>

<script setup>
import { computed, onMounted, ref, watch } from 'vue'
import api from '../api/client'
import { useWebSocketStore } from '../stores/websocket'

const wsStore = useWebSocketStore()
const loading = ref(false)
const errorMessage = ref('')

const search = ref('')
const selectedLevels = ref([])
const selectedTag = ref(null)
const paused = ref(false)

// Frozen snapshot of the live log list, captured while paused so the tail stops
// scrolling underneath the operator. When live, we read straight from the store.
const frozenLogs = ref([])

const levelOptions = ['trace', 'debug', 'info', 'warning', 'error', 'fatal']
  .map((value) => ({ title: value.toUpperCase(), value }))

const levelColor = (level) => ({
  trace: 'grey',
  debug: 'blue-grey',
  info: 'info',
  warning: 'warning',
  error: 'error',
  fatal: 'red-darken-4',
}[level] || 'grey')

const formatTimestamp = (value) => (
  value === undefined || value === null
    ? '—'
    : `${(value / 1000).toFixed(1)}s`
)

// While paused, freeze the list at the moment Pause was toggled.
const displayLogs = computed(() => (paused.value ? frozenLogs.value : wsStore.logs))

const tagOptions = computed(() => {
  const tags = new Set()
  displayLogs.value.forEach((e) => { if (e.tag) tags.add(e.tag) })
  return Array.from(tags).sort()
})

const filteredLogs = computed(() => {
  const term = (search.value || '').trim().toLowerCase()
  const levels = selectedLevels.value
  const tag = selectedTag.value
  return displayLogs.value.filter((e) => {
    if (levels.length && !levels.includes(e.level)) return false
    if (tag && e.tag !== tag) return false
    if (term) {
      const haystack = `${e.tag || ''} ${e.message || ''}`.toLowerCase()
      if (!haystack.includes(term)) return false
    }
    return true
  })
})

function clearLogs() {
  wsStore.setLogsSnapshot([])
  frozenLogs.value = []
}

async function refreshLogs() {
  loading.value = true
  errorMessage.value = ''

  try {
    const response = await api.get('/api/logs')
    wsStore.setLogsSnapshot(response.entries || [])
    if (paused.value) frozenLogs.value = [...wsStore.logs]
  } catch (error) {
    errorMessage.value = error.message || 'Failed to load logs'
  } finally {
    loading.value = false
  }
}

// Capture the snapshot the instant we pause so the view stops updating.
watch(paused, (isPaused) => {
  if (isPaused) frozenLogs.value = [...wsStore.logs]
})

onMounted(() => {
  refreshLogs()
})
</script>

<style scoped>
.log-table {
  max-height: 70vh;
  overflow-y: auto;
}
.log-message {
  font-family: 'Roboto Mono', monospace;
  white-space: pre-wrap;
  word-break: break-word;
}
</style>
