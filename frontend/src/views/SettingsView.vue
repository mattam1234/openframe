<template>
  <div>
    <v-row>
      <v-col>
        <h1 class="text-h5 mb-4">
          <v-icon class="mr-2">mdi-cog</v-icon>
          Settings
        </h1>
      </v-col>
      <v-col cols="auto">
        <v-btn color="primary" :loading="saving" prepend-icon="mdi-content-save" @click="saveSettings">
          Save
        </v-btn>
      </v-col>
    </v-row>

    <v-row v-if="statusMessage">
      <v-col>
        <v-alert :type="statusMessage.type" variant="tonal">
          {{ statusMessage.text }}
        </v-alert>
      </v-col>
    </v-row>

    <v-row>
      <v-col cols="12" md="6">
        <v-card title="Device">
          <v-card-text>
            <v-text-field v-model="form.device.name" label="Device Name" />
            <v-text-field v-model="form.device.board" label="Board" readonly />
            <v-text-field
              v-model="form.device.id"
              label="Device ID"
              readonly
              hint="Stable MAC-derived identifier used by the fleet/CMS"
              persistent-hint
            />
          </v-card-text>
        </v-card>
      </v-col>

      <v-col cols="12" md="6">
        <v-card title="WiFi">
          <v-card-text>
            <div class="d-flex ga-2 mb-2">
              <v-btn color="secondary" :loading="scanningWifi" prepend-icon="mdi-wifi-refresh" @click="scanNearbyNetworks">
                Scan Nearby SSIDs
              </v-btn>
              <v-btn color="primary" variant="tonal" prepend-icon="mdi-plus" @click="addManualNetwork">
                Add Network
              </v-btn>
            </div>

            <div class="d-flex ga-2 mb-4">
              <v-select
                v-model="selectedScannedSsid"
                :items="scanOptions"
                label="Nearby SSIDs"
                item-title="title"
                item-value="value"
                hide-details
              />
              <v-btn color="primary" variant="outlined" :disabled="!selectedScannedSsid" @click="addScannedNetwork">
                Add Selected
              </v-btn>
            </div>

            <v-table density="compact">
              <thead>
                <tr>
                  <th class="text-left">SSID</th>
                  <th class="text-left">Password</th>
                  <th class="text-left">Actions</th>
                </tr>
              </thead>
              <tbody>
                <tr v-for="(network, index) in form.wifi.networks" :key="`wifi-${index}`">
                  <td style="min-width:180px">
                    <v-text-field v-model="network.ssid" density="compact" hide-details variant="plain" />
                  </td>
                  <td style="min-width:180px">
                    <v-text-field v-model="network.password" type="password" density="compact" hide-details variant="plain" />
                  </td>
                  <td>
                    <v-btn icon="mdi-delete" variant="text" color="error" @click="removeNetwork(index)" />
                  </td>
                </tr>
                <tr v-if="form.wifi.networks.length === 0">
                  <td colspan="3" class="text-medium-emphasis">No WiFi networks saved.</td>
                </tr>
              </tbody>
            </v-table>

            <v-switch v-model="form.wifi.ap_mode" label="Force AP Mode" color="primary" />
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>

    <v-row>
      <v-col cols="12" md="6">
        <v-card title="MQTT">
          <v-card-text>
            <v-switch v-model="form.mqtt.enabled" label="Enabled" color="primary" />
            <v-text-field v-model="form.mqtt.host" label="Host" />
            <v-text-field v-model.number="form.mqtt.port" label="Port" type="number" />
            <v-text-field v-model="form.mqtt.user" label="Username" />
            <v-text-field v-model="form.mqtt.password" label="Password" type="password" />
            <v-text-field v-model="form.mqtt.base_topic" label="Base Topic" />
          </v-card-text>
        </v-card>
      </v-col>

      <v-col cols="12" md="6">
        <v-card title="Node Link (mesh)">
          <v-card-text>
            <v-switch v-model="form.nodelink.enabled" label="Enable ESP-NOW node link" color="primary" />
            <v-text-field
              v-model.number="form.nodelink.channel"
              label="Channel"
              type="number"
              hint="0 = follow the current WiFi channel. All linked nodes must share a channel."
              persistent-hint
            />
            <v-switch
              v-model="form.nodelink.gateway"
              label="Act as gateway (bridge leaf nodes to MQTT/CMS)"
              color="primary"
              :disabled="!form.nodelink.enabled || !form.mqtt.enabled"
            />
          </v-card-text>
        </v-card>
      </v-col>

      <v-col cols="12" md="6">
        <v-card title="Integrations">
          <v-card-text>
            <v-switch v-model="form.ha.enabled" label="Home Assistant Discovery" color="primary" />
            <v-text-field v-model="form.ha.discovery_prefix" label="Discovery Prefix" />
            <v-divider class="my-4" />
            <v-switch v-model="form.ota.enabled" label="OTA Enabled" color="primary" />
            <v-switch v-model="form.ota.auto_check" label="Auto-check Releases" color="primary" />
            <v-text-field v-model="form.ota.github_repo" label="GitHub Repository" hint="owner/repo" persistent-hint />
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>
  </div>
</template>

<script setup>
import { computed, onMounted, reactive, ref } from 'vue'
import { useDeviceStore } from '../stores/device'

const deviceStore = useDeviceStore()
const saving = ref(false)
const statusMessage = ref(null)
const scanningWifi = ref(false)
const scannedNetworks = ref([])
const selectedScannedSsid = ref('')

const form = reactive({
  device: { name: '', board: '', id: '' },
  wifi: { ssid: '', password: '', ap_mode: true, networks: [] },
  mqtt: { enabled: false, host: '', port: 1883, user: '', password: '', base_topic: 'openframe' },
  ha: { enabled: false, discovery_prefix: 'homeassistant' },
  ota: { enabled: true, github_repo: '', auto_check: false },
  nodelink: { enabled: false, channel: 0, gateway: false },
})

const scanOptions = computed(() => {
  return scannedNetworks.value.map(n => {
    const secure = n.secure ? 'secured' : 'open'
    return {
      title: `${n.ssid} (${n.rssi} dBm, ch ${n.channel}, ${secure})`,
      value: n.ssid,
    }
  })
})

function applyConfig(config) {
  form.device.name = config?.device?.name || ''
  form.device.board = config?.device?.board || ''
  form.device.id = config?.device?.id || ''
  form.wifi.ssid = config?.wifi?.ssid || ''
  form.wifi.password = config?.wifi?.password || ''
  form.wifi.ap_mode = config?.wifi?.ap_mode ?? true
  const configuredNetworks = Array.isArray(config?.wifi?.networks)
    ? config.wifi.networks
    : []
  form.wifi.networks = configuredNetworks
    .filter(n => String(n?.ssid || '').trim())
    .map(n => ({
      ssid: String(n.ssid || ''),
      password: String(n.password || ''),
    }))
  if (form.wifi.networks.length === 0 && form.wifi.ssid) {
    form.wifi.networks.push({
      ssid: form.wifi.ssid,
      password: form.wifi.password,
    })
  }
  form.mqtt.enabled = config?.mqtt?.enabled ?? false
  form.mqtt.host = config?.mqtt?.host || ''
  form.mqtt.port = config?.mqtt?.port ?? 1883
  form.mqtt.user = config?.mqtt?.user || ''
  form.mqtt.password = config?.mqtt?.password || ''
  form.mqtt.base_topic = config?.mqtt?.base_topic || 'openframe'
  form.ha.enabled = config?.ha?.enabled ?? false
  form.ha.discovery_prefix = config?.ha?.discovery_prefix || 'homeassistant'
  form.ota.enabled = config?.ota?.enabled ?? true
  form.ota.github_repo = config?.ota?.github_repo || ''
  form.ota.auto_check = config?.ota?.auto_check ?? false
  form.nodelink.enabled = config?.nodelink?.enabled ?? false
  form.nodelink.channel = config?.nodelink?.channel ?? 0
  form.nodelink.gateway = config?.nodelink?.gateway ?? false
}

function addManualNetwork() {
  form.wifi.networks.push({ ssid: '', password: '' })
}

function removeNetwork(index) {
  form.wifi.networks.splice(index, 1)
}

function addScannedNetwork() {
  const ssid = String(selectedScannedSsid.value || '').trim()
  if (!ssid) return

  const existing = form.wifi.networks.find(n => n.ssid === ssid)
  if (!existing) {
    form.wifi.networks.push({ ssid, password: '' })
  }
}

async function scanNearbyNetworks() {
  scanningWifi.value = true
  statusMessage.value = null
  try {
    const payload = await deviceStore.scanWifi()
    scannedNetworks.value = Array.isArray(payload?.networks) ? payload.networks : []
  } catch (error) {
    statusMessage.value = {
      type: 'error',
      text: error.message || 'Failed to scan nearby SSIDs',
    }
  } finally {
    scanningWifi.value = false
  }
}

async function loadSettings() {
  statusMessage.value = null

  try {
    await deviceStore.fetchConfig()
    applyConfig(deviceStore.config)
  } catch (error) {
    statusMessage.value = {
      type: 'error',
      text: error.message || 'Failed to load settings',
    }
  }
}

async function saveSettings() {
  saving.value = true
  statusMessage.value = null

  try {
    const payload = JSON.parse(JSON.stringify(form))
    payload.wifi.networks = (payload.wifi.networks || [])
      .map(n => ({
        ssid: String(n.ssid || '').trim(),
        password: String(n.password || ''),
      }))
      .filter(n => n.ssid)
    payload.wifi.ssid = payload.wifi.networks[0]?.ssid || ''
    payload.wifi.password = payload.wifi.networks[0]?.password || ''

    await deviceStore.saveConfig(payload)
    applyConfig(deviceStore.config)
    statusMessage.value = {
      type: 'success',
      text: 'Settings saved successfully.',
    }
  } catch (error) {
    statusMessage.value = {
      type: 'error',
      text: error.message || 'Failed to save settings',
    }
  } finally {
    saving.value = false
  }
}

onMounted(() => {
  loadSettings()
})
</script>
