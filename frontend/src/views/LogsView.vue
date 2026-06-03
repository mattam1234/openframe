<template>
  <div>
    <v-row>
      <v-col>
        <h1 class="text-h5 mb-4">
          <v-icon class="mr-2">mdi-text-box-outline</v-icon>
          Logs
        </h1>
      </v-col>
      <v-col cols="auto">
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

    <v-row>
      <v-col>
        <v-card>
          <v-card-title class="d-flex align-center justify-space-between">
            <span>Recent Entries</span>
            <v-chip size="small">{{ wsStore.logs.length }}</v-chip>
          </v-card-title>
          <v-card-text class="pa-0">
            <v-table density="compact">
              <thead>
                <tr>
                  <th class="text-left">Time</th>
                  <th class="text-left">Level</th>
                  <th class="text-left">Tag</th>
                  <th class="text-left">Message</th>
                </tr>
              </thead>
              <tbody>
                <tr v-for="entry in wsStore.logs" :key="`${entry.timestamp}-${entry.tag}-${entry.message}`">
                  <td>{{ formatTimestamp(entry.timestamp) }}</td>
                  <td class="text-uppercase">{{ entry.level || '—' }}</td>
                  <td>{{ entry.tag || '—' }}</td>
                  <td>{{ entry.message || '—' }}</td>
                </tr>
                <tr v-if="wsStore.logs.length === 0">
                  <td colspan="4" class="text-medium-emphasis">No log entries available.</td>
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
import { onMounted, ref } from 'vue'
import api from '../api/client'
import { useWebSocketStore } from '../stores/websocket'

const wsStore = useWebSocketStore()
const loading = ref(false)
const errorMessage = ref('')

const formatTimestamp = (value) => (
  value === undefined || value === null
    ? '—'
    : `${(value / 1000).toFixed(1)}s`
)

async function refreshLogs() {
  loading.value = true
  errorMessage.value = ''

  try {
    const response = await api.get('/api/logs')
    wsStore.setLogsSnapshot(response.entries || [])
  } catch (error) {
    errorMessage.value = error.message || 'Failed to load logs'
  } finally {
    loading.value = false
  }
}

onMounted(() => {
  refreshLogs()
})
</script>
