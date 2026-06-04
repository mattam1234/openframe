<template>
  <div>
    <h1 class="text-h5 mb-6">
      <v-icon class="mr-2">mdi-view-dashboard</v-icon>
      Dashboard
    </h1>

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

    <v-row class="mt-2">
      <v-col cols="12" md="6">
        <v-card>
          <v-card-title>Connection Status</v-card-title>
          <v-card-text class="d-flex flex-column ga-3">
            <div>
              <v-chip :color="wsStore.connected ? 'success' : 'error'" class="mr-2">
                <v-icon start>{{ wsStore.connected ? 'mdi-wifi' : 'mdi-wifi-off' }}</v-icon>
                WebSocket {{ wsStore.connected ? 'Connected' : 'Disconnected' }}
              </v-chip>
              <v-chip :color="status.apMode ? 'warning' : 'info'">
                <v-icon start>{{ status.apMode ? 'mdi-access-point' : 'mdi-router-wireless' }}</v-icon>
                {{ status.apMode ? 'Access Point Mode' : 'Station Mode' }}
              </v-chip>
            </div>

            <div class="text-body-2">
              <div><strong>Device:</strong> {{ status.name || '—' }}</div>
              <div><strong>IP Address:</strong> {{ status.ip || '—' }}</div>
              <div><strong>Variables:</strong> {{ status.variableCount ?? '—' }}</div>
              <div><strong>Logs:</strong> {{ status.logCount ?? '—' }}</div>
              <div v-if="status.rebootReason"><strong>Last Reboot:</strong> {{ status.rebootReason }}</div>
              <div v-if="status.activeProfileId"><strong>Active Profile:</strong> {{ status.activeProfileId }}</div>
            </div>
          </v-card-text>
        </v-card>
      </v-col>

      <v-col cols="12" md="6">
        <v-card>
          <v-card-title>Subsystems</v-card-title>
          <v-list density="compact">
            <v-list-item>
              <template #prepend><v-icon color="primary">mdi-chip</v-icon></template>
              <v-list-item-title>OTA Updates</v-list-item-title>
              <template #append>
                <v-chip :color="status.otaEnabled ? 'success' : 'default'" size="small">
                  {{ status.otaEnabled ? 'Enabled' : 'Disabled' }}
                </v-chip>
              </template>
            </v-list-item>
            <v-list-item>
              <template #prepend><v-icon color="secondary">mdi-transit-connection-variant</v-icon></template>
              <v-list-item-title>MQTT</v-list-item-title>
              <template #append>
                <v-chip :color="status.mqttEnabled ? 'success' : 'default'" size="small">
                  {{ status.mqttEnabled ? 'Enabled' : 'Disabled' }}
                </v-chip>
              </template>
            </v-list-item>
            <v-list-item>
              <template #prepend><v-icon color="info">mdi-home-assistant</v-icon></template>
              <v-list-item-title>Home Assistant</v-list-item-title>
              <template #append>
                <v-chip :color="status.haEnabled ? 'success' : 'default'" size="small">
                  {{ status.haEnabled ? 'Enabled' : 'Disabled' }}
                </v-chip>
              </template>
            </v-list-item>
          </v-list>
        </v-card>
      </v-col>
    </v-row>

    <!-- ── Phase 8: Extended Health Row ─────────────────────────────────── -->
    <v-row class="mt-2">
      <v-col cols="12" md="6">
        <v-card>
          <v-card-title>Memory</v-card-title>
          <v-card-text>
            <v-table density="compact">
              <tbody>
                <tr>
                  <td class="text-medium-emphasis">Free Heap</td>
                  <td>{{ formatHeap(status.freeHeap) }}</td>
                </tr>
                <tr>
                  <td class="text-medium-emphasis">Min Free Heap</td>
                  <td>{{ formatHeap(status.minFreeHeap) }}</td>
                </tr>
                <tr v-if="status.psramSize > 0">
                  <td class="text-medium-emphasis">Free PSRAM</td>
                  <td>{{ formatHeap(status.freePsram) }}</td>
                </tr>
                <tr v-if="status.psramSize > 0">
                  <td class="text-medium-emphasis">Total PSRAM</td>
                  <td>{{ formatHeap(status.psramSize) }}</td>
                </tr>
              </tbody>
            </v-table>
          </v-card-text>
        </v-card>
      </v-col>

      <v-col cols="12" md="6">
        <v-card>
          <v-card-title>Diagnostics</v-card-title>
          <v-card-text>
            <v-table density="compact">
              <tbody>
                <tr>
                  <td class="text-medium-emphasis">CPU Load</td>
                  <td>
                    <v-chip
                      :color="cpuLoadColor(status.cpuLoadPercent)"
                      size="small"
                    >
                      {{ status.cpuLoadPercent ?? '—' }}%
                    </v-chip>
                  </td>
                </tr>
                <tr>
                  <td class="text-medium-emphasis">I²C Errors</td>
                  <td>
                    <v-chip :color="status.i2cErrorCount > 0 ? 'warning' : 'success'" size="small">
                      {{ status.i2cErrorCount ?? 0 }}
                    </v-chip>
                  </td>
                </tr>
                <tr>
                  <td class="text-medium-emphasis">Sensor Errors</td>
                  <td>
                    <v-chip :color="status.sensorErrorCount > 0 ? 'error' : 'success'" size="small">
                      {{ status.sensorErrorCount ?? 0 }}
                    </v-chip>
                  </td>
                </tr>
                <tr>
                  <td class="text-medium-emphasis">WiFi RSSI</td>
                  <td>{{ formatRssi(status.rssi) }}</td>
                </tr>
              </tbody>
            </v-table>

            <v-alert
              v-if="sensorFailures.length"
              type="warning"
              density="compact"
              class="mt-3"
              title="Sensor Issues"
            >
              <ul class="text-body-2 mt-1">
                <li v-for="s in sensorFailures" :key="s.id">
                  <strong>{{ s.id }}</strong>: {{ s.lastError || 'unhealthy' }}
                  ({{ s.errorCount }} errors)
                </li>
              </ul>
            </v-alert>
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

const status = computed(() => ({
  ...(deviceStore.status ?? {}),
  ...wsStore.health,
}))

const sensorFailures = computed(() =>
  Array.isArray(status.value.sensors) ? status.value.sensors : []
)

const formatHeap = (value) => (
  value === undefined || value === null
    ? '—'
    : `${Math.round(value / 1024)} KB`
)

const formatRssi = (value) => (
  value === undefined || value === null || value === 0
    ? '—'
    : `${value} dBm`
)

const cpuLoadColor = (pct) => {
  if (pct === undefined || pct === null) return 'default'
  if (pct >= 80) return 'error'
  if (pct >= 50) return 'warning'
  return 'success'
}

const statusCards = computed(() => [
  {
    label: 'Uptime',
    value: status.value.uptime || '—',
    icon: 'mdi-clock-outline',
    color: 'primary',
  },
  {
    label: 'Free Heap',
    value: formatHeap(status.value.freeHeap),
    icon: 'mdi-memory',
    color: 'secondary',
  },
  {
    label: 'CPU Load',
    value: status.value.cpuLoadPercent !== undefined ? `${status.value.cpuLoadPercent}%` : '—',
    icon: 'mdi-speedometer',
    color: cpuLoadColor(status.value.cpuLoadPercent),
  },
  {
    label: 'WiFi RSSI',
    value: formatRssi(status.value.rssi),
    icon: 'mdi-wifi',
    color: 'info',
  },
  {
    label: 'Firmware',
    value: status.value.version || '—',
    icon: 'mdi-chip',
    color: 'success',
  },
  {
    label: 'Board',
    value: status.value.board || '—',
    icon: 'mdi-developer-board',
    color: 'warning',
  },
  {
    label: 'Modules',
    value: status.value.moduleCount ?? '—',
    icon: 'mdi-expansion-card',
    color: 'purple',
  },
])
</script>

