<template>
  <div>
    <v-row>
      <v-col>
        <h1 class="text-h5 mb-4">
          <v-icon class="mr-2">mdi-lightning-bolt</v-icon>
          Action Manager
        </h1>
      </v-col>
      <v-col cols="auto" class="d-flex align-center ga-2">
        <v-btn color="primary" prepend-icon="mdi-plus" @click="openNewAction">
          New Action
        </v-btn>
        <v-btn prepend-icon="mdi-playlist-play" variant="tonal" @click="openNewMacro">
          New Macro
        </v-btn>
        <v-btn :loading="loading" prepend-icon="mdi-refresh" variant="text" @click="refresh" />
      </v-col>
    </v-row>

    <v-row v-if="statusMessage">
      <v-col>
        <v-alert :type="statusMessage.type" variant="tonal" closable @click:close="statusMessage = null">
          {{ statusMessage.text }}
        </v-alert>
      </v-col>
    </v-row>

    <v-row>
      <!-- Actions -->
      <v-col cols="12" md="7">
        <v-card>
          <v-card-title class="d-flex align-center justify-space-between">
            Actions
            <v-chip size="small">{{ actions.length }}</v-chip>
          </v-card-title>
          <v-card-text class="pa-0">
            <v-table density="compact">
              <thead>
                <tr>
                  <th>Name</th>
                  <th>Steps</th>
                  <th>Conditions</th>
                  <th>Enabled</th>
                  <th></th>
                </tr>
              </thead>
              <tbody>
                <tr v-for="action in actions" :key="action.id">
                  <td>
                    <div class="font-weight-medium">{{ action.name || action.id }}</div>
                    <div class="text-caption text-medium-emphasis">{{ action.id }}</div>
                  </td>
                  <td>{{ action.step_count ?? 0 }}</td>
                  <td>{{ action.condition_count ?? 0 }}</td>
                  <td>
                    <v-icon :color="action.enabled ? 'success' : 'default'" size="small">
                      {{ action.enabled ? 'mdi-check-circle' : 'mdi-circle-outline' }}
                    </v-icon>
                  </td>
                  <td class="text-right">
                    <v-btn
                      icon="mdi-play"
                      size="small"
                      variant="text"
                      color="success"
                      :loading="triggeringId === action.id"
                      @click="triggerAction(action.id)"
                    />
                    <v-btn
                      icon="mdi-pencil"
                      size="small"
                      variant="text"
                      @click="editAction(action)"
                    />
                  </td>
                </tr>
                <tr v-if="actions.length === 0">
                  <td colspan="5" class="text-medium-emphasis text-center py-4">
                    No actions defined yet.
                  </td>
                </tr>
              </tbody>
            </v-table>
          </v-card-text>
        </v-card>
      </v-col>

      <!-- Macros -->
      <v-col cols="12" md="5">
        <v-card>
          <v-card-title class="d-flex align-center justify-space-between">
            Macros
            <v-chip size="small">{{ macros.length }}</v-chip>
          </v-card-title>
          <v-card-text class="pa-0">
            <v-table density="compact">
              <thead>
                <tr>
                  <th>Name</th>
                  <th>Actions</th>
                  <th></th>
                </tr>
              </thead>
              <tbody>
                <tr v-for="macro in macros" :key="macro.id">
                  <td>
                    <div class="font-weight-medium">{{ macro.name || macro.id }}</div>
                    <div class="text-caption text-medium-emphasis">{{ macro.id }}</div>
                  </td>
                  <td>{{ macro.action_ids?.length ?? 0 }}</td>
                  <td class="text-right">
                    <v-btn
                      icon="mdi-play"
                      size="small"
                      variant="text"
                      color="success"
                      @click="triggerMacro(macro.id)"
                    />
                    <v-btn
                      icon="mdi-pencil"
                      size="small"
                      variant="text"
                      @click="editMacro(macro)"
                    />
                  </td>
                </tr>
                <tr v-if="macros.length === 0">
                  <td colspan="3" class="text-medium-emphasis text-center py-4">
                    No macros defined yet.
                  </td>
                </tr>
              </tbody>
            </v-table>
          </v-card-text>
        </v-card>

        <!-- Action History -->
        <v-card class="mt-4">
          <v-card-title class="d-flex align-center justify-space-between">
            Recent History
            <v-chip size="small">{{ history.length }}</v-chip>
          </v-card-title>
          <v-card-text class="pa-0">
            <v-table density="compact">
              <thead>
                <tr>
                  <th>Action</th>
                  <th>Result</th>
                  <th>Time</th>
                </tr>
              </thead>
              <tbody>
                <tr v-for="(entry, idx) in history.slice(0, 20)" :key="idx">
                  <td>{{ entry.action_name || entry.action_id }}</td>
                  <td>
                    <v-icon size="small" :color="entry.success ? 'success' : 'error'">
                      {{ entry.success ? 'mdi-check' : 'mdi-close' }}
                    </v-icon>
                  </td>
                  <td class="text-caption">{{ formatTs(entry.timestamp_ms) }}</td>
                </tr>
                <tr v-if="history.length === 0">
                  <td colspan="3" class="text-medium-emphasis">No history yet.</td>
                </tr>
              </tbody>
            </v-table>
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>

    <!-- Action Editor Dialog -->
    <v-dialog v-model="actionDialog" max-width="700" scrollable>
      <v-card>
        <v-card-title>{{ editingAction.id ? 'Edit Action' : 'New Action' }}</v-card-title>
        <v-divider />
        <v-card-text>
          <v-row>
            <v-col cols="6">
              <v-text-field v-model="editingAction.id" label="ID" :disabled="!!editingAction._existing" />
            </v-col>
            <v-col cols="6">
              <v-text-field v-model="editingAction.name" label="Name" />
            </v-col>
          </v-row>
          <v-switch v-model="editingAction.enabled" label="Enabled" color="primary" />

          <v-divider class="my-3" />
          <div class="text-subtitle-2 mb-2">When (trigger)</div>
          <v-row>
            <v-col cols="12" sm="4">
              <v-select
                v-model="editingAction.trigger.source"
                :items="[{ value: 'manual', title: 'Manual / triggered by name' }, { value: 'input', title: 'When an input fires' }]"
                label="Trigger"
                density="compact"
                hide-details
              />
            </v-col>
            <template v-if="editingAction.trigger.source === 'input'">
              <v-col cols="12" sm="4">
                <v-select
                  v-model="editingAction.trigger.input_id"
                  :items="inputsList"
                  label="Input"
                  density="compact"
                  hide-details
                  :no-data-text="'No inputs configured'"
                />
              </v-col>
              <v-col cols="12" sm="4">
                <v-select
                  v-model="editingAction.trigger.event"
                  :items="inputEvents"
                  label="Event"
                  density="compact"
                  hide-details
                />
              </v-col>
            </template>
          </v-row>

          <v-divider class="my-3" />
          <div class="text-subtitle-2 mb-2">Steps</div>

          <div v-for="(step, idx) in editingAction.steps" :key="idx" class="mb-3">
            <v-card variant="outlined">
              <v-card-text>
                <v-row align="center">
                  <v-col>
                    <v-select
                      v-model="step.type"
                      :items="actionStepTypes"
                      item-title="label"
                      item-value="value"
                      label="Type"
                      density="compact"
                      hide-details
                    />
                  </v-col>
                  <v-col cols="auto">
                    <v-btn icon="mdi-close" size="small" variant="text" @click="removeStep(idx)" />
                  </v-col>
                </v-row>
                <!-- Type-specific fields -->
                <v-text-field v-if="step.type === 'delay'" v-model.number="step.delay_ms" label="Delay (ms)" type="number" density="compact" class="mt-2" />
                <template v-if="step.type === 'mqtt_publish'">
                  <v-text-field v-model="step.topic" label="Topic" density="compact" class="mt-2" />
                  <v-text-field v-model="step.body" label="Payload" density="compact" />
                </template>

                <!-- Output control -->
                <template v-if="step.type === 'output_control'">
                  <v-row class="mt-1">
                    <v-col cols="12" sm="6">
                      <v-select
                        v-model="step.output_id"
                        :items="outputsList"
                        item-title="id"
                        item-value="id"
                        label="Output"
                        density="compact"
                        hide-details
                        :no-data-text="'No outputs configured'"
                      />
                    </v-col>
                    <v-col cols="12" sm="6">
                      <v-select
                        v-model="step.command"
                        :items="outputCommands"
                        item-title="label"
                        item-value="value"
                        label="Action"
                        density="compact"
                        hide-details
                      />
                    </v-col>
                  </v-row>
                  <v-switch
                    v-if="step.command === 'digital'"
                    v-model="step.on"
                    :label="step.on ? 'Turn ON' : 'Turn OFF'"
                    color="primary"
                    density="compact"
                    hide-details
                  />
                  <div v-if="step.command === 'rgb' || step.command === 'animation'" class="d-flex align-center ga-2 mt-2">
                    <span class="text-caption text-medium-emphasis">Colour</span>
                    <input type="color" :value="stepHex(step)" @input="setStepColor(step, $event.target.value)">
                  </div>
                  <v-slider
                    v-if="step.command === 'brightness'"
                    v-model="step.brightness"
                    :min="0" :max="255" :step="1"
                    label="Brightness"
                    density="compact"
                    hide-details
                    class="mt-2"
                  />
                  <template v-if="step.command === 'animation'">
                    <v-select v-model="step.animation" :items="animations" label="Animation" density="compact" hide-details class="mt-2" />
                    <v-slider v-model="step.speed" :min="1" :max="255" :step="1" label="Speed" density="compact" hide-details class="mt-2" />
                  </template>
                </template>

                <!-- Variable steps: pick from the device's variable list -->
                <template v-if="step.type === 'variable_set' || step.type === 'variable_increment' || step.type === 'variable_toggle'">
                  <v-select
                    v-model="step.variable_id"
                    :items="variablesList"
                    label="Variable"
                    density="compact"
                    hide-details
                    class="mt-2"
                    :no-data-text="'No variables defined'"
                  />
                  <v-text-field v-if="step.type === 'variable_set'" v-model="step.value" label="Value" density="compact" class="mt-2" />
                  <v-text-field v-if="step.type === 'variable_increment'" v-model.number="step.increment" label="Increment" type="number" density="compact" class="mt-2" />
                </template>

                <!-- Page change: pick display + page -->
                <template v-if="step.type === 'page_change'">
                  <v-select
                    v-model="step.display_id"
                    :items="displaysList"
                    label="Display"
                    density="compact"
                    hide-details
                    class="mt-2"
                    :no-data-text="'No displays configured'"
                  />
                  <v-select
                    v-model="step.page_id"
                    :items="pagesForDisplay(step.display_id)"
                    item-title="title"
                    item-value="id"
                    label="Page"
                    density="compact"
                    hide-details
                    class="mt-2"
                    :no-data-text="'No pages for this display'"
                  />
                </template>
                <v-text-field v-if="step.type === 'notification'" v-model="step.message" label="Message" density="compact" class="mt-2" />
                <template v-if="step.type === 'http_request'">
                  <v-text-field v-model="step.url" label="URL" density="compact" class="mt-2" />
                  <v-select v-model="step.method" :items="['GET','POST','PUT','DELETE']" label="Method" density="compact" />
                  <v-text-field v-model="step.body" label="Body" density="compact" />
                </template>
                <template v-if="step.type === 'remote_action'">
                  <v-text-field v-model="step.node_id" label="Target node ID" hint="Device ID of the node to trigger" persistent-hint density="compact" class="mt-2" />
                  <v-text-field v-model="step.value" label="Remote action ID" hint="ID of the action to run on that node" persistent-hint density="compact" />
                </template>
                <template v-if="step.type === 'sync_action'">
                  <v-text-field v-model="step.value" label="Action ID (all nodes)" hint="Action every node runs in unison" persistent-hint density="compact" class="mt-2" />
                  <v-text-field v-model.number="step.delay_ms" label="Lead time (ms)" type="number" hint="Scheduling lead; needs cluster time sync" persistent-hint density="compact" />
                </template>
              </v-card-text>
            </v-card>
          </div>

          <v-btn prepend-icon="mdi-plus" variant="tonal" size="small" @click="addStep">
            Add Step
          </v-btn>
        </v-card-text>
        <v-divider />
        <v-card-actions>
          <v-spacer />
          <v-btn variant="text" @click="actionDialog = false">Cancel</v-btn>
          <v-btn color="primary" :loading="saving" @click="saveAction">Save</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>

    <!-- Macro Editor Dialog -->
    <v-dialog v-model="macroDialog" max-width="500">
      <v-card>
        <v-card-title>{{ editingMacro.id ? 'Edit Macro' : 'New Macro' }}</v-card-title>
        <v-divider />
        <v-card-text>
          <v-text-field v-model="editingMacro.id" label="ID" :disabled="!!editingMacro._existing" />
          <v-text-field v-model="editingMacro.name" label="Name" />
          <v-switch v-model="editingMacro.enabled" label="Enabled" color="primary" />
          <div class="text-subtitle-2 mb-2">Action IDs (one per line)</div>
          <v-textarea v-model="editingMacro.actionsText" label="Action IDs" rows="4" />
        </v-card-text>
        <v-divider />
        <v-card-actions>
          <v-spacer />
          <v-btn variant="text" @click="macroDialog = false">Cancel</v-btn>
          <v-btn color="primary" :loading="saving" @click="saveMacro">Save</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>
  </div>
</template>

<script setup>
import { onMounted, ref } from 'vue'
import api from '../api/client'
import { useWebSocketStore } from '../stores/websocket'

const wsStore = useWebSocketStore()
const loading = ref(false)
const saving = ref(false)
const statusMessage = ref(null)
const triggeringId = ref(null)

const actions = ref([])
const macros = ref([])
const history = ref([])

// Lists that populate the editor dropdowns ("choose from a list").
const outputsList = ref([])    // [{id, type}]
const variablesList = ref([])  // [id]
const inputsList = ref([])     // [id] (digital inputs)
const displaysList = ref([])   // [id]
const pagesList = ref([])      // [{id, title, displayId}]

const actionDialog = ref(false)
const macroDialog = ref(false)

const actionStepTypes = [
  { value: 'output_control', label: 'Control Output' },
  { value: 'variable_set', label: 'Variable Set' },
  { value: 'variable_increment', label: 'Variable Increment' },
  { value: 'variable_toggle', label: 'Variable Toggle' },
  { value: 'page_change', label: 'Page Change' },
  { value: 'notification', label: 'Notification' },
  { value: 'delay', label: 'Delay' },
  { value: 'mqtt_publish', label: 'MQTT Publish' },
  { value: 'ha_service_call', label: 'HA Service Call' },
  { value: 'http_request', label: 'HTTP Request' },
  { value: 'keyboard_shortcut', label: 'Keyboard Shortcut' },
  { value: 'media_control', label: 'Media Control' },
  { value: 'remote_action', label: 'Remote Action (another node)' },
  { value: 'sync_action', label: 'Sync Action (all nodes, timed)' },
]

// Digital-input events an action can be bound to (must match firmware names).
const inputEvents = ['Press', 'Release', 'LongPress', 'DoublePress', 'TriplePress', 'Hold', 'Repeat']
const outputCommands = [
  { value: 'digital', label: 'On / Off' },
  { value: 'rgb', label: 'Colour' },
  { value: 'brightness', label: 'Brightness' },
  { value: 'animation', label: 'Animation' },
]
const animations = ['solid', 'off', 'blink', 'breathe', 'rainbow', 'chase', 'colorwipe']

const pagesForDisplay = (displayId) =>
  pagesList.value.filter(p => !displayId || p.displayId === displayId)

// <input type="color"> helpers — convert between hex and the step's r/g/b ints.
const toHex = (n) => Math.max(0, Math.min(255, n | 0)).toString(16).padStart(2, '0')
const stepHex = (step) => `#${toHex(step.r || 0)}${toHex(step.g || 0)}${toHex(step.b || 0)}`
function setStepColor(step, hex) {
  const m = /^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})/i.exec(hex || '')
  if (!m) return
  step.r = parseInt(m[1], 16); step.g = parseInt(m[2], 16); step.b = parseInt(m[3], 16)
}

const newTrigger = () => ({ source: 'manual', input_id: '', event: 'Press' })
const editingAction = ref({ id: '', name: '', enabled: true, trigger: newTrigger(), steps: [], _existing: false })
const editingMacro = ref({ id: '', name: '', enabled: true, actionsText: '', _existing: false })

const formatTs = (ms) => ms ? `${(ms / 1000).toFixed(1)}s` : '—'

async function refresh() {
  loading.value = true
  statusMessage.value = null
  try {
    const [actData, macData] = await Promise.all([
      api.get('/api/actions'),
      api.get('/api/macros'),
    ])
    actions.value = actData.actions || []
    macros.value = macData.macros || []
    history.value = (actData.history || []).slice().reverse()

    // Dropdown sources — best-effort, don't fail the whole view if one errors.
    const [out, vars, ins, disp, pages] = await Promise.all([
      api.get('/api/outputs/state').catch(() => ({ outputs: [] })),
      api.get('/api/variables').catch(() => ({ variables: {} })),
      api.get('/api/inputs').catch(() => ({ digital: [] })),
      api.get('/api/displays').catch(() => ({ displays: [] })),
      api.get('/api/displays/pages').catch(() => ({ pages: [] })),
    ])
    outputsList.value = out.outputs || []
    variablesList.value = Object.keys(vars.variables || {})
    inputsList.value = (ins.digital || []).map(i => i.id).filter(Boolean)
    displaysList.value = (disp.displays || []).map(d => d.id).filter(Boolean)
    pagesList.value = pages.pages || []
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'Failed to load data' }
  } finally {
    loading.value = false
  }
}

function openNewAction() {
  editingAction.value = { id: '', name: '', enabled: true, trigger: newTrigger(), steps: [], _existing: false }
  actionDialog.value = true
}

function editAction(action) {
  editingAction.value = {
    ...action,
    trigger: { ...newTrigger(), ...(action.trigger || {}) },
    steps: action.steps ? JSON.parse(JSON.stringify(action.steps)) : [],
    _existing: true,
  }
  actionDialog.value = true
}

function addStep() {
  editingAction.value.steps.push({ type: 'output_control', command: 'digital', on: true, speed: 128, animation: 'solid' })
}

function removeStep(idx) {
  editingAction.value.steps.splice(idx, 1)
}

async function saveAction() {
  saving.value = true
  try {
    const t = editingAction.value.trigger || newTrigger()
    const payload = {
      id: editingAction.value.id,
      name: editingAction.value.name,
      enabled: editingAction.value.enabled,
      trigger: t.source === 'input'
        ? { source: 'input', input_id: t.input_id, event: t.event }
        : { source: 'manual', input_id: '', event: '' },
      steps: editingAction.value.steps,
      conditions: [],
    }
    await api.post('/api/actions', { actions: [payload] })
    await refresh()
    actionDialog.value = false
    statusMessage.value = { type: 'success', text: 'Action saved.' }
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'Save failed' }
  } finally {
    saving.value = false
  }
}

function openNewMacro() {
  editingMacro.value = { id: '', name: '', enabled: true, actionsText: '', _existing: false }
  macroDialog.value = true
}

function editMacro(macro) {
  editingMacro.value = {
    ...macro,
    actionsText: (macro.action_ids || []).join('\n'),
    _existing: true,
  }
  macroDialog.value = true
}

async function saveMacro() {
  saving.value = true
  try {
    const actionIds = editingMacro.value.actionsText
      .split('\n')
      .map(s => s.trim())
      .filter(Boolean)
    const payload = {
      id: editingMacro.value.id,
      name: editingMacro.value.name,
      enabled: editingMacro.value.enabled,
      action_ids: actionIds,
    }
    await api.post('/api/macros', { macros: [payload] })
    await refresh()
    macroDialog.value = false
    statusMessage.value = { type: 'success', text: 'Macro saved.' }
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'Save failed' }
  } finally {
    saving.value = false
  }
}

async function triggerAction(id) {
  triggeringId.value = id
  try {
    wsStore.send('action_trigger', { id })
    statusMessage.value = { type: 'info', text: `Triggered action: ${id}` }
  } finally {
    triggeringId.value = null
  }
}

function triggerMacro(id) {
  wsStore.send('action_trigger', { id, type: 'macro' })
  statusMessage.value = { type: 'info', text: `Triggered macro: ${id}` }
}

onMounted(() => {
  refresh()
})
</script>
