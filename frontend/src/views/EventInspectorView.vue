<template>
  <div>
    <div class="d-flex align-center justify-space-between mb-3">
      <h1 class="text-h5">Event Inspector</h1>
      <div class="d-flex align-center ga-2">
        <v-chip size="small" :color="paused ? 'warning' : 'success'">{{ paused ? 'Paused' : 'Live' }}</v-chip>
        <v-btn size="small" variant="tonal" :prepend-icon="paused ? 'mdi-play' : 'mdi-pause'" @click="paused = !paused">
          {{ paused ? 'Resume' : 'Pause' }}
        </v-btn>
        <v-btn size="small" variant="text" prepend-icon="mdi-delete-sweep" @click="clearEvents">Clear</v-btn>
      </div>
    </div>

    <v-card>
      <v-card-text>
        <div class="d-flex ga-2 mb-2 flex-wrap">
          <v-text-field
            v-model="filter"
            label="Filter (type / source / payload)"
            density="compact"
            hide-details
            clearable
            style="max-width: 320px"
          />
          <v-select
            v-model="typeFilter"
            :items="knownTypes"
            label="Type"
            density="compact"
            hide-details
            clearable
            style="max-width: 240px"
          />
          <v-spacer />
          <span class="text-caption text-medium-emphasis align-self-center">
            {{ filtered.length }} shown · {{ wsStore.events.length }} buffered
          </span>
        </div>
        <v-table density="compact" class="event-table">
          <thead>
            <tr><th>Event</th><th>Source</th><th>Payload</th></tr>
          </thead>
          <tbody>
            <tr v-for="(e, idx) in filtered" :key="idx">
              <td><v-chip size="x-small" label>{{ e.event }}</v-chip></td>
              <td class="text-caption">{{ e.sourceId || e.source || '—' }}</td>
              <td class="text-caption font-monospace text-truncate" style="max-width: 480px">{{ payloadText(e) }}</td>
            </tr>
            <tr v-if="!filtered.length">
              <td colspan="3" class="text-medium-emphasis text-center py-4">
                No events yet — interact with the device (press an input, run an action) to see them stream in.
              </td>
            </tr>
          </tbody>
        </v-table>
      </v-card-text>
    </v-card>
  </div>
</template>

<script setup>
import { computed, ref, watch } from 'vue'
import { useWebSocketStore } from '../stores/websocket'

const wsStore = useWebSocketStore()
const filter = ref('')
const typeFilter = ref(null)
const paused = ref(false)
const frozen = ref([])      // snapshot taken while paused so the list stops updating
const clearRef = ref(null)  // boundary event; only events newer than it are shown

const knownTypes = computed(() => [...new Set(wsStore.events.map((e) => e.event).filter(Boolean))].sort())

// The store buffers newest-first and is shared, so we don't mutate it. "Clear"
// remembers the current newest event and we slice everything ahead of it.
const source = computed(() => {
  const arr = paused.value ? frozen.value : wsStore.events
  if (!clearRef.value) return arr
  const i = arr.indexOf(clearRef.value)
  return i >= 0 ? arr.slice(0, i) : arr
})

watch(paused, (p) => { if (p) frozen.value = wsStore.events.slice() })

function clearEvents() { clearRef.value = wsStore.events[0] ?? null }

const filtered = computed(() => {
  const q = (filter.value || '').trim().toLowerCase()
  return source.value.filter((e) => {
    if (typeFilter.value && e.event !== typeFilter.value) return false
    if (!q) return true
    return JSON.stringify(e).toLowerCase().includes(q)
  })
})

function payloadText(e) {
  const p = e.payload ?? e.data
  if (p == null) return ''
  return typeof p === 'string' ? p : JSON.stringify(p)
}
</script>

<style scoped>
.event-table { max-height: 65vh; overflow-y: auto; display: block; }
</style>
