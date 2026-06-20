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
            <v-text-field
              v-model="form.device.api_token"
              label="API token (optional)"
              type="password"
              hint="Set to require this token for API writes, uploads, deletes & config read. Blank = open on the LAN. Saved to this browser too so you stay logged in."
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

            <v-divider class="my-2" />
            <v-select
              v-model="form.wifi.tx_power"
              :items="txPowerOptions"
              label="WiFi TX Power"
              density="compact"
              hide-details
              class="mb-1"
            />
            <div class="text-caption text-medium-emphasis mb-2">
              Lower power reduces heat and the current spikes that can brown out marginally-powered boards. Lower it if the device runs hot or the connection is flaky. Applied on next connect/restart.
            </div>

            <v-divider class="my-2" />
            <div class="text-subtitle-2 mb-1">Static IP (optional)</div>
            <div class="text-caption text-medium-emphasis mb-2">
              Leave the IP blank for DHCP. A blank gateway defaults to x.x.x.1; blank DNS uses the gateway. Applied on next connect/restart.
            </div>
            <v-text-field v-model="form.wifi.static_ip" label="IP Address" placeholder="192.168.1.50" density="compact" hide-details class="mb-2" />
            <v-row no-gutters class="ga-2">
              <v-col><v-text-field v-model="form.wifi.gateway" label="Gateway" placeholder="192.168.1.1" density="compact" hide-details /></v-col>
              <v-col><v-text-field v-model="form.wifi.subnet" label="Subnet" density="compact" hide-details /></v-col>
            </v-row>
            <v-row no-gutters class="ga-2 mt-2">
              <v-col><v-text-field v-model="form.wifi.dns1" label="DNS 1" density="compact" hide-details /></v-col>
              <v-col><v-text-field v-model="form.wifi.dns2" label="DNS 2 (optional)" density="compact" hide-details /></v-col>
            </v-row>
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
            <v-switch v-model="form.mqtt.tls" label="Use TLS (MQTTS)" color="primary" hide-details />
            <v-switch
              v-if="form.mqtt.tls"
              v-model="form.mqtt.tls_insecure"
              label="Skip certificate validation (insecure)"
              color="warning"
              hide-details
            />
            <div v-if="form.mqtt.tls && !form.mqtt.tls_insecure" class="text-caption text-medium-emphasis mt-1">
              Upload your broker's CA certificate (PEM) as
              <code>/mqtt_ca.pem</code> in the
              <router-link to="/files" class="text-decoration-none">file browser</router-link>.
              Remember to set the port to 8883.
            </div>
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
            <v-text-field
              v-model="form.nodelink.key"
              label="Shared key"
              type="password"
              hint="≤16 chars. Encrypts unicast mesh traffic; all nodes must match. Blank = off."
              persistent-hint
            />

            <v-divider class="my-3" />
            <div class="text-subtitle-2 mb-1">Linked nodes</div>
            <div v-if="!form.nodelink.enabled" class="text-caption text-medium-emphasis">
              Enable the node link to discover peers over ESP-NOW.
            </div>
            <template v-else>
              <div class="text-caption text-medium-emphasis mb-2">
                <v-icon size="x-small" :color="nodelinkStatus.peerCount > 0 ? 'success' : 'grey'" class="mr-1">mdi-access-point-network</v-icon>
                {{ nodelinkStatus.peerCount }} online<span v-if="nodelinkStatus.totalSeen > nodelinkStatus.peerCount"> · {{ nodelinkStatus.totalSeen }} seen total</span>
              </div>
              <v-table v-if="nodelinkStatus.peers.length" density="compact">
                <tbody>
                  <tr v-for="p in nodelinkStatus.peers" :key="p.id">
                    <td>
                      <v-icon size="x-small" :color="p.online ? 'success' : 'grey'" class="mr-1">mdi-circle</v-icon>
                      {{ p.name || p.id }}
                    </td>
                    <td class="text-caption text-medium-emphasis mono">{{ p.id }}</td>
                    <td class="text-caption text-right">{{ formatAge(p.ageMs) }}</td>
                  </tr>
                </tbody>
              </v-table>
              <div v-else class="text-caption text-medium-emphasis">
                No peers heard yet — nodes announce every ~10s. Check that peers share the same channel and key.
              </div>
            </template>
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

      <v-col cols="12" md="6">
        <v-card title="Time &amp; Schedules">
          <v-card-subtitle>NTP keeps the clock accurate; the timezone makes daily schedules fire at local wall-clock time (with automatic DST).</v-card-subtitle>
          <v-card-text>
            <v-text-field v-model="form.time.ntp_server" label="NTP Server" hint="e.g. pool.ntp.org" persistent-hint />
            <v-text-field v-model="form.time.ntp_server2" label="NTP Server (fallback)" class="mt-2" />
            <v-combobox
              v-model="form.time.tz"
              :items="tzPresets"
              item-title="label"
              item-value="value"
              :return-object="false"
              label="Timezone (POSIX TZ)"
              hint="Pick a preset or enter a POSIX TZ string. Blank = UTC."
              persistent-hint
              class="mt-2"
            />
            <v-switch v-model="form.time.rtc_enabled" label="DS3231 RTC present" color="primary" class="mt-2" hide-details />
            <div class="text-caption text-medium-emphasis">Seeds the clock before/without NTP, and is re-synced from NTP when available.</div>
          </v-card-text>
        </v-card>
      </v-col>

      <v-col cols="12" md="6">
        <v-card title="Power Management">
          <v-card-subtitle>Battery nodes: run for an awake window after boot, then sleep. Deep sleep reboots on wake; light sleep resumes (ESP32). Default off.</v-card-subtitle>
          <v-card-text>
            <v-select
              v-model="form.power.mode"
              :items="[
                { title: 'Off (always on)', value: 'off' },
                { title: 'Deep sleep (reboots on wake)', value: 'deep' },
                { title: 'Light sleep (resumes, ESP32)', value: 'light' },
              ]"
              label="Sleep mode"
            />
            <template v-if="form.power.mode !== 'off'">
              <v-row class="mt-1">
                <v-col><v-text-field v-model.number="form.power.awake_seconds" label="Awake (s)" type="number" min="1" density="compact" hide-details /></v-col>
                <v-col><v-text-field v-model.number="form.power.sleep_seconds" label="Sleep (s)" type="number" min="1" density="compact" hide-details /></v-col>
              </v-row>
              <v-row class="mt-1">
                <v-col><v-text-field v-model.number="form.power.wake_pin" label="Wake GPIO (ESP32, -1 = none)" type="number" density="compact" hide-details /></v-col>
                <v-col><v-text-field v-model.number="form.power.wake_level" label="Wake level (0/1)" type="number" min="0" max="1" density="compact" hide-details /></v-col>
              </v-row>
              <div class="text-caption text-medium-emphasis mt-2">
                ESP8266 supports deep sleep only (wire GPIO16→RST). Sleep is suppressed in safe mode.
              </div>
            </template>
          </v-card-text>
        </v-card>
      </v-col>

      <v-col cols="12" md="6">
        <v-card title="Config Backups">
          <v-card-subtitle>A snapshot is taken automatically before every settings change. Restore one to roll back instantly.</v-card-subtitle>
          <v-card-text>
            <v-btn size="small" variant="tonal" prepend-icon="mdi-content-save-all" :loading="backupsLoading" @click="createBackup">
              Back up now
            </v-btn>
            <v-table density="compact" class="mt-3">
              <thead>
                <tr><th class="text-left">Backup</th><th class="text-left">Size</th><th></th></tr>
              </thead>
              <tbody>
                <tr v-for="b in backups" :key="b.id">
                  <td>{{ b.epoch ? new Date(b.epoch * 1000).toLocaleString() : `#${b.id}` }}</td>
                  <td>{{ b.size }} B</td>
                  <td class="text-right">
                    <v-btn size="x-small" variant="text" color="primary" @click="restoreBackup(b.id)">Restore</v-btn>
                    <v-btn size="x-small" variant="text" color="error" icon="mdi-delete" @click="deleteBackup(b.id)" />
                  </td>
                </tr>
                <tr v-if="backups.length === 0"><td colspan="3" class="text-medium-emphasis">No backups yet.</td></tr>
              </tbody>
            </v-table>

            <v-divider class="my-3" />
            <div class="text-caption text-medium-emphasis mb-1">Danger zone</div>
            <v-btn
              size="small"
              :color="confirmReset ? 'error' : undefined"
              :variant="confirmReset ? 'flat' : 'tonal'"
              prepend-icon="mdi-restore-alert"
              @click="factoryReset"
            >
              {{ confirmReset ? 'Click again to confirm wipe' : 'Factory reset (keep WiFi)' }}
            </v-btn>
            <v-btn v-if="confirmReset" size="small" variant="text" class="ml-2" @click="confirmReset = false">Cancel</v-btn>
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>
  </div>
</template>

<script setup>
import { computed, onMounted, reactive, ref } from 'vue'
import { useDeviceStore } from '../stores/device'
import { useWebSocketStore } from '../stores/websocket'
import api, { setApiToken } from '../api/client'

const deviceStore = useDeviceStore()
const wsStore = useWebSocketStore()

// Live ESP-NOW peer status rides the WebSocket health frame (buildStatusJson →
// nodelink). Falls back to empty until the first health frame arrives.
const nodelinkStatus = computed(() => {
  const nl = wsStore.health?.nodelink || {}
  return {
    peerCount: nl.peerCount ?? 0,
    totalSeen: nl.totalSeen ?? 0,
    peers: Array.isArray(nl.peers) ? nl.peers : [],
  }
})

function formatAge(ms) {
  if (ms == null) return ''
  const s = Math.round(ms / 1000)
  if (s < 60) return `${s}s ago`
  return `${Math.round(s / 60)}m ago`
}
const saving = ref(false)
const statusMessage = ref(null)
const scanningWifi = ref(false)
const scannedNetworks = ref([])
const selectedScannedSsid = ref('')

const form = reactive({
  device: { name: '', board: '', id: '', api_token: '' },
  wifi: { ssid: '', password: '', ap_mode: true, networks: [], static_ip: '', gateway: '', subnet: '255.255.255.0', dns1: '', dns2: '', tx_power: -1 },
  mqtt: { enabled: false, host: '', port: 1883, user: '', password: '', base_topic: 'openframe', tls: false, tls_insecure: false },
  ha: { enabled: false, discovery_prefix: 'homeassistant' },
  ota: { enabled: true, github_repo: '', auto_check: false },
  nodelink: { enabled: false, channel: 0, gateway: false, key: '' },
  time: { ntp_server: 'pool.ntp.org', ntp_server2: 'time.nist.gov', tz: '', rtc_enabled: false, rtc_address: 104 },
  power: { mode: 'off', awake_seconds: 30, sleep_seconds: 300, wake_pin: -1, wake_level: 1 },
})

// WiFi TX-power choices (dBm). -1 = leave at the SDK default (max). Lower values
// cut the current spikes that brown out marginally-powered boards, and reduce
// heat — the device maps the value to the nearest level its radio supports.
const txPowerOptions = [
  { title: 'Auto (max)', value: -1 },
  { title: '20 dBm', value: 20 },
  { title: '17 dBm', value: 17 },
  { title: '15 dBm', value: 15 },
  { title: '13 dBm', value: 13 },
  { title: '11 dBm', value: 11 },
  { title: '8 dBm', value: 8 },
  { title: '7 dBm', value: 7 },
  { title: '5 dBm', value: 5 },
]

// Common POSIX TZ strings (the device applies any valid POSIX TZ; these are just
// shortcuts). Format: stdOFFSET[dstOFFSET][,start[/time],end[/time]].
const tzPresets = [
  { label: 'UTC', value: '' },
  { label: 'Europe/Amsterdam · Berlin · Paris (CET/CEST)', value: 'CET-1CEST,M3.5.0,M10.5.0/3' },
  { label: 'Europe/London (GMT/BST)', value: 'GMT0BST,M3.5.0/1,M10.5.0' },
  { label: 'US Eastern (EST/EDT)', value: 'EST5EDT,M3.2.0,M11.1.0' },
  { label: 'US Central (CST/CDT)', value: 'CST6CDT,M3.2.0,M11.1.0' },
  { label: 'US Mountain (MST/MDT)', value: 'MST7MDT,M3.2.0,M11.1.0' },
  { label: 'US Pacific (PST/PDT)', value: 'PST8PDT,M3.2.0,M11.1.0' },
  { label: 'Australia Eastern (AEST/AEDT)', value: 'AEST-10AEDT,M10.1.0,M4.1.0/3' },
  { label: 'Japan (JST, no DST)', value: 'JST-9' },
  { label: 'India (IST, no DST)', value: 'IST-5:30' },
]

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
  form.device.api_token = config?.device?.api_token || ''
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
  form.wifi.static_ip = config?.wifi?.static_ip || ''
  form.wifi.gateway = config?.wifi?.gateway || ''
  form.wifi.subnet = config?.wifi?.subnet || '255.255.255.0'
  form.wifi.dns1 = config?.wifi?.dns1 || ''
  form.wifi.dns2 = config?.wifi?.dns2 || ''
  form.wifi.tx_power = config?.wifi?.tx_power ?? -1
  form.mqtt.enabled = config?.mqtt?.enabled ?? false
  form.mqtt.host = config?.mqtt?.host || ''
  form.mqtt.port = config?.mqtt?.port ?? 1883
  form.mqtt.user = config?.mqtt?.user || ''
  form.mqtt.password = config?.mqtt?.password || ''
  form.mqtt.base_topic = config?.mqtt?.base_topic || 'openframe'
  form.mqtt.tls = config?.mqtt?.tls ?? false
  form.mqtt.tls_insecure = config?.mqtt?.tls_insecure ?? false
  form.ha.enabled = config?.ha?.enabled ?? false
  form.ha.discovery_prefix = config?.ha?.discovery_prefix || 'homeassistant'
  form.ota.enabled = config?.ota?.enabled ?? true
  form.ota.github_repo = config?.ota?.github_repo || ''
  form.ota.auto_check = config?.ota?.auto_check ?? false
  form.nodelink.enabled = config?.nodelink?.enabled ?? false
  form.nodelink.channel = config?.nodelink?.channel ?? 0
  form.nodelink.gateway = config?.nodelink?.gateway ?? false
  form.nodelink.key = config?.nodelink?.key || ''
  form.time.ntp_server = config?.time?.ntp_server || 'pool.ntp.org'
  form.time.ntp_server2 = config?.time?.ntp_server2 || 'time.nist.gov'
  form.time.tz = config?.time?.tz || ''
  form.time.rtc_enabled = config?.time?.rtc_enabled ?? false
  form.time.rtc_address = config?.time?.rtc_address ?? 104
  form.power.mode = config?.power?.mode || 'off'
  form.power.awake_seconds = config?.power?.awake_seconds ?? 30
  form.power.sleep_seconds = config?.power?.sleep_seconds ?? 300
  form.power.wake_pin = config?.power?.wake_pin ?? -1
  form.power.wake_level = config?.power?.wake_level ?? 1
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

    // Mirror the API token into this browser BEFORE the save reply so we keep
    // access once the device starts enforcing it.
    setApiToken(form.device.api_token)
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

// Onboarding: a provisioning QR (from the CMS) opens this page with a base64url
// `provision` param carrying a partial config. Decode it and pre-fill the form so
// the operator just reviews and saves.
function decodeProvision(param) {
  const b64 = param.replace(/-/g, '+').replace(/_/g, '/')
  // Decode as UTF-8 — the deprecated escape(atob()) trick mangles multi-byte
  // characters (accented SSIDs, non-ASCII device names/passwords).
  const bytes = Uint8Array.from(atob(b64), c => c.charCodeAt(0))
  return JSON.parse(new TextDecoder().decode(bytes))
}

function applyProvision() {
  const param = new URLSearchParams(window.location.search).get('provision')
  if (!param) return
  try {
    const cfg = decodeProvision(param)
    if (Array.isArray(cfg?.wifi?.networks) && cfg.wifi.networks.length) {
      form.wifi.networks = cfg.wifi.networks
        .filter(n => String(n?.ssid || '').trim())
        .map(n => ({ ssid: String(n.ssid || ''), password: String(n.password || '') }))
      form.wifi.ap_mode = false
    }
    if (cfg?.mqtt) {
      form.mqtt.enabled = cfg.mqtt.enabled ?? true
      form.mqtt.host = cfg.mqtt.host || form.mqtt.host
      form.mqtt.port = cfg.mqtt.port ?? form.mqtt.port
      form.mqtt.base_topic = cfg.mqtt.base_topic || form.mqtt.base_topic
    }
    statusMessage.value = { type: 'info', text: 'Pre-filled from a provisioning link — review and Save.' }
  } catch {
    statusMessage.value = { type: 'error', text: 'Invalid provisioning link.' }
  }
}

// ── Config backups ───────────────────────────────────────────────────────────
const backups = ref([])
const backupsLoading = ref(false)
const confirmReset = ref(false)

async function factoryReset() {
  if (!confirmReset.value) { confirmReset.value = true; return }
  confirmReset.value = false
  try {
    await api.post('/api/factory-reset')
    statusMessage.value = { type: 'success', text: 'Factory reset done (WiFi kept) — device is rebooting.' }
  } catch (e) {
    statusMessage.value = { type: 'error', text: e.message || 'Factory reset failed' }
  }
}

async function loadBackups() {
  try {
    const res = await api.get('/api/config/backups')
    backups.value = res.backups || []
  } catch { /* endpoint may be auth-gated before token is set */ }
}
async function createBackup() {
  backupsLoading.value = true
  try {
    const res = await api.post('/api/config/backup')
    backups.value = res.backups || []
    statusMessage.value = { type: 'success', text: 'Backup created.' }
  } catch (e) {
    statusMessage.value = { type: 'error', text: e.message || 'Backup failed' }
  } finally { backupsLoading.value = false }
}
async function restoreBackup(id) {
  try {
    await api.post('/api/config/restore', { id })
    statusMessage.value = { type: 'success', text: 'Config restored — device is rebooting.' }
  } catch (e) {
    statusMessage.value = { type: 'error', text: e.message || 'Restore failed' }
  }
}
async function deleteBackup(id) {
  try {
    await api.delete(`/api/config/backups/${encodeURIComponent(id)}`)
    await loadBackups()
  } catch (e) {
    statusMessage.value = { type: 'error', text: e.message || 'Delete failed' }
  }
}

onMounted(async () => {
  await loadSettings()
  applyProvision()
  loadBackups()
})
</script>
