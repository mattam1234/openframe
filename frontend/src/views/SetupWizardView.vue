<template>
  <div>
    <h1 class="text-h5 mb-4">
      <v-icon class="mr-2">mdi-rocket-launch</v-icon>
      Setup Wizard
    </h1>

    <v-alert v-if="statusMessage" :type="statusMessage.type" variant="tonal" class="mb-4" closable
             @click:close="statusMessage = null">
      {{ statusMessage.text }}
    </v-alert>

    <v-card>
      <v-stepper v-model="step" :items="stepLabels" hide-actions flat>
        <!-- 1 · Device -->
        <template #item.1>
          <v-card-text>
            <p class="text-body-2 text-medium-emphasis mb-4">Give this device a friendly name.</p>
            <v-text-field v-model="form.deviceName" label="Device name" prepend-inner-icon="mdi-tag" />
          </v-card-text>
        </template>

        <!-- 2 · WiFi -->
        <template #item.2>
          <v-card-text>
            <p class="text-body-2 text-medium-emphasis mb-4">Connect the device to your WiFi network.</p>
            <div class="d-flex ga-2 align-center mb-2">
              <v-combobox
                v-model="form.ssid"
                :items="scanOptions"
                label="WiFi SSID"
                prepend-inner-icon="mdi-wifi"
                class="flex-grow-1"
              />
              <v-btn :loading="scanning" icon="mdi-magnify" variant="tonal" @click="scan" />
            </div>
            <v-text-field v-model="form.password" label="WiFi password" type="password" prepend-inner-icon="mdi-lock" />
          </v-card-text>
        </template>

        <!-- 3 · MQTT (optional) -->
        <template #item.3>
          <v-card-text>
            <p class="text-body-2 text-medium-emphasis mb-2">
              Optional — connect to an MQTT broker for Home Assistant / fleet management.
            </p>
            <v-switch v-model="form.mqttEnabled" label="Enable MQTT" color="primary" hide-details class="mb-2" />
            <template v-if="form.mqttEnabled">
              <v-text-field v-model="form.mqttHost" label="Broker host" prepend-inner-icon="mdi-server-network" />
              <v-text-field v-model.number="form.mqttPort" label="Port" type="number" />
              <v-text-field v-model="form.mqttUser" label="Username (optional)" />
              <v-text-field v-model="form.mqttPassword" label="Password (optional)" type="password" />
            </template>
          </v-card-text>
        </template>

        <!-- 4 · Review -->
        <template #item.4>
          <v-card-text>
            <p class="text-body-2 text-medium-emphasis mb-4">Review and apply. The device reboots to connect.</p>
            <v-list density="compact">
              <v-list-item prepend-icon="mdi-tag" title="Device name" :subtitle="form.deviceName || '—'" />
              <v-list-item prepend-icon="mdi-wifi" title="WiFi" :subtitle="form.ssid || '(not set)'" />
              <v-list-item prepend-icon="mdi-transit-connection-variant" title="MQTT"
                           :subtitle="form.mqttEnabled ? `${form.mqttHost}:${form.mqttPort}` : 'Disabled'" />
            </v-list>
          </v-card-text>
        </template>
      </v-stepper>

      <v-divider />
      <v-card-actions>
        <v-btn :disabled="step === 1" variant="text" @click="step--">Back</v-btn>
        <v-spacer />
        <v-btn v-if="step < 4" color="primary" variant="flat" @click="step++">Next</v-btn>
        <v-btn v-else color="success" variant="flat" :loading="saving" prepend-icon="mdi-check" @click="finish">
          Apply &amp; Reboot
        </v-btn>
      </v-card-actions>
    </v-card>
  </div>
</template>

<script setup>
import { computed, onMounted, reactive, ref } from 'vue'
import { useDeviceStore } from '../stores/device'

const deviceStore = useDeviceStore()

const step = ref(1)
const stepLabels = ['Device', 'WiFi', 'MQTT', 'Review']
const saving = ref(false)
const scanning = ref(false)
const scannedNetworks = ref([])
const statusMessage = ref(null)

const form = reactive({
  deviceName: 'OpenFrame',
  ssid: '',
  password: '',
  mqttEnabled: false,
  mqttHost: '',
  mqttPort: 1883,
  mqttUser: '',
  mqttPassword: '',
})

const scanOptions = computed(() =>
  scannedNetworks.value.map((n) => n.ssid).filter(Boolean)
)

async function scan() {
  scanning.value = true
  try {
    const payload = await deviceStore.scanWifi()
    scannedNetworks.value = Array.isArray(payload?.networks) ? payload.networks : []
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'WiFi scan failed' }
  } finally {
    scanning.value = false
  }
}

async function finish() {
  saving.value = true
  statusMessage.value = null
  try {
    const payload = {
      device: { name: form.deviceName },
      wifi: {
        ap_mode: false,
        networks: form.ssid ? [{ ssid: form.ssid, password: form.password }] : [],
      },
      mqtt: {
        enabled: form.mqttEnabled,
        host: form.mqttHost,
        port: Number(form.mqttPort) || 1883,
        user: form.mqttUser,
        password: form.mqttPassword,
      },
    }
    await deviceStore.saveConfig(payload)
    statusMessage.value = { type: 'success', text: 'Saved! The device is rebooting to apply your settings.' }
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'Failed to save configuration' }
  } finally {
    saving.value = false
  }
}

onMounted(async () => {
  // Pre-fill the device name from current config if present.
  try {
    await deviceStore.fetchConfig()
    if (deviceStore.config?.device?.name) form.deviceName = deviceStore.config.device.name
  } catch { /* fresh device — keep defaults */ }
  scan()
})
</script>
