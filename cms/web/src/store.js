// App-wide fleet store: one WebSocket + device list shared across routes so the
// live connection persists as you navigate between the grid and a device. Plain
// reactive singleton — the CMS UI is small enough not to need Pinia.
import { reactive, ref } from 'vue'
import api from './api'

const devices = ref([])
const connected = ref(false)
const activeAlerts = ref([])
// Per-device freeHeap ring buffers, fed from live updates → shared Sparkline.
const heapHistory = reactive({})

let socket = null
let reconnectTimer = null
let started = false

const asNum = (v) => { const n = Number(v); return Number.isFinite(n) ? n : null }

function recordHeap(d) {
  const n = asNum(d.freeHeap)
  if (n == null) return
  const buf = heapHistory[d.deviceId] || (heapHistory[d.deviceId] = [])
  buf.push(n)
  if (buf.length > 20) buf.shift()
}

function upsert(device) {
  const i = devices.value.findIndex((d) => d.deviceId === device.deviceId)
  if (i >= 0) devices.value[i] = device
  else devices.value.push(device)
  recordHeap(device)
}

function connect() {
  const proto = location.protocol === 'https:' ? 'wss' : 'ws'
  socket = new WebSocket(`${proto}://${location.host}/ws`)
  socket.onopen = () => { connected.value = true }
  socket.onclose = () => {
    connected.value = false
    clearTimeout(reconnectTimer)
    reconnectTimer = setTimeout(connect, 2000)
  }
  socket.onmessage = (ev) => {
    let msg
    try { msg = JSON.parse(ev.data) } catch { return }
    if (msg.type === 'snapshot') {
      devices.value = msg.devices || []
      for (const d of devices.value) recordHeap(d)
    } else if (msg.type === 'device') {
      upsert(msg.device)
    } else if (msg.type === 'alerts') {
      activeAlerts.value = msg.active || []
    } else if (msg.type === 'alert') {
      const a = msg.alert
      const i = activeAlerts.value.findIndex((x) => x.id === a.id)
      if (a.resolvedAt) { if (i >= 0) activeAlerts.value.splice(i, 1) }
      else if (i >= 0) activeAlerts.value[i] = a
      else activeAlerts.value.push(a)
    }
  }
}

// Start once (idempotent): seed from REST, then keep live over WS.
async function init() {
  if (started) return
  started = true
  try { devices.value = (await api.get('/api/devices')).devices || [] } catch { /* WS fills in */ }
  for (const d of devices.value) recordHeap(d)
  connect()
}

export function useFleet() {
  return { devices, connected, activeAlerts, heapHistory, init, upsert }
}
