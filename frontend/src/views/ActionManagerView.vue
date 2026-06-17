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
                  <td>
                    {{ action.steps?.length ?? 0 }}
                    <div v-if="lastRunByAction[action.id]" class="text-caption text-medium-emphasis d-flex align-center ga-1">
                      <v-icon size="x-small" :color="lastRunByAction[action.id].success ? 'success' : 'error'">
                        {{ lastRunByAction[action.id].success ? 'mdi-check' : 'mdi-close' }}
                      </v-icon>
                      {{ formatTs(lastRunByAction[action.id].timestamp_ms) }}
                    </div>
                  </td>
                  <td>{{ action.conditions?.length ?? 0 }}</td>
                  <td>
                    <v-tooltip :text="action.enabled ? 'Enabled — click to disable' : 'Disabled — click to enable'" location="top">
                      <template #activator="{ props }">
                        <v-btn
                          v-bind="props"
                          :icon="action.enabled ? 'mdi-check-circle' : 'mdi-circle-outline'"
                          :color="action.enabled ? 'success' : 'default'"
                          size="small"
                          variant="text"
                          :loading="togglingId === action.id"
                          @click="toggleEnabled(action)"
                        />
                      </template>
                    </v-tooltip>
                  </td>
                  <td class="text-right">
                    <v-btn
                      icon="mdi-flask-outline"
                      size="small"
                      variant="text"
                      title="Dry-run (simulate without driving hardware)"
                      aria-label="Dry-run action"
                      :loading="simulatingId === action.id"
                      @click="simulateAction(action.id)"
                    />
                    <v-btn
                      icon="mdi-play"
                      size="small"
                      variant="text"
                      color="success"
                      aria-label="Run action"
                      :loading="triggeringId === action.id"
                      @click="triggerAction(action.id)"
                    />
                    <v-btn
                      icon="mdi-pencil"
                      size="small"
                      variant="text"
                      aria-label="Edit action"
                      @click="editAction(action)"
                    />
                    <v-btn
                      icon="mdi-delete"
                      size="small"
                      variant="text"
                      color="error"
                      aria-label="Delete action"
                      @click="confirmDelete('action', action)"
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
                      aria-label="Run macro"
                      @click="triggerMacro(macro.id)"
                    />
                    <v-btn
                      icon="mdi-pencil"
                      size="small"
                      variant="text"
                      aria-label="Edit macro"
                      @click="editMacro(macro)"
                    />
                    <v-btn
                      icon="mdi-delete"
                      size="small"
                      variant="text"
                      color="error"
                      aria-label="Delete macro"
                      @click="confirmDelete('macro', macro)"
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

        <!-- Scenes (#39) -->
        <v-card class="mt-4">
          <v-card-title class="d-flex align-center justify-space-between">
            Scenes
            <v-chip size="small">{{ scenes.length }}</v-chip>
          </v-card-title>
          <v-card-text>
            <div class="text-caption text-medium-emphasis mb-2">
              Snapshot the current output + variable state, then restore it in one tap (or from a "Restore Scene" action step).
            </div>
            <div class="d-flex ga-2 mb-3">
              <v-text-field
                v-model="newSceneName"
                label="New scene name"
                density="compact"
                hide-details
                @keyup.enter="captureScene"
              />
              <v-btn
                color="primary"
                :loading="sceneBusy"
                :disabled="!newSceneName.trim()"
                prepend-icon="mdi-camera"
                @click="captureScene"
              >
                Capture
              </v-btn>
            </div>
            <v-table density="compact">
              <tbody>
                <tr v-for="scene in scenes" :key="scene.name">
                  <td>
                    <div class="font-weight-medium">{{ scene.name }}</div>
                    <div class="text-caption text-medium-emphasis">
                      {{ scene.outputs }} output{{ scene.outputs === 1 ? '' : 's' }} · {{ scene.variables }} var{{ scene.variables === 1 ? '' : 's' }}
                    </div>
                  </td>
                  <td class="text-right">
                    <v-btn icon="mdi-play" size="small" variant="text" color="success" aria-label="Restore scene" :loading="sceneBusy" @click="restoreScene(scene.name)" />
                    <v-btn icon="mdi-delete" size="small" variant="text" color="error" aria-label="Delete scene" :loading="sceneBusy" @click="deleteScene(scene.name)" />
                  </td>
                </tr>
                <tr v-if="scenes.length === 0">
                  <td class="text-medium-emphasis text-center py-4">No scenes captured yet.</td>
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
                :items="[{ value: 'manual', title: 'Manual / triggered by name' }, { value: 'input', title: 'When an input fires' }, { value: 'schedule', title: 'On a schedule' }]"
                label="Trigger"
                density="compact"
                hide-details
              />
            </v-col>
            <template v-if="editingAction.trigger.source === 'schedule'">
              <v-col cols="12" sm="4">
                <v-select
                  v-model="editingAction.trigger.schedule_mode"
                  :items="[{ value: 'interval', title: 'Every N seconds' }, { value: 'daily', title: 'Daily at time' }]"
                  label="Mode"
                  density="compact"
                  hide-details
                />
              </v-col>
              <v-col cols="12" sm="4">
                <v-text-field
                  v-if="editingAction.trigger.schedule_mode === 'daily'"
                  v-model="editingAction.trigger.daily_time"
                  type="time"
                  label="Time (UTC)"
                  hint="Needs NTP/cluster time"
                  persistent-hint
                  density="compact"
                />
                <v-text-field
                  v-else
                  v-model.number="editingAction.trigger.interval_sec"
                  type="number"
                  :min="1"
                  label="Interval (seconds)"
                  density="compact"
                  hide-details
                />
              </v-col>
            </template>
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
              <v-col cols="12" sm="6">
                <v-text-field
                  v-model.number="editingAction.trigger.debounce_ms"
                  label="Debounce (ms)"
                  type="number"
                  min="0"
                  density="compact"
                  hide-details
                  hint="Wait until the input is quiet this long, then fire once (coalesces chatter). 0 = off."
                  persistent-hint
                />
              </v-col>
              <v-col cols="12" sm="6">
                <v-text-field
                  v-model.number="editingAction.trigger.cooldown_ms"
                  label="Cooldown (ms)"
                  type="number"
                  min="0"
                  density="compact"
                  hide-details
                  hint="Minimum gap between fires; events arriving sooner are dropped (throttle). 0 = off."
                  persistent-hint
                />
              </v-col>
            </template>
          </v-row>

          <v-divider class="my-3" />
          <div class="d-flex align-center mb-2">
            <div class="text-subtitle-2">Conditions</div>
            <span class="text-caption text-medium-emphasis ml-2">all must be true to run</span>
          </div>
          <div v-for="(cond, idx) in editingAction.conditions" :key="'cond' + idx" class="mb-2">
            <v-row align="center" no-gutters class="ga-2">
              <v-col cols="12" sm="4">
                <v-select
                  v-model="cond.variable_id"
                  :items="variablesList"
                  label="Variable"
                  density="compact"
                  hide-details
                  :no-data-text="'No variables defined'"
                />
              </v-col>
              <v-col cols="8" sm="3">
                <v-select
                  v-model="cond.op"
                  :items="conditionOps"
                  item-title="label"
                  item-value="value"
                  label="Operator"
                  density="compact"
                  hide-details
                />
              </v-col>
              <v-col cols="auto" class="flex-grow-1">
                <v-text-field v-model="cond.value" label="Value" density="compact" hide-details />
              </v-col>
              <v-col cols="auto">
                <v-btn icon="mdi-close" size="small" variant="text" @click="removeCondition(idx)" />
              </v-col>
            </v-row>
          </div>
          <v-btn prepend-icon="mdi-plus" variant="tonal" size="small" @click="addCondition">
            Add Condition
          </v-btn>

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
                  <v-text-field v-model="step.body" label="Payload" hint="Supports {{variable}} templating" persistent-hint density="compact" />
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
                  <v-row v-if="step.command === 'beep'" class="mt-1">
                    <v-col cols="12" sm="6">
                      <v-text-field v-model.number="step.frequency" label="Frequency (Hz)" type="number" :min="100" :max="20000" density="compact" hide-details />
                    </v-col>
                    <v-col cols="12" sm="6">
                      <v-text-field v-model.number="step.duration_ms" label="Duration (ms)" type="number" :min="20" :max="5000" density="compact" hide-details />
                    </v-col>
                  </v-row>
                  <v-slider
                    v-if="step.command === 'angle'"
                    v-model.number="step.angle"
                    :min="0" :max="180" :step="1"
                    label="Angle (°)"
                    thumb-label
                    density="compact"
                    hide-details
                    class="mt-2"
                  />
                  <v-text-field
                    v-if="step.command === 'move'"
                    v-model.number="step.position"
                    label="Target position (steps)"
                    type="number"
                    density="compact"
                    hide-details
                    class="mt-2"
                  />
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
                  <v-text-field v-if="step.type === 'variable_set'" v-model="step.value" label="Value" hint="Supports {{variable}} templating" persistent-hint density="compact" class="mt-2" />
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
                <v-text-field v-if="step.type === 'notification'" v-model="step.message" label="Message" hint="Supports {{variable}} templating" persistent-hint density="compact" class="mt-2" />
                <template v-if="step.type === 'http_request'">
                  <v-text-field v-model="step.url" label="URL" hint="Webhook URL — supports {{variable}} templating" persistent-hint density="compact" class="mt-2" />
                  <v-select v-model="step.method" :items="['GET','POST','PUT','DELETE']" label="Method" density="compact" />
                  <v-text-field v-model="step.body" label="Body" hint="JSON payload — supports {{variable}} templating" persistent-hint density="compact" />
                </template>
                <template v-if="step.type === 'remote_action'">
                  <v-text-field v-model="step.node_id" label="Target node ID" hint="Device ID of the node to trigger" persistent-hint density="compact" class="mt-2" />
                  <v-text-field v-model="step.value" label="Remote action ID" hint="ID of the action to run on that node" persistent-hint density="compact" />
                </template>
                <template v-if="step.type === 'sync_action'">
                  <v-text-field v-model="step.value" label="Action ID (all nodes)" hint="Action every node runs in unison" persistent-hint density="compact" class="mt-2" />
                  <v-text-field v-model.number="step.delay_ms" label="Lead time (ms)" type="number" hint="Scheduling lead; needs cluster time sync" persistent-hint density="compact" />
                </template>
                <template v-if="step.type === 'keyboard_shortcut'">
                  <v-combobox
                    v-model="step.keys"
                    :items="keyboardPresets"
                    label="Key combo"
                    hint="Modifiers + key, joined by '+', e.g. CTRL+ALT+DELETE or GUI+L"
                    persistent-hint
                    density="compact"
                    class="mt-2"
                  />
                  <div class="text-caption text-medium-emphasis mt-1">
                    Sends keystrokes to a paired computer (USB on S3, Bluetooth on ESP32).
                  </div>
                </template>
                <template v-if="step.type === 'media_control'">
                  <v-select
                    v-model="step.media_command"
                    :items="mediaCommands"
                    item-title="label"
                    item-value="value"
                    label="Media command"
                    density="compact"
                    class="mt-2"
                  />
                </template>

                <!-- If: condition; runs steps up to the matching Else/End If only when true -->
                <template v-if="step.type === 'if'">
                  <v-row class="mt-1" align="center">
                    <v-col cols="12" sm="5">
                      <v-select v-model="step.variable_id" :items="variablesList" label="Variable" density="compact" hide-details />
                    </v-col>
                    <v-col cols="6" sm="3">
                      <v-select v-model="step.op" :items="conditionOps" item-title="label" item-value="value" label="Op" density="compact" hide-details />
                    </v-col>
                    <v-col cols="6" sm="4">
                      <v-text-field v-model="step.value" label="Value" density="compact" hide-details />
                    </v-col>
                  </v-row>
                </template>
                <template v-if="step.type === 'wait_until'">
                  <v-row class="mt-1" align="center">
                    <v-col cols="12" sm="5">
                      <v-select v-model="step.variable_id" :items="variablesList" label="Variable" density="compact" hide-details />
                    </v-col>
                    <v-col cols="6" sm="3">
                      <v-select v-model="step.op" :items="conditionOps" item-title="label" item-value="value" label="Op" density="compact" hide-details />
                    </v-col>
                    <v-col cols="6" sm="4">
                      <v-text-field v-model="step.value" label="Value" density="compact" hide-details />
                    </v-col>
                  </v-row>
                  <v-text-field v-model.number="step.delay_ms" type="number" min="0" label="Timeout (ms)" hint="0 = 30s cap. On timeout the step fails and the action stops." persistent-hint density="compact" class="mt-2" />
                </template>
                <template v-if="step.type === 'scene_restore'">
                  <v-combobox v-model="step.value" :items="scenes.map(s => s.name)" label="Scene" hint="Supports {{variable}} templating" persistent-hint density="compact" class="mt-2" />
                </template>
                <template v-if="step.type === 'repeat'">
                  <v-text-field v-model.number="step.repeat_count" type="number" :min="1" :max="65535" label="Repeat count" hint="Loops the steps up to the matching End Repeat" persistent-hint density="compact" class="mt-2" />
                </template>
                <div v-if="['else','endif','endrepeat'].includes(step.type)" class="text-caption text-medium-emphasis mt-2">
                  Block marker — pair it with its matching {{ step.type === 'endrepeat' ? 'Repeat' : 'If' }} step.
                </div>
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

          <div class="text-subtitle-2 mt-2 mb-1 d-flex align-center">
            Parameters
            <span class="text-caption text-medium-emphasis ml-2">set these variables before the actions run</span>
          </div>
          <v-row v-for="(p, idx) in editingMacro.params" :key="'mp' + idx" no-gutters class="ga-2 mb-1" align="center">
            <v-col>
              <v-combobox v-model="p.variable_id" :items="variablesList" label="Variable" density="compact" hide-details />
            </v-col>
            <v-col>
              <v-text-field v-model="p.value" label="Value" density="compact" hide-details />
            </v-col>
            <v-col cols="auto">
              <v-btn icon="mdi-close" size="small" variant="text" @click="editingMacro.params.splice(idx, 1)" />
            </v-col>
          </v-row>
          <v-btn size="small" variant="text" prepend-icon="mdi-plus" @click="editingMacro.params.push({ variable_id: '', value: '' })">Add parameter</v-btn>
        </v-card-text>
        <v-divider />
        <v-card-actions>
          <v-spacer />
          <v-btn variant="text" @click="macroDialog = false">Cancel</v-btn>
          <v-btn color="primary" :loading="saving" @click="saveMacro">Save</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>

    <!-- Delete Confirm Dialog (shared by actions & macros) -->
    <v-dialog v-model="deleteDialog" max-width="360">
      <v-card :title="`Delete ${deleting.kind === 'macro' ? 'Macro' : 'Action'}`">
        <v-card-text>
          Are you sure you want to delete <strong>{{ deleting.item?.name || deleting.item?.id }}</strong>?
          This cannot be undone.
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn @click="deleteDialog = false">Cancel</v-btn>
          <v-btn color="error" :loading="deletingBusy" @click="doDelete">Delete</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>

    <!-- Dry-run / simulate result (#42) -->
    <v-dialog v-model="simDialog" max-width="560">
      <v-card title="Dry run — what would happen">
        <v-card-text>
          <v-alert v-if="!simResult.ok && simResult.error" type="error" variant="tonal" density="compact" class="mb-2">
            {{ simResult.error }}
          </v-alert>
          <p class="text-caption text-medium-emphasis mb-2">
            Simulated steps for <strong>{{ simResult.id }}</strong> — no hardware was driven and no variables changed.
          </p>
          <ol v-if="simResult.steps.length" class="pl-4">
            <li v-for="(step, idx) in simResult.steps" :key="idx" class="mb-1 font-monospace text-body-2">{{ step }}</li>
          </ol>
          <p v-else class="text-medium-emphasis">No steps would run (conditions not met, or the action is empty).</p>
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn @click="simDialog = false">Close</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>
  </div>
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import api from '../api/client'
import { useWebSocketStore } from '../stores/websocket'

const wsStore = useWebSocketStore()
const loading = ref(false)
const saving = ref(false)
const statusMessage = ref(null)
const triggeringId = ref(null)
const togglingId = ref(null)
const simulatingId = ref(null)
const simDialog = ref(false)
const simResult = ref({ id: '', ok: false, steps: [], error: '' })

const actions = ref([])
const macros = ref([])
const history = ref([])
const scenes = ref([])
const newSceneName = ref('')
const sceneBusy = ref(false)

// Most-recent history entry per action id, for the inline last-run indicator.
// `history` is newest-first (reversed on load), so the first match wins.
const lastRunByAction = computed(() => {
  const map = {}
  for (const entry of history.value) {
    if (entry.action_id && !map[entry.action_id]) map[entry.action_id] = entry
  }
  return map
})

// Lists that populate the editor dropdowns ("choose from a list").
const outputsList = ref([])    // [{id, type}]
const variablesList = ref([])  // [id]
const inputsList = ref([])     // [id] (digital inputs)
const displaysList = ref([])   // [id]
const pagesList = ref([])      // [{id, title, displayId}]

const actionDialog = ref(false)
const macroDialog = ref(false)

const deleteDialog = ref(false)
const deletingBusy = ref(false)
const deleting = ref({ kind: 'action', item: null })

const actionStepTypes = [
  { value: 'output_control', label: 'Control Output' },
  { value: 'variable_set', label: 'Variable Set' },
  { value: 'variable_increment', label: 'Variable Increment' },
  { value: 'variable_toggle', label: 'Variable Toggle' },
  { value: 'page_change', label: 'Page Change' },
  { value: 'notification', label: 'Notification' },
  { value: 'delay', label: 'Delay' },
  { value: 'wait_until', label: 'Wait Until (variable)' },
  { value: 'scene_restore', label: 'Restore Scene' },
  { value: 'mqtt_publish', label: 'MQTT Publish' },
  { value: 'ha_service_call', label: 'HA Service Call' },
  { value: 'http_request', label: 'HTTP Request' },
  { value: 'keyboard_shortcut', label: 'Keyboard Shortcut' },
  { value: 'media_control', label: 'Media Control' },
  { value: 'remote_action', label: 'Remote Action (another node)' },
  { value: 'sync_action', label: 'Sync Action (all nodes, timed)' },
  { value: 'if', label: 'If (condition)' },
  { value: 'else', label: 'Else' },
  { value: 'endif', label: 'End If' },
  { value: 'repeat', label: 'Repeat (loop start)' },
  { value: 'endrepeat', label: 'End Repeat' },
]

// Digital-input events an action can be bound to (must match firmware names).
const inputEvents = ['Press', 'Release', 'LongPress', 'DoublePress', 'TriplePress', 'Hold', 'Repeat', 'RotateCW', 'RotateCCW', 'KeyPress', 'KeyRelease']
const outputCommands = [
  { value: 'digital', label: 'On / Off' },
  { value: 'rgb', label: 'Colour' },
  { value: 'brightness', label: 'Brightness' },
  { value: 'animation', label: 'Animation' },
  { value: 'beep', label: 'Beep (buzzer)' },
  { value: 'angle', label: 'Angle (servo)' },
  { value: 'move', label: 'Move to (stepper)' },
]
const animations = ['solid', 'off', 'blink', 'breathe', 'rainbow', 'chase', 'colorwipe', 'fire']
// Suggested keyboard combos (free text is also allowed — the combobox is editable).
const keyboardPresets = [
  'CTRL+C', 'CTRL+V', 'CTRL+ALT+DELETE', 'CTRL+SHIFT+ESC',
  'GUI+L', 'GUI+D', 'ALT+TAB', 'ALT+F4', 'F5',
]
// Media commands (values must match firmware mediaCmdFromString).
const mediaCommands = [
  { value: 'play_pause', label: 'Play / Pause' },
  { value: 'stop', label: 'Stop' },
  { value: 'next_track', label: 'Next track' },
  { value: 'prev_track', label: 'Previous track' },
  { value: 'volume_up', label: 'Volume up' },
  { value: 'volume_down', label: 'Volume down' },
  { value: 'mute', label: 'Mute' },
]
// Condition operators (values must match firmware conditionOpFromString).
const conditionOps = [
  { value: 'eq', label: '= equals' },
  { value: 'ne', label: '≠ not equal' },
  { value: 'lt', label: '< less than' },
  { value: 'lte', label: '≤ at most' },
  { value: 'gt', label: '> greater than' },
  { value: 'gte', label: '≥ at least' },
]

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

const newTrigger = () => ({
  source: 'manual', input_id: '', event: 'Press',
  schedule_mode: 'interval', interval_sec: 60, daily_time: '08:00',
  debounce_ms: 0, cooldown_ms: 0,
})

// HH:MM (UTC) <-> seconds-since-midnight helpers for the daily schedule.
const secondsToHhmm = (secs) => {
  if (secs === undefined || secs === null || secs < 0) return '08:00'
  const h = Math.floor(secs / 3600)
  const m = Math.floor((secs % 3600) / 60)
  return `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}`
}
const hhmmToSeconds = (hhmm) => {
  const [h, m] = String(hhmm || '').split(':').map((n) => parseInt(n, 10))
  if (Number.isNaN(h) || Number.isNaN(m)) return -1
  return h * 3600 + m * 60
}
const editingAction = ref({ id: '', name: '', enabled: true, trigger: newTrigger(), conditions: [], steps: [], _existing: false })
const editingMacro = ref({ id: '', name: '', enabled: true, actionsText: '', params: [], _existing: false })

const formatTs = (ms) => ms ? `${(ms / 1000).toFixed(1)}s` : '—'

async function refresh() {
  loading.value = true
  statusMessage.value = null
  try {
    const [actData, macData, sceneData] = await Promise.all([
      api.get('/api/actions'),
      api.get('/api/macros'),
      api.get('/api/scenes').catch(() => ({ scenes: [] })),
    ])
    actions.value = actData.actions || []
    macros.value = macData.macros || []
    history.value = (actData.history || []).slice().reverse()
    scenes.value = sceneData.scenes || []

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
  editingAction.value = { id: '', name: '', enabled: true, trigger: newTrigger(), conditions: [], steps: [], _existing: false }
  actionDialog.value = true
}

function editAction(action) {
  const trigger = { ...newTrigger(), ...(action.trigger || {}) }
  // The device stores the daily schedule as seconds-since-midnight; the picker
  // edits an HH:MM string.
  if (action.trigger && action.trigger.daily_seconds >= 0) {
    trigger.daily_time = secondsToHhmm(action.trigger.daily_seconds)
  }
  editingAction.value = {
    ...action,
    trigger,
    conditions: action.conditions ? JSON.parse(JSON.stringify(action.conditions)) : [],
    steps: action.steps ? JSON.parse(JSON.stringify(action.steps)) : [],
    _existing: true,
  }
  actionDialog.value = true
}

function addCondition() {
  editingAction.value.conditions.push({ variable_id: '', op: 'eq', value: '' })
}

function removeCondition(idx) {
  editingAction.value.conditions.splice(idx, 1)
}

function addStep() {
  editingAction.value.steps.push({ type: 'output_control', command: 'digital', on: true, speed: 128, animation: 'solid', frequency: 2000, duration_ms: 200, angle: 90, position: 0, keys: '', media_command: 'play_pause', op: 'eq', repeat_count: 1 })
}

function removeStep(idx) {
  editingAction.value.steps.splice(idx, 1)
}

async function saveAction() {
  saving.value = true
  try {
    const t = editingAction.value.trigger || newTrigger()
    let trigger
    if (t.source === 'input') {
      trigger = {
        source: 'input', input_id: t.input_id, event: t.event,
        debounce_ms: Math.max(0, parseInt(t.debounce_ms, 10) || 0),
        cooldown_ms: Math.max(0, parseInt(t.cooldown_ms, 10) || 0),
      }
    } else if (t.source === 'schedule') {
      trigger = {
        source: 'schedule',
        schedule_mode: t.schedule_mode || 'interval',
        interval_sec: Math.max(1, parseInt(t.interval_sec, 10) || 0),
        daily_seconds: t.schedule_mode === 'daily' ? hhmmToSeconds(t.daily_time) : -1,
      }
    } else {
      trigger = { source: 'manual', input_id: '', event: '' }
    }
    const payload = {
      id: editingAction.value.id,
      name: editingAction.value.name,
      enabled: editingAction.value.enabled,
      trigger,
      steps: editingAction.value.steps,
      conditions: (editingAction.value.conditions || []).filter(c => c.variable_id),
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
  editingMacro.value = { id: '', name: '', enabled: true, actionsText: '', params: [], _existing: false }
  macroDialog.value = true
}

function editMacro(macro) {
  editingMacro.value = {
    ...macro,
    actionsText: (macro.action_ids || []).join('\n'),
    params: (macro.params || []).map(p => ({ ...p })),
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
      params: (editingMacro.value.params || []).filter(p => p.variable_id),
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

// Quick enable/disable without opening the editor. registerAction is upsert-by-id,
// so posting the single action with `enabled` flipped leaves the others untouched.
async function toggleEnabled(action) {
  togglingId.value = action.id
  try {
    await api.post('/api/actions', { actions: [{ ...action, enabled: !action.enabled }] })
    await refresh()
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'Toggle failed' }
  } finally {
    togglingId.value = null
  }
}

async function simulateAction(id) {
  simulatingId.value = id
  try {
    const res = await api.post('/api/actions/simulate', { id })
    simResult.value = { id, ok: res.ok, steps: res.steps || [], error: res.error || '' }
    simDialog.value = true
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'Simulate failed' }
  } finally {
    simulatingId.value = null
  }
}

function triggerMacro(id) {
  wsStore.send('action_trigger', { id, type: 'macro' })
  statusMessage.value = { type: 'info', text: `Triggered macro: ${id}` }
}

// ── Scenes (#39) ──────────────────────────────────────────────────────────────
async function captureScene() {
  const name = (newSceneName.value || '').trim()
  if (!name) return
  sceneBusy.value = true
  try {
    const res = await api.post('/api/scenes/capture', { name })
    scenes.value = res.scenes || scenes.value
    newSceneName.value = ''
    statusMessage.value = { type: 'success', text: `Captured scene "${name}".` }
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'Capture failed' }
  } finally {
    sceneBusy.value = false
  }
}

async function restoreScene(name) {
  sceneBusy.value = true
  try {
    await api.post('/api/scenes/restore', { name })
    statusMessage.value = { type: 'success', text: `Restored scene "${name}".` }
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'Restore failed' }
  } finally {
    sceneBusy.value = false
  }
}

async function deleteScene(name) {
  sceneBusy.value = true
  try {
    const res = await api.delete(`/api/scenes/${encodeURIComponent(name)}`)
    scenes.value = res.scenes || scenes.value.filter(s => s.name !== name)
    statusMessage.value = { type: 'success', text: `Deleted scene "${name}".` }
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'Delete failed' }
  } finally {
    sceneBusy.value = false
  }
}

function confirmDelete(kind, item) {
  deleting.value = { kind, item }
  deleteDialog.value = true
}

async function doDelete() {
  const { kind, item } = deleting.value
  if (!item) return
  deletingBusy.value = true
  try {
    const base = kind === 'macro' ? '/api/macros/' : '/api/actions/'
    await api.delete(base + encodeURIComponent(item.id))
    await refresh()
    deleteDialog.value = false
    statusMessage.value = { type: 'success', text: `${kind === 'macro' ? 'Macro' : 'Action'} deleted.` }
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'Delete failed' }
  } finally {
    deletingBusy.value = false
  }
}

onMounted(() => {
  refresh()
})
</script>
