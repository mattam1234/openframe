// App-wide fleet store: one WebSocket + device list shared across routes so the
// live connection persists as you navigate between the grid and a device. Plain
// reactive singleton — the CMS UI is small enough not to need Pinia.
import { computed, reactive, ref, watch } from 'vue'
import api from './api'
import { filterBySite, sitesOf } from './lib/fleet'
import { buildIndex, createDeviceBatcher, upsertInto } from './lib/deviceBatch'
import { pushLog, seedLog } from './lib/logWindow'

const devices = ref([])
const connected = ref(false)
const activeAlerts = ref([])

// Multi-site grouping (#66): a selected site scopes the console. Persisted so it
// survives reloads; `sites` is the distinct set across the fleet.
let savedSite = ''
try { savedSite = localStorage.getItem('of-cms-site') || '' } catch { /* private mode */ }
const selectedSite = ref(savedSite)
watch(selectedSite, (s) => { try { localStorage.setItem('of-cms-site', s || '') } catch { /* ignore */ } })
const sites = computed(() => sitesOf(devices.value))
const devicesInSite = computed(() => filterBySite(devices.value, selectedSite.value))
// Per-device freeHeap ring buffers, fed from live updates → shared Sparkline.
const heapHistory = reactive({})

// Per-device rolling window of the Warning+ log lines devices mirror over MQTT,
// fed live from `log` WS frames; DeviceView seeds it from GET /api/devices/:id/logs.
// Entries carry a client-side `_seq` (see lib/logWindow) for use as a list key.
const deviceLogs = reactive({})
const MAX_LOG_LINES = 250

function recordLog(deviceId, entry) {
  if (!deviceId || !entry) return
  const buf = deviceLogs[deviceId] || (deviceLogs[deviceId] = [])
  pushLog(buf, entry, MAX_LOG_LINES)
}

// Replace the window with the server's copy (REST seed) — it already contains
// anything that also arrived over the WS, so replacing avoids duplicates.
function seedLogs(deviceId, entries) {
  if (!deviceId) return
  deviceLogs[deviceId] = seedLog(entries, MAX_LOG_LINES)
}

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

// deviceId → index into `devices`, rebuilt whenever the list is replaced, so a
// live update doesn't scan the fleet to find its row.
let deviceIndex = new Map()

// Live `device` frames are coalesced per deviceId and applied together once per
// animation frame, so the computeds that iterate the fleet (sites, the grid's
// filter/sort, topology) re-run once per frame rather than once per WS message.
const batcher = createDeviceBatcher((batch) => {
  const list = devices.value
  for (const d of batch) upsertInto(list, deviceIndex, d)
})

// Replace the whole list (REST seed / WS snapshot). Anything still queued is
// older than the new list, so it is dropped.
function setDevices(list) {
  batcher.clear()
  devices.value = list
  deviceIndex = buildIndex(list)
  for (const d of list) recordHeap(d)
}

// Synchronous single-device update for callers that need it applied now.
function upsert(device) {
  batcher.discard(device.deviceId)
  upsertInto(devices.value, deviceIndex, device)
  recordHeap(device)
}

// A live frame: sample the heap sparkline per frame received (not per flush),
// but defer the list update to the next batch.
function onDeviceFrame(device) {
  if (!device || device.deviceId == null) return
  recordHeap(device)
  batcher.queue(device)
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
      setDevices(msg.devices || [])
    } else if (msg.type === 'device') {
      onDeviceFrame(msg.device)
    } else if (msg.type === 'alerts') {
      activeAlerts.value = msg.active || []
    } else if (msg.type === 'alert') {
      const a = msg.alert
      const i = activeAlerts.value.findIndex((x) => x.id === a.id)
      if (a.resolvedAt) { if (i >= 0) activeAlerts.value.splice(i, 1) }
      else if (i >= 0) activeAlerts.value[i] = a
      else activeAlerts.value.push(a)
    } else if (msg.type === 'log') {
      recordLog(msg.deviceId, msg.entry)
    }
  }
}

// Start once (idempotent): seed from REST, then keep live over WS.
async function init() {
  if (started) return
  started = true
  try { setDevices((await api.get('/api/devices')).devices || []) } catch { /* WS fills in */ }
  connect()
}

export function useFleet() {
  return { devices, connected, activeAlerts, heapHistory, deviceLogs, seedLogs, selectedSite, sites, devicesInSite, init, upsert }
}
