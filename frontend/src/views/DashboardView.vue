<template>
  <div>
    <h1 class="text-h5 mb-6">
      <v-icon class="mr-2">mdi-view-dashboard</v-icon>
      Dashboard
    </h1>

    <!-- Status cards -->
    <v-row>
      <v-col v-for="card in statusCards" :key="card.label" cols="12" sm="6" md="4" lg="3">
        <v-card>
          <v-card-text class="d-flex align-center">
            <v-icon :color="card.color" size="40" class="mr-4">{{ card.icon }}</v-icon>
            <div>
              <div class="text-caption text-medium-emphasis">{{ card.label }}</div>
              <div class="text-h6">{{ card.value }}</div>
            </div>
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>

    <!-- Connection status -->
    <v-row class="mt-2">
      <v-col cols="12">
        <v-card>
          <v-card-title>Connection Status</v-card-title>
          <v-card-text>
            <v-chip
              :color="wsStore.connected ? 'success' : 'error'"
              class="mr-2"
            >
              <v-icon start>{{ wsStore.connected ? 'mdi-wifi' : 'mdi-wifi-off' }}</v-icon>
              WebSocket {{ wsStore.connected ? 'Connected' : 'Disconnected' }}
            </v-chip>
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>
  </div>
</template>

<script setup>
import { computed, onMounted } from 'vue'
import { useWebSocketStore } from '../stores/websocket'
import { useDeviceStore } from '../stores/device'

const wsStore = useWebSocketStore()
const deviceStore = useDeviceStore()

onMounted(() => deviceStore.fetchStatus().catch(() => {}))

const h = computed(() => wsStore.health)

const statusCards = computed(() => [
  {
    label: 'Uptime',
    value: h.value.uptime ?? '—',
    icon: 'mdi-clock-outline',
    color: 'primary',
  },
  {
    label: 'Free Heap',
    value: h.value.freeHeap ? `${Math.round(h.value.freeHeap / 1024)} KB` : '—',
    icon: 'mdi-memory',
    color: 'secondary',
  },
  {
    label: 'WiFi RSSI',
    value: h.value.rssi ? `${h.value.rssi} dBm` : '—',
    icon: 'mdi-wifi',
    color: 'info',
  },
  {
    label: 'Firmware',
    value: h.value.version ?? '—',
    icon: 'mdi-chip',
    color: 'success',
  },
  {
    label: 'Board',
    value: h.value.board ?? '—',
    icon: 'mdi-developer-board',
    color: 'warning',
  },
  {
    label: 'Modules',
    value: h.value.moduleCount ?? '—',
    icon: 'mdi-expansion-card',
    color: 'purple',
  },
])
</script>
