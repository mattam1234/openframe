<template>
  <div>
    <v-row>
      <v-col>
        <h1 class="text-h5 mb-4">
          <v-icon class="mr-2">mdi-expansion-card</v-icon>
          Module Manager
        </h1>
      </v-col>
      <v-col cols="auto" class="d-flex align-center ga-2">
        <v-btn
          :loading="adopting"
          :disabled="adoptableCount === 0"
          color="primary"
          prepend-icon="mdi-plus-circle"
          @click="adoptAll"
        >
          Adopt all ({{ adoptableCount }})
        </v-btn>
        <v-btn :loading="loading" prepend-icon="mdi-refresh" @click="refresh">Scan</v-btn>
      </v-col>
    </v-row>

    <v-row v-if="statusMessage">
      <v-col>
        <v-alert :type="statusMessage.type" variant="tonal" closable @click:close="statusMessage = null">
          {{ statusMessage.text }}
        </v-alert>
      </v-col>
    </v-row>

    <!-- Board / SoC info -->
    <v-card v-if="board" class="mb-4" variant="tonal" color="info">
      <v-card-text>
        <div class="d-flex align-center ga-2 mb-2">
          <v-icon>mdi-memory</v-icon>
          <span class="text-subtitle-1 font-weight-medium">
            {{ board.chip_model }}
            <span v-if="board.build_env" class="text-medium-emphasis">({{ board.build_env }})</span>
          </span>
        </div>
        <v-row dense>
          <v-col v-for="item in boardFacts" :key="item.label" cols="6" sm="4" md="3">
            <div class="text-caption text-medium-emphasis">{{ item.label }}</div>
            <div class="text-body-2">{{ item.value }}</div>
          </v-col>
        </v-row>
      </v-card-text>
    </v-card>

    <!-- Summary cards -->
    <v-row class="mb-2">
      <v-col cols="6" sm="4" md="3">
        <v-card variant="tonal" color="success">
          <v-card-text class="d-flex align-center ga-2">
            <v-icon>mdi-check-circle</v-icon>
            <div>
              <div class="text-h6">{{ onlineCount }}</div>
              <div class="text-caption">Online</div>
            </div>
          </v-card-text>
        </v-card>
      </v-col>
      <v-col cols="6" sm="4" md="3">
        <v-card variant="tonal" color="error">
          <v-card-text class="d-flex align-center ga-2">
            <v-icon>mdi-alert-circle</v-icon>
            <div>
              <div class="text-h6">{{ offlineCount }}</div>
              <div class="text-caption">Offline</div>
            </div>
          </v-card-text>
        </v-card>
      </v-col>
      <v-col cols="6" sm="4" md="3">
        <v-card variant="tonal" color="info">
          <v-card-text class="d-flex align-center ga-2">
            <v-icon>mdi-chip</v-icon>
            <div>
              <div class="text-h6">{{ modules.length }}</div>
              <div class="text-caption">Total</div>
            </div>
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>

    <!-- Modules table -->
    <v-card>
      <v-card-title>Discovered I²C Modules</v-card-title>
      <v-card-text class="pa-0">
        <v-table>
          <thead>
            <tr>
              <th>Address</th>
              <th>Type</th>
              <th>Chip</th>
              <th>Status</th>
              <th>Last Seen</th>
              <th class="text-right">Action</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="mod in modules" :key="mod.address">
              <td>
                <code>{{ formatAddress(mod.address) }}</code>
              </td>
              <td>{{ mod.type || '—' }}</td>
              <td>
                <span v-if="mod.chip_model">{{ mod.chip_model }}</span>
                <span v-else class="text-medium-emphasis">—</span>
              </td>
              <td>
                <v-chip
                  :color="mod.online ? 'success' : 'error'"
                  size="small"
                  :prepend-icon="mod.online ? 'mdi-check' : 'mdi-close'"
                >
                  {{ mod.online ? 'Online' : 'Offline' }}
                </v-chip>
              </td>
              <td class="text-body-2 text-medium-emphasis">
                {{ mod.last_seen_ms ? formatUptime(mod.last_seen_ms) : '—' }}
              </td>
              <td class="text-right">
                <v-btn
                  v-if="mod.suggested_driver && mod.online"
                  size="small"
                  variant="tonal"
                  color="primary"
                  prepend-icon="mdi-plus"
                  :loading="adoptingAddr === mod.address"
                  @click="adoptOne(mod)"
                >
                  Adopt as {{ mod.suggested_driver }}
                </v-btn>
              </td>
            </tr>
            <tr v-if="modules.length === 0 && !loading">
              <td colspan="6" class="text-center text-medium-emphasis py-6">
                <v-icon size="40" class="mb-2" color="grey">mdi-magnify</v-icon>
                <div>No modules discovered. Click <strong>Scan</strong> to search for I²C devices.</div>
              </td>
            </tr>
            <tr v-if="loading">
              <td colspan="6" class="text-center py-6">
                <v-progress-circular indeterminate size="30" />
              </td>
            </tr>
          </tbody>
        </v-table>
      </v-card-text>
    </v-card>
  </div>
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import api from '../api/client'

const loading = ref(false)
const adopting = ref(false)
const adoptingAddr = ref(null)
const statusMessage = ref(null)
const modules = ref([])
const board = ref(null)

const onlineCount = computed(() => modules.value.filter(m => m.online).length)
const offlineCount = computed(() => modules.value.filter(m => !m.online).length)
const adoptableCount = computed(
  () => modules.value.filter(m => m.online && m.suggested_driver).length,
)

const boardFacts = computed(() => {
  if (!board.value) return []
  const b = board.value
  return [
    { label: 'Firmware', value: b.firmware },
    { label: 'Revision', value: `rev ${b.chip_revision}` },
    { label: 'Cores', value: b.cpu_cores },
    { label: 'CPU', value: `${b.cpu_freq_mhz} MHz` },
    { label: 'Flash', value: formatBytes(b.flash_size) },
    { label: 'PSRAM', value: b.psram_present ? formatBytes(b.psram_size) : 'none' },
    { label: 'Free heap', value: formatBytes(b.heap_free) },
    { label: 'MAC', value: b.mac },
  ]
})

function formatAddress(addr) {
  if (typeof addr === 'number') return `0x${addr.toString(16).toUpperCase().padStart(2, '0')}`
  return addr
}

function formatBytes(n) {
  if (!n && n !== 0) return '—'
  if (n >= 1024 * 1024) return `${(n / 1024 / 1024).toFixed(1)} MB`
  if (n >= 1024) return `${(n / 1024).toFixed(0)} KB`
  return `${n} B`
}

// last_seen_ms is millis() since boot, not a wall-clock timestamp.
function formatUptime(ms) {
  const s = Math.floor(ms / 1000)
  if (s < 60) return `${s}s ago`
  if (s < 3600) return `${Math.floor(s / 60)}m ago`
  return `${Math.floor(s / 3600)}h ago`
}

async function refresh() {
  loading.value = true
  statusMessage.value = null
  try {
    const [hw, mods] = await Promise.all([
      api.get('/api/hardware').catch(() => null),
      api.get('/api/modules'),
    ])
    if (hw) board.value = hw.board || null
    modules.value = mods.modules || []
    statusMessage.value = { type: 'success', text: `Found ${modules.value.length} module(s)` }
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'Scan failed' }
  } finally {
    loading.value = false
  }
}

async function adopt(body, _addr) {
  statusMessage.value = null
  try {
    const res = await api.post('/api/hardware/adopt', body)
    statusMessage.value = {
      type: res.added && res.added.length ? 'success' : 'info',
      text: res.message || 'Adopted',
    }
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'Adopt failed' }
  } finally {
    adopting.value = false
    adoptingAddr.value = null
  }
}

function adoptOne(mod) {
  adoptingAddr.value = mod.address
  return adopt({ addresses: [mod.address] }, mod.address)
}

function adoptAll() {
  adopting.value = true
  return adopt({})
}

onMounted(() => {
  refresh()
})
</script>
