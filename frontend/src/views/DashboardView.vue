<template>
  <div>
    <h1 class="text-h5 mb-6">
      <v-icon class="mr-2">mdi-view-dashboard</v-icon>
      Dashboard
    </h1>

    <v-alert
      v-if="status.apMode"
      type="info"
      variant="tonal"
      class="mb-4"
      icon="mdi-rocket-launch"
      title="Finish setup"
    >
      This device isn't connected to your network yet.
      <router-link to="/setup" class="text-decoration-none font-weight-medium">Run the setup wizard →</router-link>
    </v-alert>

    <v-alert
      v-if="status.safeMode"
      type="error"
      variant="tonal"
      class="mb-4"
      icon="mdi-shield-alert"
      title="Safe Mode"
    >
      The device rebooted repeatedly and started in <strong>safe mode</strong>: hardware and
      automation are disabled so it stays reachable. Fix the configuration (or apply an OTA update),
      then reboot to resume normal operation.
    </v-alert>

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

    <!-- Outputs (live control) -->
    <v-row v-if="outputs.length" class="mt-2">
      <v-col cols="12">
        <div class="d-flex align-center justify-space-between mb-1">
          <h2 class="text-subtitle-1">
            <v-icon class="mr-1" size="small">mdi-led-strip-variant</v-icon>
            Outputs
          </h2>
          <router-link to="/outputs" class="text-caption text-decoration-none">manage →</router-link>
        </div>
      </v-col>
      <v-col v-for="out in outputs" :key="out.id" cols="12" sm="6" md="4" lg="3">
        <OutputControlCard :output="out" @changed="onOutputChanged" />
      </v-col>
    </v-row>

    <!-- Live screen mirror -->
    <v-row v-if="!status.apMode" class="mt-2">
      <v-col cols="12">
        <v-card>
          <v-card-title class="d-flex align-center justify-space-between">
            <span><v-icon class="mr-1" size="small">mdi-monitor</v-icon>Screen</span>
            <router-link to="/screens" class="text-caption text-decoration-none">designer →</router-link>
          </v-card-title>
          <v-card-text>
            <div v-if="screens.length" class="d-flex flex-wrap ga-4">
              <ScreenPreview v-for="s in screens" :key="s.id" :screen="s" :scale="3" />
            </div>
            <div v-else-if="screensLoaded" class="text-body-2 text-medium-emphasis">
              No active displays.
              <router-link to="/layout" class="text-decoration-none">Configure a display →</router-link>
            </div>
            <v-skeleton-loader v-else type="image" />
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>

    <div class="d-flex align-center justify-space-between mt-2">
      <h2 class="text-subtitle-1"><v-icon class="mr-1" size="small">mdi-view-grid</v-icon>Panels</h2>
      <v-btn v-if="layoutCustomised" size="x-small" variant="text" prepend-icon="mdi-restore" @click="resetLayout">Reset layout</v-btn>
    </div>
    <v-row>
      <v-col cols="12" md="6" :style="{ order: orderOf('connection') }" @dragover.prevent @drop="onCardDrop('connection')">
        <v-card>
          <v-card-title class="d-flex align-center justify-space-between">
            Connection Status
            <v-icon class="drag-handle" size="small" draggable="true" title="Drag to reorder" aria-label="Drag to reorder" @dragstart="onCardDragStart('connection')">mdi-drag</v-icon>
          </v-card-title>
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
              <div v-if="status.mdnsHost && !status.apMode">
                <strong>Hostname:</strong>
                <a :href="`http://${status.mdnsHost}`" class="text-decoration-none">{{ status.mdnsHost }}</a>
              </div>
              <div><strong>Variables:</strong> {{ status.variableCount ?? '—' }}</div>
              <div><strong>Logs:</strong> {{ status.logCount ?? '—' }}</div>
              <div v-if="status.rebootReason"><strong>Last Reboot:</strong> {{ status.rebootReason }}</div>
              <div v-if="status.activeProfileId"><strong>Active Profile:</strong> {{ status.activeProfileId }}</div>
            </div>
          </v-card-text>
        </v-card>
      </v-col>

      <v-col cols="12" md="6" :style="{ order: orderOf('subsystems') }" @dragover.prevent @drop="onCardDrop('subsystems')">
        <v-card>
          <v-card-title class="d-flex align-center justify-space-between">
            Subsystems
            <v-icon class="drag-handle" size="small" draggable="true" title="Drag to reorder" aria-label="Drag to reorder" @dragstart="onCardDragStart('subsystems')">mdi-drag</v-icon>
          </v-card-title>
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
              <v-list-item-subtitle v-if="status.mqttEnabled && !status.mqttConnected && status.mqttLastError">
                {{ status.mqttLastError }}
              </v-list-item-subtitle>
              <template #append>
                <v-chip :color="mqttChip.color" size="small">{{ mqttChip.text }}</v-chip>
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

    <v-row class="mt-2">
      <v-col cols="12">
        <v-card>
          <v-card-title class="d-flex align-center justify-space-between">
            <span>Storage (LittleFS)</span>
            <span class="text-caption text-medium-emphasis">
              {{ formatBytes(storage.used) }} used / {{ formatBytes(storage.total) }}
              ({{ formatBytes(storage.free) }} free)
            </span>
          </v-card-title>
          <v-card-text>
            <v-progress-linear
              :model-value="storagePercent"
              :color="storagePercent > 90 ? 'error' : storagePercent > 75 ? 'warning' : 'primary'"
              height="10"
              rounded
            />
            <div class="text-caption text-medium-emphasis mt-1">
              {{ storagePercent }}% used —
              <router-link to="/files" class="text-decoration-none">browse files</router-link>
            </div>
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>

    <!-- ── Phase 8: Extended Health Row ─────────────────────────────────── -->
    <v-row class="mt-2">
      <v-col cols="12" md="6" :style="{ order: orderOf('memory') }" @dragover.prevent @drop="onCardDrop('memory')">
        <v-card>
          <v-card-title class="d-flex align-center justify-space-between">
            Memory
            <v-icon class="drag-handle" size="small" draggable="true" title="Drag to reorder" aria-label="Drag to reorder" @dragstart="onCardDragStart('memory')">mdi-drag</v-icon>
          </v-card-title>
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

      <v-col cols="12" md="6" :style="{ order: orderOf('diagnostics') }" @dragover.prevent @drop="onCardDrop('diagnostics')">
        <v-card>
          <v-card-title class="d-flex align-center justify-space-between">
            Diagnostics
            <v-icon class="drag-handle" size="small" draggable="true" title="Drag to reorder" aria-label="Drag to reorder" @dragstart="onCardDragStart('diagnostics')">mdi-drag</v-icon>
          </v-card-title>
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
              v-if="crashlog.length"
              type="error"
              variant="tonal"
              density="compact"
              class="mt-3"
              title="Recent crash resets"
            >
              <ul class="text-body-2 mt-1">
                <li v-for="(c, i) in crashlog.slice(-5).reverse()" :key="i">
                  <strong>{{ c.reason }}</strong> <span class="text-medium-emphasis">(boot #{{ c.boot }})</span>
                </li>
              </ul>
            </v-alert>

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

            <!-- Boot/system self-test (#9) -->
            <v-btn
              size="small"
              variant="tonal"
              prepend-icon="mdi-stethoscope"
              class="mt-3"
              :loading="selfTest.busy"
              @click="runSelfTest"
            >
              Run self-test
            </v-btn>
            <div v-if="selfTest.result" class="mt-2 text-body-2">
              <div>RAM: <v-chip size="x-small" :color="selfTest.result.ram?.ok ? 'success' : 'error'">{{ selfTest.result.ram?.ok ? 'OK' : 'LOW' }}</v-chip>
                {{ Math.round((selfTest.result.ram?.free_heap || 0) / 1024) }} KB free</div>
              <div>Flash: <v-chip size="x-small" :color="selfTest.result.flash?.ok ? 'success' : 'error'">{{ selfTest.result.flash?.ok ? 'OK' : 'FAIL' }}</v-chip></div>
              <div>Filesystem: <v-chip size="x-small" :color="selfTest.result.fs?.ok ? 'success' : 'error'">{{ selfTest.result.fs?.ok ? 'OK' : 'FAIL' }}</v-chip>
                {{ Math.round((selfTest.result.fs?.used || 0) / 1024) }}/{{ Math.round((selfTest.result.fs?.total || 0) / 1024) }} KB</div>
              <div>
                I²C devices<span v-if="selfTest.result.i2c?.sda != null"> (SDA {{ selfTest.result.i2c.sda }}/SCL {{ selfTest.result.i2c.scl }})</span>:
                {{ (selfTest.result.i2c?.devices || []).join(', ') || 'none found' }}
              </div>
            </div>
            <div v-else-if="selfTest.error" class="mt-2 text-error text-body-2">{{ selfTest.error }}</div>

            <!-- Network diagnostics (#86) -->
            <v-btn
              size="small"
              variant="tonal"
              prepend-icon="mdi-lan-connect"
              class="mt-3 ml-2"
              :loading="netDiag.busy"
              @click="runNetDiag"
            >
              Network diagnostics
            </v-btn>
            <div v-if="netDiag.result" class="mt-2 text-body-2">
              <div>WiFi: <v-chip size="x-small" :color="netDiag.result.wifi?.connected ? 'success' : 'error'">{{ netDiag.result.wifi?.connected ? 'up' : 'down' }}</v-chip>
                {{ netDiag.result.wifi?.ip || '' }}<span v-if="netDiag.result.wifi?.gateway"> · gw {{ netDiag.result.wifi.gateway }}</span></div>
              <div>DNS: <v-chip size="x-small" :color="netDiag.result.dns?.resolved ? 'success' : 'error'">{{ netDiag.result.dns?.resolved ? 'ok' : 'fail' }}</v-chip>
                {{ netDiag.result.dns?.host }} → {{ netDiag.result.dns?.ip || '—' }} ({{ netDiag.result.dns?.ms }} ms)</div>
              <div v-if="netDiag.result.mqtt?.enabled">Broker: <v-chip size="x-small" :color="netDiag.result.mqtt?.reachable ? 'success' : 'error'">{{ netDiag.result.mqtt?.reachable ? 'reachable' : 'unreachable' }}</v-chip>
                {{ netDiag.result.mqtt?.host }}:{{ netDiag.result.mqtt?.port }}<span v-if="netDiag.result.mqtt?.ms != null"> ({{ netDiag.result.mqtt.ms }} ms)</span></div>
              <div>Time: <v-chip size="x-small" :color="netDiag.result.ntp?.valid ? 'success' : 'warning'">{{ netDiag.result.ntp?.source }}</v-chip></div>
            </div>
            <div v-else-if="netDiag.error" class="mt-2 text-error text-body-2">{{ netDiag.error }}</div>
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>
  </div>
</template>

<script setup>
import { computed, onMounted, onUnmounted, ref, watch } from 'vue'
import { useWebSocketStore } from '../stores/websocket'
import { useDeviceStore } from '../stores/device'
import api from '../api/client'
import OutputControlCard from '../components/OutputControlCard.vue'
import ScreenPreview from '../components/ScreenPreview.vue'

const wsStore = useWebSocketStore()
const deviceStore = useDeviceStore()

const storage = ref({ total: 0, used: 0, free: 0 })
const outputs = ref([])
const crashlog = ref([])
const selfTest = ref({ busy: false, result: null, error: '' })

// Live screen mirror: poll the device's reported screen content and render it as
// a monochrome preview (see ScreenPreview). `screensLoaded` distinguishes "no
// displays configured" from "haven't fetched yet".
const screens = ref([])
const screensLoaded = ref(false)
let screensTimer = null

async function refreshScreens() {
  try {
    const d = await api.get('/api/screens')
    screens.value = Array.isArray(d.screens) ? d.screens : []
  } catch {
    screens.value = []
  } finally {
    screensLoaded.value = true
  }
}

async function runSelfTest() {
  selfTest.value.busy = true
  selfTest.value.error = ''
  try {
    selfTest.value.result = await api.get('/api/selftest')
  } catch (e) {
    selfTest.value.error = e.message || 'Self-test failed'
    selfTest.value.result = null
  } finally {
    selfTest.value.busy = false
  }
}

// Dashboard panel layout (#48): reorder the panels by dragging their handle; the
// order is persisted to localStorage and applied via CSS `order` (the v-row is a
// flex container). New/unknown panels fall back to their default position.
const DEFAULT_LAYOUT = ['connection', 'subsystems', 'memory', 'diagnostics']
const LAYOUT_KEY = 'of-dashboard-layout'

function loadLayout() {
  try {
    const saved = JSON.parse(localStorage.getItem(LAYOUT_KEY) || 'null')
    if (Array.isArray(saved) && saved.length) {
      // Keep only known panels, then append any defaults missing from the saved set.
      const known = saved.filter((id) => DEFAULT_LAYOUT.includes(id))
      return [...known, ...DEFAULT_LAYOUT.filter((id) => !known.includes(id))]
    }
  } catch { /* ignore */ }
  return [...DEFAULT_LAYOUT]
}

const layout = ref(loadLayout())
const draggedCard = ref(null)
const layoutCustomised = computed(() => layout.value.join() !== DEFAULT_LAYOUT.join())

function orderOf(id) {
  const i = layout.value.indexOf(id)
  return i < 0 ? DEFAULT_LAYOUT.indexOf(id) : i
}
function onCardDragStart(id) { draggedCard.value = id }
function onCardDrop(targetId) {
  const from = draggedCard.value
  draggedCard.value = null
  if (!from || from === targetId) return
  const next = layout.value.filter((id) => id !== from)
  const at = next.indexOf(targetId)
  next.splice(at < 0 ? next.length : at, 0, from)
  layout.value = next
  try { localStorage.setItem(LAYOUT_KEY, JSON.stringify(next)) } catch { /* ignore */ }
}
function resetLayout() {
  layout.value = [...DEFAULT_LAYOUT]
  try { localStorage.removeItem(LAYOUT_KEY) } catch { /* ignore */ }
}

const netDiag = ref({ busy: false, result: null, error: '' })

async function runNetDiag() {
  netDiag.value.busy = true
  netDiag.value.error = ''
  try {
    netDiag.value.result = await api.get('/api/netdiag')
  } catch (e) {
    netDiag.value.error = e.message || 'Diagnostics failed'
    netDiag.value.result = null
  } finally {
    netDiag.value.busy = false
  }
}

function mergeOutputs(list) {
  if (!Array.isArray(list)) return
  for (const incoming of list) {
    const idx = outputs.value.findIndex(o => o.id === incoming.id)
    if (idx >= 0) outputs.value[idx] = { ...outputs.value[idx], ...incoming }
    else outputs.value.push(incoming)
  }
}

function onOutputChanged(list, error) {
  if (!error) mergeOutputs(list)
}

watch(() => wsStore.outputs, (map) => mergeOutputs(Object.values(map)), { deep: true })

onMounted(() => {
  deviceStore.fetchStatus().catch(() => {})
  api.fs.stat().then((s) => { storage.value = s }).catch(() => {})
  api.get('/api/outputs/state').then((d) => { outputs.value = d.outputs || [] }).catch(() => {})
  api.get('/api/crashlog').then((d) => { crashlog.value = d.resets || [] }).catch(() => {})
  refreshScreens()
  screensTimer = setInterval(refreshScreens, 2000)
})

onUnmounted(() => {
  if (screensTimer) { clearInterval(screensTimer); screensTimer = null }
})

const storagePercent = computed(() =>
  storage.value.total > 0 ? Math.round((storage.value.used / storage.value.total) * 100) : 0
)

const formatBytes = (bytes) => {
  if (bytes === undefined || bytes === null) return '—'
  if (bytes < 1024) return `${bytes} B`
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`
  return `${(bytes / (1024 * 1024)).toFixed(2)} MB`
}

const status = computed(() => ({
  ...(deviceStore.status ?? {}),
  ...wsStore.health,
}))

const sensorFailures = computed(() =>
  Array.isArray(status.value.sensors) ? status.value.sensors : []
)

const mqttChip = computed(() => {
  if (!status.value.mqttEnabled) return { color: 'default', text: 'Disabled' }
  return status.value.mqttConnected
    ? { color: 'success', text: 'Connected' }
    : { color: 'warning', text: 'Disconnected' }
})

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
  {
    label: 'Storage Used',
    value: storage.value.total > 0 ? `${storagePercent.value}%` : '—',
    icon: 'mdi-harddisk',
    color: storagePercent.value > 90 ? 'error' : storagePercent.value > 75 ? 'warning' : 'teal',
  },
])
</script>

<style scoped>
.drag-handle { cursor: grab; opacity: 0.5; }
.drag-handle:hover { opacity: 1; }
</style>

