#!/usr/bin/env node
/**
 * Mock OpenFrame device (#93 follow-up) — serves the device REST API + WebSocket
 * so the Vue frontend can be developed and demoed with no hardware.
 *
 *   node tools/mock-device.mjs              # listens on :8787
 *   PORT=9000 node tools/mock-device.mjs
 *
 * Point the dev server at it (the Vite proxy forwards /api and /ws):
 *   VITE_DEVICE_IP=http://localhost:8787 npm run dev
 *
 * GET endpoints return fixture data; writes (POST/PUT/DELETE) ack with
 * { ok: true }. The WebSocket replays the device's initial-state frames on
 * connect and then pushes drifting health + sensor updates, so live views
 * (dashboard, sensor charts, event inspector) animate without a board.
 */
import http from 'node:http'
import { WebSocketServer } from 'ws'

const PORT = Number(process.env.PORT) || 8787

// Mutable bits the WS animates so the dashboard/sensor charts actually move.
let heap = 210_000
let temp = 21.4
let humidity = 47
let uptimeMs = 0

const board = {
  board_type: 'ESP32', chip_model: 'ESP32-D0WD', build_env: 'esp32dev', firmware: '0.4.0',
  chip_revision: 1, cpu_cores: 2, cpu_freq_mhz: 240, flash_size: 4 * 1024 * 1024,
  psram_present: false, psram_size: 0, heap_free: heap, mac: '24:6F:28:AB:CD:EF',
}

const status = () => ({
  deviceId: 'mockdevice01', name: 'Mock Frame', board: 'ESP32', version: '0.4.0',
  ip: '127.0.0.1', rssi: -54, freeHeap: heap, largestFreeBlock: Math.round(heap * 0.7),
  heapFragPercent: 12, uptimeMs, cpuLoadPercent: 18, safeMode: false,
  wifi: { connected: true, ssid: 'workbench', rssi: -54 }, mqtt: { connected: true },
})

const variables = [
  { id: 'temp', type: 'Float', value: temp, unit: '°C' },
  { id: 'humidity', type: 'Integer', value: humidity, unit: '%' },
  { id: 'lights', type: 'Boolean', value: true },
]

// GET fixtures, keyed by pathname.
const routes = {
  '/api/status': status,
  '/api/hardware': () => ({ board: { ...board, heap_free: heap }, i2c: { devices: [] } }),
  '/api/inputs': () => ({
    digital: [{ id: 'button', subtype: 'digital', pin: 0, pullup: true, touch: false }],
    analog: [{ id: 'knob', subtype: 'analog', pin: 34 }],
  }),
  '/api/outputs': () => ({ outputs: [
    { id: 'status', type: 'led', pin: 2, gamma: true },
    { id: 'strip', type: 'ws2812', pin: 5, led_count: 30 },
  ] }),
  '/api/outputs/state': () => ({ status: { on: true, brightness: 200 }, strip: { on: false } }),
  '/api/sensors': () => ({ sensors: [
    { id: 'climate', type: 'bme280', address: '0x76', poll_interval_ms: 5000, enabled: true },
  ] }),
  '/api/displays': () => ({ displays: [
    { id: 'oled', type: 'ssd1306', address: '0x3C', width: 128, height: 64, enabled: true },
  ] }),
  '/api/displays/pages': () => ({ pages: [
    { id: 'home', title: 'Home', displayId: 'oled', widgets: [
      { id: 'w1', type: 'text', x: 4, y: 2, textSize: 1, text: 'Mock Frame' },
      { id: 'w2', type: 'value', x: 4, y: 26, textSize: 2, variableId: 'temp', suffix: 'C', decimals: 1 },
    ] },
  ] }),
  '/api/variables': () => ({ variables }),
  '/api/actions': () => ({ actions: [] }),
  '/api/macros': () => ({ macros: [] }),
  '/api/scenes': () => ({ scenes: [] }),
  '/api/profiles': () => ({ active: 'default', profiles: [{ id: 'default', name: 'Default' }] }),
  '/api/modules': () => ({ modules: [], count: 0 }),
  '/api/logs': () => ({ entries: [] }),
  '/api/notifications': () => ({ notifications: [] }),
  '/api/config': () => ({ device: { name: 'Mock Frame' }, wifi: {}, mqtt: { enabled: true } }),
}

const server = http.createServer((req, res) => {
  const { pathname } = new URL(req.url, `http://${req.headers.host}`)
  res.setHeader('Access-Control-Allow-Origin', '*')
  res.setHeader('Access-Control-Allow-Methods', 'GET,POST,PUT,DELETE,OPTIONS')
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type, Authorization')
  if (req.method === 'OPTIONS') { res.writeHead(204).end(); return }

  const send = (obj, code = 200) => {
    res.writeHead(code, { 'Content-Type': 'application/json' })
    res.end(JSON.stringify(obj))
  }

  if (req.method === 'GET' && routes[pathname]) return send(routes[pathname]())
  // Writes just ack; config/display saves report a restart like the firmware.
  if (req.method !== 'GET') {
    const restartRequired = /\/api\/(config|displays|inputs|outputs|sensors)/.test(pathname)
    return send({ ok: true, restartRequired })
  }
  send({ error: 'not found' }, 404)
})

// WebSocket: mirror the device's initial-state frames, then push live updates.
const wss = new WebSocketServer({ server, path: '/ws' })
const frame = (type, payload) => JSON.stringify({ type, payload })

wss.on('connection', (ws) => {
  ws.send(frame('health', status()))
  ws.send(frame('variable_snapshot', Object.fromEntries(variables.map((v) => [v.id, v]))))
  ws.send(frame('log_snapshot', []))
})

setInterval(() => {
  uptimeMs += 2000
  heap = 200_000 + Math.round(Math.sin(uptimeMs / 20000) * 8000)
  temp = +(21 + Math.sin(uptimeMs / 30000) * 2).toFixed(1)
  humidity = 45 + Math.round(Math.cos(uptimeMs / 40000) * 5)
  variables[0].value = temp
  variables[1].value = humidity
  const msg = frame('health', status())
  const sensor = frame('sensor_update', { climate: { type: 'bme280', values: { temperature: temp, humidity } } })
  for (const ws of wss.clients) {
    if (ws.readyState === ws.OPEN) { ws.send(msg); ws.send(sensor) }
  }
}, 2000)

server.listen(PORT, () => {
  console.log(`[mock-device] REST + WS on http://localhost:${PORT}`)
  console.log(`[mock-device] run the UI against it:  VITE_DEVICE_IP=http://localhost:${PORT} npm run dev`)
})
