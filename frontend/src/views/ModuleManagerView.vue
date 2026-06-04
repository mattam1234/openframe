<template>
  <div>
    <v-row>
      <v-col>
        <h1 class="text-h5 mb-4">
          <v-icon class="mr-2">mdi-expansion-card</v-icon>
          Module Manager
        </h1>
      </v-col>
      <v-col cols="auto" class="d-flex align-center">
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
              <th>Description</th>
              <th>Status</th>
              <th>Last Seen</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="mod in modules" :key="mod.address">
              <td>
                <code>{{ formatAddress(mod.address) }}</code>
              </td>
              <td>{{ mod.type || '—' }}</td>
              <td class="text-medium-emphasis text-body-2">{{ mod.description || knownTypes[mod.type] || '' }}</td>
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
                {{ mod.lastSeen ? formatTime(mod.lastSeen) : '—' }}
              </td>
            </tr>
            <tr v-if="modules.length === 0 && !loading">
              <td colspan="5" class="text-center text-medium-emphasis py-6">
                <v-icon size="40" class="mb-2" color="grey">mdi-magnify</v-icon>
                <div>No modules discovered. Click <strong>Scan</strong> to search for I²C devices.</div>
              </td>
            </tr>
            <tr v-if="loading">
              <td colspan="5" class="text-center py-6">
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
const statusMessage = ref(null)
const modules = ref([])

const onlineCount = computed(() => modules.value.filter(m => m.online).length)
const offlineCount = computed(() => modules.value.filter(m => !m.online).length)

const knownTypes = {
  ssd1306: 'OLED Display (SSD1306)',
  sh1106: 'OLED Display (SH1106)',
  bme280: 'Temperature/Humidity/Pressure Sensor',
  bmp280: 'Temperature/Pressure Sensor',
  sht31: 'Temperature/Humidity Sensor',
  bh1750: 'Light Sensor',
  ina219: 'Current/Power Sensor',
  mpu6050: 'IMU (Accel + Gyro)',
}

function formatAddress(addr) {
  if (typeof addr === 'number') return `0x${addr.toString(16).toUpperCase().padStart(2, '0')}`
  return addr
}

function formatTime(ts) {
  if (!ts) return '—'
  return new Date(ts * 1000).toLocaleTimeString()
}

async function refresh() {
  loading.value = true
  statusMessage.value = null
  try {
    const data = await api.get('/api/modules')
    modules.value = data.modules || []
    statusMessage.value = { type: 'success', text: `Found ${modules.value.length} module(s)` }
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'Scan failed' }
  } finally {
    loading.value = false
  }
}

onMounted(() => {
  refresh()
})
</script>
