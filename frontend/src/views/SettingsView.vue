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
          </v-card-text>
        </v-card>
      </v-col>

      <v-col cols="12" md="6">
        <v-card title="WiFi">
          <v-card-text>
            <v-text-field v-model="form.wifi.ssid" label="SSID" />
            <v-text-field v-model="form.wifi.password" label="Password" type="password" />
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
import { onMounted, reactive, ref } from 'vue'
import { useDeviceStore } from '../stores/device'

const deviceStore = useDeviceStore()
const saving = ref(false)
const statusMessage = ref(null)

const form = reactive({
  device: { name: '', board: '' },
  wifi: { ssid: '', password: '', ap_mode: true },
  mqtt: { enabled: false, host: '', port: 1883, user: '', password: '', base_topic: 'openframe' },
  ha: { enabled: false, discovery_prefix: 'homeassistant' },
  ota: { enabled: true, github_repo: '', auto_check: false },
})

function applyConfig(config) {
  form.device.name = config?.device?.name || ''
  form.device.board = config?.device?.board || ''
  form.wifi.ssid = config?.wifi?.ssid || ''
  form.wifi.password = config?.wifi?.password || ''
  form.wifi.ap_mode = config?.wifi?.ap_mode ?? true
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
