import { defineStore } from 'pinia'
import { ref, reactive } from 'vue'

/**
 * WebSocket store — manages the live connection to the OpenFrame device.
 *
 * The device pushes JSON frames of the shape:
 *   { type: 'sensor_update' | 'variable_change' | 'log' | 'health' | 'event', payload: {...} }
 */
export const useWebSocketStore = defineStore('websocket', () => {
  const connected = ref(false)
  const sensors = reactive({})
  const variables = reactive({})
  const health = reactive({})
  const logs = ref([])
  const events = ref([])

  let socket = null
  let reconnectTimer = null

  function connect() {
    const protocol = location.protocol === 'https:' ? 'wss' : 'ws'
    const host = import.meta.env.VITE_DEVICE_IP
      ? new URL(import.meta.env.VITE_DEVICE_IP).host
      : location.host
    const url = `${protocol}://${host}/ws`

    socket = new WebSocket(url)

    socket.onopen = () => {
      connected.value = true
      if (reconnectTimer) { clearTimeout(reconnectTimer); reconnectTimer = null }
    }

    socket.onclose = () => {
      connected.value = false
      reconnectTimer = setTimeout(connect, 3000)
    }

    socket.onerror = () => {
      socket.close()
    }

    socket.onmessage = (event) => {
      try {
        const frame = JSON.parse(event.data)
        handleFrame(frame)
      } catch (_) { /* ignore malformed frames */ }
    }
  }

  function handleFrame({ type, payload }) {
    switch (type) {
      case 'sensor_update':
        Object.assign(sensors, payload)
        break
      case 'variable_snapshot':
        Object.keys(variables).forEach((key) => delete variables[key])
        Object.assign(variables, payload || {})
        break
      case 'variable_change':
        Object.assign(variables, payload)
        break
      case 'health':
        Object.assign(health, payload)
        break
      case 'log_snapshot':
        logs.value = Array.isArray(payload) ? payload : []
        break
      case 'log':
        logs.value.unshift(payload)
        if (logs.value.length > 1000) logs.value.length = 1000
        break
      case 'event':
        events.value.unshift(payload)
        if (events.value.length > 1000) events.value.length = 1000
        break
    }
  }

  function send(type, payload) {
    if (socket && socket.readyState === WebSocket.OPEN) {
      socket.send(JSON.stringify({ type, payload }))
    }
  }

  function setLogsSnapshot(entries) {
    logs.value = Array.isArray(entries) ? entries : []
  }

  return { connected, sensors, variables, health, logs, events, connect, send, setLogsSnapshot }
})
