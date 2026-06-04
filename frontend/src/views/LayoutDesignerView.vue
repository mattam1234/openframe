<template>
  <div>
    <v-row>
      <v-col>
        <h1 class="text-h5 mb-4">
          <v-icon class="mr-2">mdi-developer-board</v-icon>
          Layout Designer
        </h1>
      </v-col>
      <v-col cols="auto" class="d-flex align-center ga-2">
        <v-btn color="primary" prepend-icon="mdi-plus" @click="addInput">Input</v-btn>
        <v-btn color="secondary" prepend-icon="mdi-plus" @click="addOutput">Output</v-btn>
        <v-btn :loading="loading" prepend-icon="mdi-refresh" variant="text" @click="refresh" />
      </v-col>
    </v-row>

    <v-row v-if="statusMessage">
      <v-col>
        <v-alert :type="statusMessage.type" variant="tonal" closable @click:close="statusMessage = null">
          {{ statusMessage.text }}
        </v-alert>
      </v-col>
    </v-row>

    <v-row>
      <!-- Inputs Panel -->
      <v-col cols="12" md="6">
        <v-card>
          <v-card-title class="d-flex align-center justify-space-between">
            <span>
              <v-icon class="mr-1" color="primary">mdi-gesture-tap</v-icon>
              Inputs
            </span>
            <v-chip size="small">{{ inputs.length }}</v-chip>
          </v-card-title>
          <v-card-text class="pa-0">
            <v-table density="compact">
              <thead>
                <tr>
                  <th>ID</th>
                  <th>Type</th>
                  <th>Pin</th>
                  <th></th>
                </tr>
              </thead>
              <tbody>
                <tr v-for="(inp, idx) in inputs" :key="idx">
                  <td>
                    <v-text-field
                      v-model="inp.id"
                      density="compact"
                      hide-details
                      variant="plain"
                    />
                  </td>
                  <td>
                    <v-select
                      v-model="inp.subtype"
                      :items="['digital', 'analog']"
                      density="compact"
                      hide-details
                      variant="plain"
                      style="min-width:90px"
                    />
                  </td>
                  <td>
                    <v-text-field
                      v-model.number="inp.pin"
                      type="number"
                      density="compact"
                      hide-details
                      variant="plain"
                      style="max-width:70px"
                    />
                  </td>
                  <td>
                    <v-btn icon="mdi-delete" size="small" variant="text" color="error" @click="removeInput(idx)" />
                  </td>
                </tr>
                <tr v-if="inputs.length === 0">
                  <td colspan="4" class="text-medium-emphasis text-center py-4">
                    No inputs configured.
                  </td>
                </tr>
              </tbody>
            </v-table>
          </v-card-text>
          <v-card-actions>
            <v-btn color="primary" :loading="savingInputs" @click="saveInputs">Save Inputs</v-btn>
          </v-card-actions>
        </v-card>
      </v-col>

      <!-- Outputs Panel -->
      <v-col cols="12" md="6">
        <v-card>
          <v-card-title class="d-flex align-center justify-space-between">
            <span>
              <v-icon class="mr-1" color="secondary">mdi-led-on</v-icon>
              Outputs
            </span>
            <v-chip size="small">{{ outputs.length }}</v-chip>
          </v-card-title>
          <v-card-text class="pa-0">
            <v-table density="compact">
              <thead>
                <tr>
                  <th>ID</th>
                  <th>Type</th>
                  <th>Pin</th>
                  <th></th>
                </tr>
              </thead>
              <tbody>
                <tr v-for="(out, idx) in outputs" :key="idx">
                  <td>
                    <v-text-field
                      v-model="out.id"
                      density="compact"
                      hide-details
                      variant="plain"
                    />
                  </td>
                  <td>
                    <v-select
                      v-model="out.type"
                      :items="['led', 'rgb', 'ws2812', 'relay', 'buzzer']"
                      density="compact"
                      hide-details
                      variant="plain"
                      style="min-width:90px"
                    />
                  </td>
                  <td>
                    <v-text-field
                      v-model.number="out.pin"
                      type="number"
                      density="compact"
                      hide-details
                      variant="plain"
                      style="max-width:70px"
                    />
                  </td>
                  <td>
                    <v-btn icon="mdi-delete" size="small" variant="text" color="error" @click="removeOutput(idx)" />
                  </td>
                </tr>
                <tr v-if="outputs.length === 0">
                  <td colspan="4" class="text-medium-emphasis text-center py-4">
                    No outputs configured.
                  </td>
                </tr>
              </tbody>
            </v-table>
          </v-card-text>
          <v-card-actions>
            <v-btn color="secondary" :loading="savingOutputs" @click="saveOutputs">Save Outputs</v-btn>
          </v-card-actions>
        </v-card>
      </v-col>
    </v-row>

    <!-- Sensors Panel -->
    <v-row class="mt-2">
      <v-col>
        <v-card>
          <v-card-title class="d-flex align-center justify-space-between">
            <span>
              <v-icon class="mr-1" color="warning">mdi-chip</v-icon>
              Sensors
            </span>
            <div class="d-flex ga-2">
              <v-chip size="small">{{ sensors.length }}</v-chip>
              <v-btn prepend-icon="mdi-plus" size="small" variant="tonal" @click="addSensor">Add</v-btn>
            </div>
          </v-card-title>
          <v-card-text class="pa-0">
            <v-table density="compact">
              <thead>
                <tr>
                  <th>ID</th>
                  <th>Type</th>
                  <th>Address / Pin</th>
                  <th>Poll (ms)</th>
                  <th>Enabled</th>
                  <th></th>
                </tr>
              </thead>
              <tbody>
                <tr v-for="(sensor, idx) in sensors" :key="idx">
                  <td>
                    <v-text-field v-model="sensor.id" density="compact" hide-details variant="plain" />
                  </td>
                  <td>
                    <v-select
                      v-model="sensor.type"
                      :items="sensorTypes"
                      density="compact"
                      hide-details
                      variant="plain"
                      style="min-width:110px"
                    />
                  </td>
                  <td>
                    <v-text-field v-model="sensor.address" density="compact" hide-details variant="plain" style="max-width:80px" />
                  </td>
                  <td>
                    <v-text-field v-model.number="sensor.poll_interval_ms" type="number" density="compact" hide-details variant="plain" style="max-width:80px" />
                  </td>
                  <td>
                    <v-checkbox v-model="sensor.enabled" density="compact" hide-details />
                  </td>
                  <td>
                    <v-btn icon="mdi-delete" size="small" variant="text" color="error" @click="removeSensor(idx)" />
                  </td>
                </tr>
                <tr v-if="sensors.length === 0">
                  <td colspan="6" class="text-medium-emphasis text-center py-4">
                    No sensors configured.
                  </td>
                </tr>
              </tbody>
            </v-table>
          </v-card-text>
          <v-card-actions>
            <v-btn color="warning" :loading="savingSensors" @click="saveSensors">Save Sensors</v-btn>
          </v-card-actions>
        </v-card>
      </v-col>
    </v-row>

    <!-- Displays Panel -->
    <v-row class="mt-2">
      <v-col>
        <v-card>
          <v-card-title class="d-flex align-center justify-space-between">
            <span>
              <v-icon class="mr-1" color="info">mdi-monitor</v-icon>
              Displays
            </span>
            <div class="d-flex ga-2">
              <v-chip size="small">{{ displays.length }}</v-chip>
              <v-btn prepend-icon="mdi-plus" size="small" variant="tonal" @click="addDisplay">Add</v-btn>
            </div>
          </v-card-title>
          <v-card-text class="pa-0">
            <v-table density="compact">
              <thead>
                <tr>
                  <th>ID</th>
                  <th>Type</th>
                  <th>Address</th>
                  <th>Size</th>
                  <th>Enabled</th>
                  <th></th>
                </tr>
              </thead>
              <tbody>
                <tr v-for="(disp, idx) in displays" :key="idx">
                  <td><v-text-field v-model="disp.id" density="compact" hide-details variant="plain" /></td>
                  <td>
                    <v-select
                      v-model="disp.type"
                      :items="displayTypes"
                      density="compact"
                      hide-details
                      variant="plain"
                      style="min-width:100px"
                    />
                  </td>
                  <td><v-text-field v-model="disp.address" density="compact" hide-details variant="plain" style="max-width:80px" /></td>
                  <td>
                    <span class="text-body-2">{{ disp.width }}×{{ disp.height }}</span>
                  </td>
                  <td><v-checkbox v-model="disp.enabled" density="compact" hide-details /></td>
                  <td>
                    <v-btn icon="mdi-delete" size="small" variant="text" color="error" @click="removeDisplay(idx)" />
                  </td>
                </tr>
                <tr v-if="displays.length === 0">
                  <td colspan="6" class="text-medium-emphasis text-center py-4">
                    No displays configured.
                  </td>
                </tr>
              </tbody>
            </v-table>
          </v-card-text>
          <v-card-actions>
            <v-btn color="info" :loading="savingDisplays" @click="saveDisplays">Save Displays</v-btn>
          </v-card-actions>
        </v-card>
      </v-col>
    </v-row>
  </div>
</template>

<script setup>
import { onMounted, ref } from 'vue'
import api from '../api/client'

const loading = ref(false)
const statusMessage = ref(null)
const savingInputs = ref(false)
const savingOutputs = ref(false)
const savingSensors = ref(false)
const savingDisplays = ref(false)

const inputs = ref([])
const outputs = ref([])
const sensors = ref([])
const displays = ref([])

const sensorTypes = ['bme280', 'bmp280', 'dht22', 'ds18b20', 'sht31', 'bh1750', 'ina219', 'mpu6050']
const displayTypes = ['ssd1306', 'sh1106']

function addInput() {
  inputs.value.push({ id: `btn${inputs.value.length + 1}`, subtype: 'digital', pin: 0, pullup: true, inverted: false })
}

function removeInput(idx) {
  inputs.value.splice(idx, 1)
}

function addOutput() {
  outputs.value.push({ id: `led${outputs.value.length + 1}`, type: 'led', pin: 0, inverted: false })
}

function removeOutput(idx) {
  outputs.value.splice(idx, 1)
}

function addSensor() {
  sensors.value.push({ id: `sensor${sensors.value.length + 1}`, type: 'bme280', address: '0x76', poll_interval_ms: 5000, enabled: true })
}

function removeSensor(idx) {
  sensors.value.splice(idx, 1)
}

function addDisplay() {
  displays.value.push({ id: `display${displays.value.length + 1}`, type: 'ssd1306', address: '0x3C', width: 128, height: 64, enabled: true })
}

function removeDisplay(idx) {
  displays.value.splice(idx, 1)
}

async function saveInputs() {
  savingInputs.value = true
  statusMessage.value = null
  try {
    const digital = inputs.value.filter(i => i.subtype !== 'analog').map(i => ({
      id: i.id, pin: i.pin, pullup: i.pullup ?? true, inverted: i.inverted ?? false
    }))
    const analog = inputs.value.filter(i => i.subtype === 'analog').map(i => ({
      id: i.id, pin: i.pin, inverted: i.inverted ?? false
    }))
    await api.post('/api/inputs', { digital, analog })
    statusMessage.value = { type: 'success', text: 'Inputs saved. Restart device to apply.' }
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'Save failed' }
  } finally {
    savingInputs.value = false
  }
}

async function saveOutputs() {
  savingOutputs.value = true
  statusMessage.value = null
  try {
    await api.post('/api/outputs', { outputs: outputs.value })
    statusMessage.value = { type: 'success', text: 'Outputs saved. Restart device to apply.' }
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'Save failed' }
  } finally {
    savingOutputs.value = false
  }
}

async function saveSensors() {
  savingSensors.value = true
  statusMessage.value = null
  try {
    await api.post('/api/sensors', { sensors: sensors.value })
    statusMessage.value = { type: 'success', text: 'Sensors saved. Restart device to apply.' }
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'Save failed' }
  } finally {
    savingSensors.value = false
  }
}

async function saveDisplays() {
  savingDisplays.value = true
  statusMessage.value = null
  try {
    await api.post('/api/displays', { displays: displays.value })
    statusMessage.value = { type: 'success', text: 'Displays saved. Restart device to apply.' }
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'Save failed' }
  } finally {
    savingDisplays.value = false
  }
}

async function refresh() {
  loading.value = true
  statusMessage.value = null
  try {
    const [inData, outData, sensData, dispData] = await Promise.all([
      api.get('/api/inputs').catch(() => ({ inputs: [], digital: [], analog: [] })),
      api.get('/api/outputs').catch(() => ({ outputs: [] })),
      api.get('/api/sensors').catch(() => ({ sensors: [] })),
      api.get('/api/displays').catch(() => ({ displays: [] })),
    ])
    // Merge digital and analog inputs into one list
    const d = (inData.digital || []).map(i => ({ ...i, subtype: 'digital' }))
    const a = (inData.analog || []).map(i => ({ ...i, subtype: 'analog' }))
    inputs.value = [...d, ...a]
    outputs.value = outData.outputs || []
    sensors.value = sensData.sensors || []
    displays.value = dispData.displays || []
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'Failed to load layout' }
  } finally {
    loading.value = false
  }
}

onMounted(() => {
  refresh()
})
</script>
