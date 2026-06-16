<template>
  <div>
    <v-row align="center">
      <v-col>
        <h1 class="text-h5 mb-0">
          <v-icon class="mr-2">mdi-led-strip-variant</v-icon>
          Outputs
        </h1>
        <div class="text-medium-emphasis text-caption">
          Live control of LEDs, RGB, addressable strips, relays and buzzers
        </div>
      </v-col>
      <v-col cols="auto">
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

    <v-row>
      <v-col v-for="out in outputs" :key="out.id" cols="12" sm="6" md="4">
        <OutputControlCard :output="out" @changed="onChanged" />
      </v-col>

      <v-col v-if="!outputs.length && !loading" cols="12">
        <v-alert type="info" variant="tonal">
          No outputs configured. Add them in the
          <router-link to="/layout" class="text-decoration-none">Layout Designer</router-link>
          (set the type to <code>ws2812</code> for an addressable strip and give it a pin and LED count),
          then restart the device.
        </v-alert>
      </v-col>
    </v-row>
  </div>
</template>

<script setup>
import { onMounted, ref, watch } from 'vue'
import { useWebSocketStore } from '../stores/websocket'
import api from '../api/client'
import OutputControlCard from '../components/OutputControlCard.vue'

const wsStore = useWebSocketStore()
const outputs = ref([])
const loading = ref(false)
const errorMessage = ref('')

function mergeStates(list) {
  if (!Array.isArray(list)) return
  for (const incoming of list) {
    const idx = outputs.value.findIndex(o => o.id === incoming.id)
    if (idx >= 0) outputs.value[idx] = { ...outputs.value[idx], ...incoming }
    else outputs.value.push(incoming)
  }
}

async function refresh() {
  loading.value = true
  errorMessage.value = ''
  try {
    const data = await api.get('/api/outputs/state')
    outputs.value = data.outputs || []
  } catch (err) {
    errorMessage.value = err.message || 'Failed to load outputs'
  } finally {
    loading.value = false
  }
}

function onChanged(list, error) {
  if (error) { errorMessage.value = error; return }
  mergeStates(list)
}

// Live updates pushed from the device (actions, external control).
watch(() => wsStore.outputs, (map) => {
  mergeStates(Object.values(map))
}, { deep: true })

onMounted(refresh)
</script>
