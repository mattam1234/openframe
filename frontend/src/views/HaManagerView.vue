<template>
  <div>
    <v-row>
      <v-col>
        <h1 class="text-h5 mb-4">
          <v-icon class="mr-2">mdi-home-assistant</v-icon>
          Home Assistant
        </h1>
      </v-col>
      <v-col cols="auto" class="d-flex align-center ga-2">
        <v-btn :loading="loading" prepend-icon="mdi-refresh" variant="tonal" @click="refresh" />
      </v-col>
    </v-row>

    <v-row v-if="statusMessage">
      <v-col>
        <v-alert :type="statusMessage.type" variant="tonal" closable @click:close="statusMessage = null">
          {{ statusMessage.text }}
        </v-alert>
      </v-col>
    </v-row>

    <!-- Integration status card -->
    <v-row>
      <v-col cols="12" md="5">
        <v-card>
          <v-card-title>Integration Status</v-card-title>
          <v-list density="compact">
            <v-list-item>
              <template #prepend><v-icon color="primary">mdi-transit-connection-variant</v-icon></template>
              <v-list-item-title>MQTT Connection</v-list-item-title>
              <template #append>
                <v-chip :color="health.mqttConnected ? 'success' : 'error'" size="small">
                  {{ health.mqttConnected ? 'Connected' : 'Disconnected' }}
                </v-chip>
              </template>
            </v-list-item>
            <v-list-item>
              <template #prepend><v-icon color="info">mdi-home-assistant</v-icon></template>
              <v-list-item-title>HA Discovery</v-list-item-title>
              <template #append>
                <v-chip :color="health.haEnabled ? 'success' : 'default'" size="small">
                  {{ health.haEnabled ? 'Enabled' : 'Disabled' }}
                </v-chip>
              </template>
            </v-list-item>
            <v-list-item>
              <template #prepend><v-icon color="secondary">mdi-identifier</v-icon></template>
              <v-list-item-title>Discovery Prefix</v-list-item-title>
              <template #append>
                <span class="text-body-2">{{ config?.ha?.discovery_prefix || 'homeassistant' }}</span>
              </template>
            </v-list-item>
            <v-list-item>
              <template #prepend><v-icon color="secondary">mdi-folder-network</v-icon></template>
              <v-list-item-title>Base Topic</v-list-item-title>
              <template #append>
                <span class="text-body-2">{{ config?.mqtt?.base_topic || 'openframe' }}</span>
              </template>
            </v-list-item>
          </v-list>
          <v-card-actions>
            <v-btn variant="tonal" color="primary" prepend-icon="mdi-broadcast" @click="republishDiscovery">
              Re-publish Discovery
            </v-btn>
          </v-card-actions>
        </v-card>
      </v-col>

      <v-col cols="12" md="7">
        <v-card>
          <v-card-title class="d-flex align-center justify-space-between">
            Variables as HA Entities
            <v-chip size="small">{{ sensorEntities.length }}</v-chip>
          </v-card-title>
          <v-card-subtitle>Variables with a <code>sensor.</code> prefix are automatically published as HA sensors.</v-card-subtitle>
          <v-card-text class="pa-0">
            <v-table density="compact">
              <thead>
                <tr>
                  <th>Variable</th>
                  <th>Value</th>
                  <th>HA Topic</th>
                </tr>
              </thead>
              <tbody>
                <tr v-for="[id, info] in sensorEntities" :key="id">
                  <td class="font-weight-medium">{{ id }}</td>
                  <td>{{ formatValue(info) }}</td>
                  <td class="text-caption text-medium-emphasis">
                    {{ haStateTopic(id) }}
                  </td>
                </tr>
                <tr v-if="sensorEntities.length === 0">
                  <td colspan="3" class="text-medium-emphasis text-center py-4">
                    No sensor variables available.
                  </td>
                </tr>
              </tbody>
            </v-table>
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>

    <!-- Import: read & control the wider Home Assistant -->
    <v-row class="mt-2">
      <v-col cols="12" md="5">
        <v-card>
          <v-card-title>
            <v-icon class="mr-2">mdi-import</v-icon>Import from Home Assistant
          </v-card-title>
          <v-card-subtitle>
            Mirror HA entities into variables you can show on screens and use in actions.
          </v-card-subtitle>
          <v-card-text>
            <v-switch
              v-model="importForm.enabled"
              color="primary"
              label="Enable entity import"
              hide-details
              density="compact"
              class="mb-2"
            />
            <v-select
              v-model="importForm.transport"
              :items="transportItems"
              item-title="label"
              item-value="value"
              label="Transport"
              density="compact"
              :hint="transportHint"
              persistent-hint
              class="mb-2"
            />
            <v-text-field
              v-if="importForm.transport === 'mqtt' || importForm.transport === 'auto'"
              v-model="importForm.prefix"
              label="Statestream prefix"
              hint="HA mqtt_statestream base_topic (MQTT transport)"
              persistent-hint
              density="compact"
              class="mb-2"
            />
            <template v-if="importForm.transport === 'websocket' || importForm.transport === 'auto'">
              <v-text-field
                v-model="importForm.wsHost"
                label="HA host"
                hint="Home Assistant host/IP for the WebSocket API (ESP32-S3 only)"
                persistent-hint
                density="compact"
                class="mb-2"
              />
              <v-text-field
                v-model.number="importForm.wsPort"
                label="HA port"
                type="number"
                density="compact"
                class="mb-2"
              />
              <v-text-field
                v-model="importForm.wsToken"
                label="Long-lived access token"
                type="password"
                hint="HA → Profile → Long-lived access tokens. Stored on the device."
                persistent-hint
                density="compact"
                class="mb-2"
              />
              <v-switch
                v-model="importForm.wsTls"
                color="primary"
                density="compact"
                hide-details
                inset
                label="Use TLS (wss://)"
              />
              <p class="text-caption text-medium-emphasis mt-1 mb-0">
                Required for a remote Home Assistant — the token is only sent in
                plaintext to loopback/LAN hosts, otherwise the connection is refused.
              </p>
            </template>
          </v-card-text>
          <v-card-actions>
            <v-btn :loading="savingSettings" color="primary" variant="tonal" prepend-icon="mdi-content-save" @click="saveImportSettings">
              Save
            </v-btn>
          </v-card-actions>
        </v-card>
      </v-col>

      <v-col cols="12" md="7">
        <v-card>
          <v-card-title class="d-flex align-center justify-space-between">
            Imported Entities
            <v-btn size="small" variant="tonal" prepend-icon="mdi-plus" @click="addEntity">Add</v-btn>
          </v-card-title>
          <v-card-subtitle>
            Each becomes variable <code>ha.&lt;name&gt;</code>. Mark writable on/off entities to control them.
          </v-card-subtitle>
          <v-card-text class="pa-0">
            <v-table density="compact">
              <thead>
                <tr>
                  <th>Entity ID</th>
                  <th>Variable</th>
                  <th>Type</th>
                  <th class="text-center">Writable</th>
                  <th></th>
                </tr>
              </thead>
              <tbody>
                <tr v-for="(e, idx) in entities" :key="idx">
                  <td>
                    <v-text-field v-model="e.entity_id" placeholder="light.living_room" density="compact" variant="plain" hide-details />
                  </td>
                  <td>
                    <v-text-field v-model="e.variable_id" :placeholder="defaultVarId(e.entity_id)" density="compact" variant="plain" hide-details />
                  </td>
                  <td>
                    <v-select v-model="e.type" :items="typeItems" density="compact" variant="plain" hide-details style="min-width:96px" />
                  </td>
                  <td class="text-center">
                    <v-checkbox v-model="e.writable" density="compact" hide-details class="d-inline-flex" />
                  </td>
                  <td>
                    <v-btn icon="mdi-delete" size="x-small" variant="text" @click="entities.splice(idx, 1)" />
                  </td>
                </tr>
                <tr v-if="entities.length === 0">
                  <td colspan="5" class="text-medium-emphasis text-center py-4">
                    No imported entities. Click “Add” to map an HA entity to a variable.
                  </td>
                </tr>
              </tbody>
            </v-table>
          </v-card-text>
          <v-card-actions>
            <v-btn :loading="savingEntities" color="primary" variant="tonal" prepend-icon="mdi-content-save" @click="saveEntities">
              Save &amp; restart
            </v-btn>
            <span class="text-caption text-medium-emphasis ml-2">Saving restarts the device to re-bind the bridge.</span>
          </v-card-actions>
        </v-card>
      </v-col>
    </v-row>

    <!-- Entity types overview -->
    <v-row class="mt-2">
      <v-col>
        <v-card>
          <v-card-title>Supported Entity Types</v-card-title>
          <v-card-text>
            <v-row>
              <v-col
                v-for="et in entityTypes"
                :key="et.type"
                cols="6" sm="4" md="2"
              >
                <div class="d-flex flex-column align-center pa-2">
                  <v-icon :color="et.color" size="32" class="mb-1">{{ et.icon }}</v-icon>
                  <span class="text-caption">{{ et.label }}</span>
                </div>
              </v-col>
            </v-row>
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>

    <!-- Live event feed -->
    <v-row class="mt-2">
      <v-col>
        <v-card>
          <v-card-title class="d-flex align-center justify-space-between">
            Live HA Events
            <v-btn icon="mdi-delete-sweep" size="small" variant="text" @click="clearEvents" />
          </v-card-title>
          <v-card-text class="pa-0">
            <v-table density="compact">
              <thead>
                <tr>
                  <th>Type</th>
                  <th>Source</th>
                  <th>Payload</th>
                </tr>
              </thead>
              <tbody>
                <tr v-for="(evt, idx) in haEvents.slice(0, 30)" :key="idx">
                  <td class="text-caption">{{ evt.event }}</td>
                  <td class="text-caption">{{ evt.sourceId }}</td>
                  <td class="text-caption text-medium-emphasis" style="max-width:250px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;">
                    {{ evt.payload }}
                  </td>
                </tr>
                <tr v-if="haEvents.length === 0">
                  <td colspan="3" class="text-medium-emphasis text-center py-4">
                    Waiting for HA events…
                  </td>
                </tr>
              </tbody>
            </v-table>
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>
  </div>
</template>

<script setup>
import { computed, onMounted, reactive, ref, watch } from 'vue'
import { useWebSocketStore } from '../stores/websocket'
import { useDeviceStore } from '../stores/device'
import api from '../api/client'

const wsStore = useWebSocketStore()
const deviceStore = useDeviceStore()
const loading = ref(false)
const statusMessage = ref(null)
const haEvents = ref([])

// ── Import (HA → variables) ──────────────────────────────────────────────────
const importForm = reactive({ enabled: false, transport: 'mqtt', prefix: 'homeassistant', wsHost: '', wsPort: 8123, wsToken: '', wsTls: false })
const entities = ref([])           // [{ entity_id, variable_id, type, writable }]
const savingSettings = ref(false)
const savingEntities = ref(false)

const transportItems = [
  { value: 'mqtt', label: 'MQTT (Statestream)' },
  { value: 'websocket', label: 'WebSocket (HA API)' },
  { value: 'auto', label: 'Auto' },
]
const typeItems = ['string', 'float', 'integer', 'boolean']
const transportHint = computed(() => importForm.transport === 'mqtt'
  ? 'Reads HA Statestream over MQTT; control needs an HA automation. Works on all boards.'
  : 'WebSocket needs a long-lived token (Settings) — ESP32 family only; falls back to MQTT.')

function defaultVarId(entityId) {
  const dot = (entityId || '').indexOf('.')
  return dot > 0 ? `ha.${entityId.slice(dot + 1)}` : 'ha.entity'
}

function addEntity() {
  entities.value.push({ entity_id: '', variable_id: '', type: 'string', writable: false })
}

function applyImportConfig(config) {
  importForm.enabled = config?.ha?.import_enabled ?? false
  importForm.transport = config?.ha?.transport || 'mqtt'
  importForm.prefix = config?.ha?.import_prefix || 'homeassistant'
  importForm.wsHost = config?.ha?.ws_host || ''
  importForm.wsPort = config?.ha?.ws_port ?? 8123
  importForm.wsToken = config?.ha?.ws_token || ''
  importForm.wsTls = config?.ha?.ws_tls ?? false
}

async function loadEntities() {
  try {
    const res = await api.get('/api/ha/import')
    entities.value = (res.entities || []).map(e => ({
      entity_id: e.entity_id || '',
      variable_id: e.variable_id || '',
      type: e.type || 'string',
      writable: !!e.writable,
    }))
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'Failed to load imported entities' }
  }
}

async function saveImportSettings() {
  savingSettings.value = true
  statusMessage.value = null
  try {
    const cfg = JSON.parse(JSON.stringify(deviceStore.config || {}))
    cfg.ha = cfg.ha || {}
    cfg.ha.import_enabled = importForm.enabled
    cfg.ha.transport = importForm.transport
    cfg.ha.import_prefix = importForm.prefix
    cfg.ha.ws_host = importForm.wsHost
    cfg.ha.ws_port = Number(importForm.wsPort) || 8123
    cfg.ha.ws_token = importForm.wsToken
    cfg.ha.ws_tls = importForm.wsTls
    await deviceStore.saveConfig(cfg)
    applyImportConfig(deviceStore.config)
    statusMessage.value = { type: 'success', text: 'Import settings saved.' }
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'Failed to save settings' }
  } finally {
    savingSettings.value = false
  }
}

async function saveEntities() {
  savingEntities.value = true
  statusMessage.value = null
  try {
    const clean = entities.value
      .map(e => ({
        entity_id: String(e.entity_id || '').trim(),
        variable_id: String(e.variable_id || '').trim() || defaultVarId(e.entity_id),
        type: e.type || 'string',
        writable: !!e.writable,
      }))
      .filter(e => e.entity_id.includes('.'))
    const res = await api.post('/api/ha/import', { entities: clean })
    statusMessage.value = { type: 'success', text: res.message || 'Saved. Restarting…' }
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'Failed to save entities' }
  } finally {
    savingEntities.value = false
  }
}

const health = computed(() => wsStore.health)
const config = computed(() => deviceStore.config)

const entityTypes = [
  { type: 'sensor', label: 'Sensor', icon: 'mdi-eye', color: 'info' },
  { type: 'binary_sensor', label: 'Binary Sensor', icon: 'mdi-toggle-switch', color: 'success' },
  { type: 'button', label: 'Button', icon: 'mdi-gesture-tap-button', color: 'primary' },
  { type: 'switch', label: 'Switch', icon: 'mdi-light-switch', color: 'warning' },
  { type: 'select', label: 'Select', icon: 'mdi-form-select', color: 'secondary' },
  { type: 'number', label: 'Number', icon: 'mdi-numeric', color: 'purple' },
  { type: 'text', label: 'Text', icon: 'mdi-form-textbox', color: 'orange' },
]

const sensorEntities = computed(() =>
  Object.entries(wsStore.variables)
    .filter(([id]) => id.startsWith('sensor.'))
    .sort(([a], [b]) => a.localeCompare(b))
)

function formatValue(info) {
  if (!info) return '—'
  return info.value !== undefined ? String(info.value) : '—'
}

function haStateTopic(variableId) {
  const baseTopic = config.value?.mqtt?.base_topic || 'openframe'
  const deviceName = config.value?.device?.name || 'openframe'
  return `${baseTopic}/${deviceName}/${variableId}/state`
}

function republishDiscovery() {
  wsStore.send('action_trigger', { id: 'ha_republish_discovery' })
  statusMessage.value = { type: 'info', text: 'Discovery republish triggered via WebSocket.' }
}

function clearEvents() {
  haEvents.value = []
}

// Track HA-related events from the WebSocket
watch(() => wsStore.events, (events) => {
  const haRelated = events.filter(e =>
    e.event && (
      e.event.includes('ha_') ||
      e.event.includes('mqtt_message') ||
      e.event === 'action_triggered'
    )
  )
  haEvents.value = haRelated
}, { deep: true })

async function refresh() {
  loading.value = true
  statusMessage.value = null
  try {
    await deviceStore.fetchConfig()
    applyImportConfig(deviceStore.config)
    await loadEntities()
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'Failed to load config' }
  } finally {
    loading.value = false
  }
}

onMounted(() => {
  refresh()
})
</script>
