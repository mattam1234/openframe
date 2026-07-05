import { defineStore } from 'pinia'
import { ref } from 'vue'
import api from '../api/client'

export const useDeviceStore = defineStore('device', () => {
  const status = ref(null)
  const config = ref(null)

  async function fetchStatus() {
    status.value = await api.get('/api/status')
  }

  async function fetchConfig() {
    config.value = await api.get('/api/config')
  }

  async function saveConfig(payload) {
    const res = await api.post('/api/config', payload)
    config.value = res
    return res   // includes restartRequired + message
  }

  async function scanWifi() {
    return api.get('/api/wifi/scan')
  }

  return { status, config, fetchStatus, fetchConfig, saveConfig, scanWifi }
})
