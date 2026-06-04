<template>
  <div>
    <v-row>
      <v-col>
        <h1 class="text-h5 mb-4">
          <v-icon class="mr-2">mdi-thermometer</v-icon>
          Sensor Dashboard
        </h1>
      </v-col>
      <v-col cols="auto" class="d-flex align-center ga-2">
        <v-btn :loading="loading" prepend-icon="mdi-refresh" variant="tonal" @click="refresh">
          Refresh
        </v-btn>
      </v-col>
    </v-row>

    <v-row v-if="errorMessage">
      <v-col>
        <v-alert type="error" variant="tonal" closable @click:close="errorMessage = ''">
          {{ errorMessage }}
        </v-alert>
      </v-col>
    </v-row>

    <v-row v-if="statusMessage">
      <v-col>
        <v-alert
          :type="statusMessage.type"
          variant="tonal"
          closable
          @click:close="statusMessage = null"
        >
          {{ statusMessage.text }}
        </v-alert>
      </v-col>
    </v-row>

    <!-- Sensor configuration -->
    <v-row class="mb-4">
      <v-col>
        <v-card>
          <v-card-title class="d-flex align-center justify-space-between">
            <span>Sensor Configuration</span>
            <div class="d-flex ga-2">
              <v-btn prepend-icon="mdi-plus" size="small" variant="tonal" @click="addSensorConfig">
                Add
              </v-btn>
              <v-btn
                color="warning"
                prepend-icon="mdi-content-save"
                size="small"
                :loading="savingConfig"
                @click="saveSensorConfig"
              >
                Save
              </v-btn>
            </div>
          </v-card-title>
          <v-card-subtitle>Changes are saved to device config. Restart may be required for new sensors to initialize.</v-card-subtitle>
          <v-card-text class="pa-0">
            <v-table density="compact">
              <thead>
                <tr>
                  <th class="text-left">ID</th>
                  <th class="text-left">Type</th>
                  <th class="text-left">Address</th>
                  <th class="text-left">Pin</th>
                  <th class="text-left">Poll (ms)</th>
                  <th class="text-left">Temp Offset (C)</th>
                  <th class="text-left">Sea Level (hPa)</th>
                  <th class="text-left">Prefix</th>
                  <th class="text-left">Enabled</th>
                  <th class="text-left">Actions</th>
                </tr>
              </thead>
              <tbody>
                <tr v-for="(sensor, idx) in configSensors" :key="`cfg-${idx}`">
                  <td style="min-width:150px">
                    <v-text-field v-model="sensor.id" density="compact" hide-details variant="plain" />
                  </td>
                  <td style="min-width:150px">
                    <v-select
                      v-model="sensor.type"
                      :items="sensorTypes"
                      density="compact"
                      hide-details
                      variant="plain"
                    />
                  </td>
                  <td style="min-width:120px">
                    <v-text-field
                      :model-value="formatHexAddress(sensor.address)"
                      :disabled="!usesAddress(sensor.type)"
                      placeholder="0x76"
                      density="compact"
                      hide-details
                      variant="plain"
                      @update:model-value="setSensorAddress(sensor, $event)"
                    />
                  </td>
                  <td style="min-width:90px">
                    <v-text-field
                      v-model.number="sensor.pin"
                      :disabled="!usesPin(sensor.type)"
                      type="number"
                      placeholder="4"
                      density="compact"
                      hide-details
                      variant="plain"
                    />
                  </td>
                  <td style="min-width:110px">
                    <v-text-field
                      v-model.number="sensor.poll_interval_ms"
                      type="number"
                      density="compact"
                      hide-details
                      variant="plain"
                    />
                  </td>
                  <td style="min-width:120px">
                    <v-text-field
                      v-model.number="sensor.temperature_offset_c"
                      :disabled="!supportsTemperatureOffset(sensor.type)"
                      type="number"
                      step="0.1"
                      placeholder="0.0"
                      density="compact"
                      hide-details
                      variant="plain"
                    />
                  </td>
                  <td style="min-width:130px">
                    <v-text-field
                      v-model.number="sensor.sea_level_pressure_hpa"
                      :disabled="!supportsSeaLevelPressure(sensor.type)"
                      type="number"
                      step="0.1"
                      placeholder="1013.25"
                      density="compact"
                      hide-details
                      variant="plain"
                    />
                  </td>
                  <td style="min-width:120px">
                    <v-text-field v-model="sensor.variable_prefix" density="compact" hide-details variant="plain" />
                  </td>
                  <td>
                    <v-checkbox v-model="sensor.enabled" density="compact" hide-details />
                  </td>
                  <td>
                    <v-btn icon="mdi-delete" size="small" variant="text" color="error" @click="removeSensorConfig(idx)" />
                  </td>
                </tr>
                <tr v-if="configSensors.length === 0">
                  <td colspan="10" class="text-medium-emphasis">No sensors configured.</td>
                </tr>
              </tbody>
            </v-table>
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>

    <!-- Filter / search -->
    <v-row class="mb-2">
      <v-col cols="12" sm="6" md="4">
        <v-text-field
          v-model="search"
          prepend-inner-icon="mdi-magnify"
          label="Search sensors"
          density="compact"
          hide-details
          clearable
        />
      </v-col>
    </v-row>

    <!-- No sensors -->
    <v-row v-if="filteredSensors.length === 0 && !loading">
      <v-col>
        <v-alert type="info" variant="tonal">
          No sensor data available yet. Add and save sensor configuration above, then refresh.
        </v-alert>
      </v-col>
    </v-row>

    <!-- Sensor cards -->
    <v-row>
      <v-col
        v-for="sensor in filteredSensors"
        :key="sensor.id"
        cols="12" sm="6" md="4" lg="3"
      >
        <v-card>
          <v-card-title class="d-flex align-center justify-space-between">
            <span class="text-subtitle-1 font-weight-bold">{{ sensor.id }}</span>
            <v-chip size="small" :color="sensor.online ? 'success' : 'error'">
              {{ sensor.online ? 'Online' : 'Offline' }}
            </v-chip>
          </v-card-title>
          <v-card-subtitle>{{ sensor.type }}</v-card-subtitle>
          <v-divider />
          <v-list density="compact">
            <v-list-item
              v-for="metric in sensor.metrics"
              :key="metric.key"
            >
              <template #prepend>
                <v-icon size="small" color="primary">{{ iconForMetric(metric.key) }}</v-icon>
              </template>
              <v-list-item-title>{{ labelForMetric(metric.key) }}</v-list-item-title>
              <template #append>
                <span class="text-body-2 font-weight-medium">
                  {{ formatValue(metric.value, metric.key) }}
                </span>
              </template>
            </v-list-item>
            <v-list-item v-if="sensor.metrics.length === 0">
              <v-list-item-title class="text-medium-emphasis">No readings yet</v-list-item-title>
            </v-list-item>
          </v-list>
          <v-card-text v-if="sensor.lastUpdatedMs !== null" class="text-caption text-medium-emphasis pt-0">
            Last updated {{ formatAge(sensor.lastUpdatedMs) }}
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>

    <!-- Variables from sensors (all sensor.* prefixed) -->
    <v-row class="mt-4">
      <v-col>
        <v-card>
          <v-card-title>Sensor Variables</v-card-title>
          <v-card-text class="pa-0">
            <v-table density="compact">
              <thead>
                <tr>
                  <th class="text-left">Variable</th>
                  <th class="text-left">Value</th>
                </tr>
              </thead>
              <tbody>
                <tr v-for="[id, info] in sensorVariables" :key="id">
                  <td class="font-weight-medium">{{ id }}</td>
                  <td>{{ info.value }}</td>
                </tr>
                <tr v-if="sensorVariables.length === 0">
                  <td colspan="2" class="text-medium-emphasis">No sensor variables defined.</td>
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
import { computed, onMounted, ref } from 'vue'
import api from '../api/client'
import { useWebSocketStore } from '../stores/websocket'

const wsStore = useWebSocketStore()
const loading = ref(false)
const savingConfig = ref(false)
const errorMessage = ref('')
const statusMessage = ref(null)
const search = ref('')
const sensorList = ref([])
const configSensors = ref([])

const sensorTypes = ['bme280', 'bmp280', 'dht22', 'ds18b20', 'sht31', 'bh1750', 'ina219', 'mpu6050']

const METRIC_LABELS = {
  temperature_c:    'Temperature',
  humidity_pct:     'Humidity',
  pressure_hpa:     'Pressure',
  altitude_m:       'Altitude',
  lux:              'Light',
  bus_voltage_v:    'Bus Voltage',
  shunt_voltage_mv: 'Shunt Voltage',
  current_ma:       'Current',
  power_mw:         'Power',
  accel_x:          'Accel X',
  accel_y:          'Accel Y',
  accel_z:          'Accel Z',
  gyro_x:           'Gyro X',
  gyro_y:           'Gyro Y',
  gyro_z:           'Gyro Z',
}

const METRIC_ICONS = {
  temperature_c:    'mdi-thermometer',
  humidity_pct:     'mdi-water-percent',
  pressure_hpa:     'mdi-gauge',
  altitude_m:       'mdi-image-filter-hdr',
  lux:              'mdi-brightness-5',
  bus_voltage_v:    'mdi-flash',
  shunt_voltage_mv: 'mdi-flash-outline',
  current_ma:       'mdi-current-ac',
  power_mw:         'mdi-lightning-bolt',
  accel_x:          'mdi-axis-x-arrow',
  accel_y:          'mdi-axis-y-arrow',
  accel_z:          'mdi-axis-z-arrow',
  gyro_x:           'mdi-rotate-right',
  gyro_y:           'mdi-rotate-right',
  gyro_z:           'mdi-rotate-right',
}

const METRIC_UNITS = {
  temperature_c:    '°C',
  humidity_pct:     '%',
  pressure_hpa:     'hPa',
  altitude_m:       'm',
  lux:              'lx',
  bus_voltage_v:    'V',
  shunt_voltage_mv: 'mV',
  current_ma:       'mA',
  power_mw:         'mW',
  accel_x:          'm/s²',
  accel_y:          'm/s²',
  accel_z:          'm/s²',
  gyro_x:           'rad/s',
  gyro_y:           'rad/s',
  gyro_z:           'rad/s',
}

function labelForMetric(key) {
  return METRIC_LABELS[key] || key
}

function iconForMetric(key) {
  return METRIC_ICONS[key] || 'mdi-chart-line'
}

function formatValue(value, key) {
  if (value === undefined || value === null) return '—'
  const unit = METRIC_UNITS[key] || ''
  const num = typeof value === 'number' ? value.toFixed(2) : value
  return `${num}${unit ? ' ' + unit : ''}`
}

function formatAge(ms) {
  if (ms === null) return ''
  const age = (Date.now() - ms) / 1000
  if (age < 0) return 'just now'
  if (age < 2) return 'just now'
  if (age < 60) return `${Math.round(age)}s ago`
  return `${Math.round(age / 60)}m ago`
}

// Merge WebSocket live data with REST snapshot
const sensorData = computed(() => {
  const result = {}
  // From WebSocket real-time updates
  for (const [id, data] of Object.entries(wsStore.sensors)) {
    result[id] = { ...data, online: true, lastUpdatedMs: Date.now() }
  }
  // From REST snapshot
  for (const s of sensorList.value) {
    if (!result[s.id]) {
      result[s.id] = { type: s.type, online: true, lastUpdatedMs: null, values: {} }
    }
  }
  return result
})

const sensors = computed(() => {
  return Object.entries(sensorData.value).map(([id, data]) => {
    const metrics = Object.entries(data.values || {}).map(([key, value]) => ({ key, value }))
    return {
      id,
      type: data.type || '—',
      online: data.online ?? true,
      metrics,
      lastUpdatedMs: data.lastUpdatedMs,
    }
  })
})

const filteredSensors = computed(() => {
  if (!search.value) return sensors.value
  const q = search.value.toLowerCase()
  return sensors.value.filter(s => s.id.toLowerCase().includes(q) || s.type.toLowerCase().includes(q))
})

const sensorVariables = computed(() => {
  return Object.entries(wsStore.variables)
    .filter(([id]) => id.startsWith('sensor.'))
    .sort(([a], [b]) => a.localeCompare(b))
    .map(([id, info]) => [id, info])
})

async function refresh() {
  loading.value = true
  errorMessage.value = ''
  try {
    const data = await api.get('/api/sensors')
    sensorList.value = data.sensors || []
    configSensors.value = (data.sensors || []).map(s => ({ ...s }))
  } catch (err) {
    errorMessage.value = err.message || 'Failed to load sensors'
  } finally {
    loading.value = false
  }
}

function addSensorConfig() {
  configSensors.value.push({
    id: `sensor${configSensors.value.length + 1}`,
    type: 'bme280',
    address: 0x76,
    poll_interval_ms: 5000,
    temperature_offset_c: 0,
    sea_level_pressure_hpa: 1013.25,
    variable_prefix: '',
    enabled: true,
  })
}

function removeSensorConfig(index) {
  configSensors.value.splice(index, 1)
}

function usesAddress(type) {
  return ['bme280', 'bmp280', 'sht31', 'bh1750', 'ina219', 'mpu6050'].includes(String(type || '').toLowerCase())
}

function usesPin(type) {
  return ['dht22', 'ds18b20'].includes(String(type || '').toLowerCase())
}

function supportsTemperatureOffset(type) {
  return ['bme280', 'bmp280', 'sht31'].includes(String(type || '').toLowerCase())
}

function supportsSeaLevelPressure(type) {
  return ['bme280', 'bmp280'].includes(String(type || '').toLowerCase())
}

function formatHexAddress(value) {
  if (value === undefined || value === null || Number.isNaN(Number(value))) return ''
  return `0x${Number(value).toString(16).toUpperCase()}`
}

function setSensorAddress(sensor, value) {
  const txt = String(value || '').trim()
  if (!txt) {
    delete sensor.address
    return
  }

  if (txt.startsWith('0x') || txt.startsWith('0X')) {
    const parsed = Number.parseInt(txt.substring(2), 16)
    if (Number.isFinite(parsed)) {
      sensor.address = parsed
    }
  } else {
    const parsed = Number.parseInt(txt, 10)
    if (Number.isFinite(parsed)) {
      sensor.address = parsed
    }
  }
}

async function saveSensorConfig() {
  savingConfig.value = true
  errorMessage.value = ''
  statusMessage.value = null
  try {
    const sensors = configSensors.value.map(s => ({
      ...s,
      id: String(s.id || '').trim(),
      type: String(s.type || '').trim(),
      poll_interval_ms: Number(s.poll_interval_ms || 5000),
      temperature_offset_c: Number(s.temperature_offset_c || 0),
      sea_level_pressure_hpa: Number(s.sea_level_pressure_hpa || 1013.25),
      address: Number.isFinite(Number(s.address)) ? Number(s.address) : 0x76,
      pin: Number.isFinite(Number(s.pin)) ? Number(s.pin) : 0,
      enabled: s.enabled !== false,
    })).filter(s => s.id && s.type)

    await api.post('/api/sensors', { sensors })
    statusMessage.value = {
      type: 'success',
      text: 'Sensor configuration saved. Restart device to apply new drivers if needed.',
    }
    await refresh()
  } catch (err) {
    errorMessage.value = err.message || 'Failed to save sensors'
  } finally {
    savingConfig.value = false
  }
}

onMounted(() => {
  refresh()
})
</script>
