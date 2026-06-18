<template>
  <v-row>
    <v-col cols="12" md="6">
      <v-card border flat>
        <v-card-title class="text-subtitle-1">Provision a device</v-card-title>
        <v-card-text>
          <p class="text-caption text-medium-emphasis mb-3">
            Generate a QR a new device scans from its captive portal to pre-fill WiFi + MQTT.
          </p>
          <v-text-field v-model="form.ssid" label="WiFi SSID" density="compact" variant="outlined" class="mb-2" hide-details />
          <v-text-field v-model="form.password" label="WiFi password" type="password" density="compact" variant="outlined" class="mb-2" hide-details />
          <v-text-field v-model="form.mqttHost" label="MQTT host" density="compact" variant="outlined" class="mb-2" hide-details />
          <v-text-field v-model="form.mqttPort" label="MQTT port" density="compact" variant="outlined" class="mb-2" hide-details />
          <v-text-field v-model="form.baseTopic" label="Base topic" placeholder="openframe" density="compact" variant="outlined" class="mb-2" hide-details />
          <v-text-field v-model="form.apHost" label="AP host" placeholder="192.168.4.1" density="compact" variant="outlined" class="mb-3" hide-details />
          <v-btn color="primary" :loading="busy" @click="generate">Generate QR</v-btn>
          <span class="text-caption text-error ml-3">{{ error }}</span>
        </v-card-text>
      </v-card>
    </v-col>
    <v-col cols="12" md="6">
      <v-card v-if="qr" border flat>
        <v-card-title class="text-subtitle-1">Scan to provision</v-card-title>
        <v-card-text class="text-center">
          <img :src="qr" alt="provisioning QR" class="qr" >
          <p class="text-caption text-medium-emphasis mt-2 text-break">{{ url }}</p>
        </v-card-text>
      </v-card>
    </v-col>
  </v-row>
</template>

<script setup>
import { reactive, ref } from 'vue'
import api from '../api'

const form = reactive({ ssid: '', password: '', mqttHost: '', mqttPort: '', baseTopic: '', apHost: '' })
const qr = ref('')
const url = ref('')
const error = ref('')
const busy = ref(false)

async function generate() {
  busy.value = true
  error.value = ''
  qr.value = ''
  try {
    const body = await api.post('/api/provision', { ...form })
    qr.value = body.qr // trusted data: URL from our own API
    url.value = body.url
  } catch (e) { error.value = e.message }
  finally { busy.value = false }
}
</script>

<style scoped>
.qr { width: 240px; max-width: 100%; image-rendering: pixelated; background: #fff; padding: 8px; border-radius: 6px; }
</style>
