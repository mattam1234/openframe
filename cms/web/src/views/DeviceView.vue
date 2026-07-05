<template>
  <div>
    <div class="d-flex align-center mb-4 ga-2">
      <v-btn icon="mdi-arrow-left" variant="text" :to="{ name: 'fleet' }" aria-label="Back to fleet" />
      <h2 class="text-h6 mb-0">{{ device?.name || routeId }}</h2>
      <v-chip v-if="device" :color="device.online ? 'success' : 'grey'" size="small" variant="flat" class="ml-1">
        {{ device.online ? 'online' : 'offline' }}
      </v-chip>
    </div>

    <v-alert v-if="notFound" type="warning" variant="tonal">Unknown device.</v-alert>

    <v-row v-else>
      <!-- Status + history -->
      <v-col cols="12" md="6">
        <v-card border flat>
          <v-card-title class="text-subtitle-1">Status</v-card-title>
          <v-table density="compact">
            <tbody>
              <tr v-for="f in fields" :key="f.label">
                <th class="text-left text-medium-emphasis">{{ f.label }}</th>
                <td>{{ f.value }}</td>
              </tr>
            </tbody>
          </v-table>
          <v-divider />
          <v-card-text>
            <div class="text-caption text-medium-emphasis mb-1">Free heap</div>
            <Sparkline :values="heapSeries" :width="320" :height="40" color="#60a5fa" />
            <div class="text-caption text-medium-emphasis mt-3 mb-1">RSSI</div>
            <Sparkline :values="rssiSeries" :width="320" :height="40" color="#22c55e" />
            <p v-if="heapSeries.length < 2" class="text-caption text-medium-emphasis mt-2 mb-0">
              No telemetry history yet.
            </p>
          </v-card-text>
        </v-card>
      </v-col>

      <v-col cols="12" md="6">
        <!-- Tags & site -->
        <v-card border flat class="mb-4">
          <v-card-title class="text-subtitle-1">Tags &amp; site</v-card-title>
          <v-card-text>
            <div class="d-flex ga-2 mb-3">
              <v-text-field
                v-model="site"
                label="Site / location"
                density="compact"
                variant="outlined"
                hide-details
                @keyup.enter="saveSite"
              />
              <v-btn variant="tonal" :loading="savingSite" @click="saveSite">Save</v-btn>
            </div>
            <div class="mb-2">
              <v-chip
                v-for="t in device?.tags || []"
                :key="t"
                size="small"
                class="mr-1 mb-1"
                closable
                @click:close="saveTags((device.tags || []).filter((x) => x !== t))"
              >{{ t }}</v-chip>
              <span v-if="!(device?.tags || []).length" class="text-medium-emphasis">no tags</span>
            </div>
            <div class="d-flex ga-2">
              <v-text-field
                v-model="newTag"
                placeholder="add tag"
                density="compact"
                variant="outlined"
                hide-details
                @keyup.enter="addTag"
              />
              <v-btn variant="tonal" @click="addTag">Add</v-btn>
            </div>
          </v-card-text>
        </v-card>

        <!-- Notes -->
        <v-card border flat class="mb-4">
          <v-card-title class="text-subtitle-1">Notes</v-card-title>
          <v-card-text>
            <v-textarea
              v-model="notes"
              rows="3"
              density="compact"
              variant="outlined"
              hide-details
              placeholder="Operational notes — location, quirks, maintenance history…"
            />
            <div class="d-flex align-center ga-2 mt-2">
              <v-btn variant="tonal" :loading="savingNotes" @click="saveNotes">Save notes</v-btn>
              <span class="text-caption text-medium-emphasis">{{ notesMsg }}</span>
            </div>
          </v-card-text>
        </v-card>

        <!-- Actions + profiles -->
        <v-card border flat>
          <v-card-title class="text-subtitle-1">Actions</v-card-title>
          <v-card-text>
            <div class="d-flex ga-2 mb-3">
              <v-btn variant="tonal" @click="cmd('identify')">Identify</v-btn>
              <v-btn variant="tonal" @click="cmd('get_status')">Refresh status</v-btn>
              <v-btn variant="tonal" color="warning" @click="cmd('reboot')">Reboot</v-btn>
            </div>

            <div class="text-subtitle-2 mb-1">Profiles</div>
            <div class="d-flex align-center ga-2 mb-2">
              <v-btn size="small" variant="tonal" :loading="loadingProfiles" @click="loadProfiles">Load profiles</v-btn>
              <span class="text-caption text-medium-emphasis">{{ profilesMsg }}</span>
            </div>
            <div v-for="p in profiles" :key="p.id" class="d-flex align-center justify-space-between py-1">
              <span :class="{ 'text-warning': p.id === activeProfile }">
                {{ p.name || p.id }}<span v-if="p.id === activeProfile"> (active)</span>
              </span>
              <v-btn size="x-small" variant="tonal" :disabled="p.id === activeProfile" @click="activateProfile(p.id)">
                Activate
              </v-btn>
            </div>

            <v-divider class="my-3" />
            <pre class="result text-caption">{{ result }}</pre>
          </v-card-text>
        </v-card>

        <!-- Screen preview (#57) -->
        <v-card border flat class="mt-4">
          <v-card-title class="text-subtitle-1">Screen preview</v-card-title>
          <v-card-text>
            <p class="text-caption text-medium-emphasis mb-2">
              What each display is currently showing, reconstructed from its widgets.
            </p>
            <div class="d-flex align-center ga-2 mb-2">
              <v-btn size="small" variant="tonal" :loading="loadingScreens" @click="loadScreens">Load screens</v-btn>
              <v-checkbox v-model="screensLive" label="auto-refresh" density="compact" hide-details />
              <span class="text-caption text-medium-emphasis">{{ screensMsg }}</span>
            </div>
            <ScreenCanvas v-for="s in screens" :key="s.id" :screen="s" />
          </v-card-text>
        </v-card>

        <!-- Push config (#62) -->
        <v-card border flat class="mt-4">
          <v-card-title class="text-subtitle-1">Push config</v-card-title>
          <v-card-text>
            <p class="text-caption text-medium-emphasis mb-2">
              Partial config JSON (same shape as <code>POST /api/config</code>). The device merges and saves;
              MQTT/time/notify/weather changes apply live, anything else reboots it.
            </p>
            <v-textarea
              v-model="configJson"
              rows="6"
              density="compact"
              variant="outlined"
              hide-details
              class="mono mb-2"
              placeholder='{ "device": { "name": "Lobby Frame" } }'
            />
            <v-btn variant="tonal" @click="previewConfig">Preview changes</v-btn>

            <div v-if="configDiff" class="mt-3">
              <p v-if="!configDiff.length" class="text-caption text-medium-emphasis">No differences from the last pushed config.</p>
              <v-table v-else density="compact">
                <tbody>
                  <tr v-for="d in configDiff" :key="d.path">
                    <th class="text-left mono">{{ d.path }}</th>
                    <td>
                      <span :class="`diff-${d.type}`">{{ d.type }}</span>
                      {{ fmtVal(d.before) }} → {{ fmtVal(d.after) }}
                    </td>
                  </tr>
                </tbody>
              </v-table>
            </div>

            <div v-if="pendingConfig && configDiff && configDiff.length" class="d-flex ga-2 mt-3">
              <v-btn color="error" @click="pushConfig">Confirm push</v-btn>
              <v-btn variant="text" @click="cancelConfig">Cancel</v-btn>
            </div>
          </v-card-text>
        </v-card>

        <!-- Device log: Warning+ lines mirrored to <base>/<id>/log (#83) -->
        <v-card border flat class="mt-4">
          <v-card-title class="text-subtitle-1">Device log</v-card-title>
          <v-card-text>
            <p class="text-caption text-medium-emphasis mb-2">
              Warning+ log lines the device mirrors over MQTT — streamed live, most recent last.
            </p>
            <p v-if="!logEntries.length" class="text-caption text-medium-emphasis mb-0">
              No log lines received yet.
            </p>
            <div v-else class="logbox">
              <div v-for="(e, i) in logEntries" :key="i" class="d-flex ga-2 align-baseline py-1">
                <span class="text-caption text-medium-emphasis text-no-wrap">{{ fmtLogTime(e.t) }}</span>
                <v-chip :color="logColor(e.level)" size="x-small" variant="flat">{{ e.level }}</v-chip>
                <span v-if="e.tag" class="text-caption text-medium-emphasis text-no-wrap">{{ e.tag }}</span>
                <span class="text-caption logmsg">{{ e.msg }}</span>
              </div>
            </div>
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>
  </div>
</template>

<script setup>
import { computed, onMounted, onUnmounted, ref, watch } from 'vue'
import { Sparkline } from '@shared'
import api from '../api'
import { useFleet } from '../store'
import { fmtUptime, fmtHeap, fmtAgo, rssiLabel, asNum } from '../format'
import ScreenCanvas from '../components/ScreenCanvas.vue'

const props = defineProps({ id: { type: String, required: true } })
const routeId = computed(() => props.id)

const { devices, deviceLogs, seedLogs } = useFleet()
const device = computed(() => devices.value.find((d) => d.deviceId === props.id) || loadedDevice.value)
const loadedDevice = ref(null)
const notFound = ref(false)

const history = ref([])
const heapSeries = computed(() => history.value.map((s) => asNum(s.freeHeap)).filter((v) => v != null))
const rssiSeries = computed(() => history.value.map((s) => asNum(s.rssi)).filter((v) => v != null))

const fields = computed(() => {
  const d = device.value || {}
  return [
    { label: 'ID', value: d.deviceId || props.id },
    { label: 'Board', value: d.board || '—' },
    { label: 'Firmware', value: d.version || '—' },
    { label: 'IP', value: d.ip || '—' },
    { label: 'RSSI', value: rssiLabel(d.rssi) },
    { label: 'Free heap', value: fmtHeap(d.freeHeap) },
    { label: 'CPU load', value: asNum(d.cpuLoadPercent) == null ? '—' : `${d.cpuLoadPercent}%` },
    { label: 'Uptime', value: fmtUptime(d.uptimeMs) },
    { label: 'Profile', value: d.activeProfileId || '—' },
    { label: 'Features', value: featureList(d.features) },
    { label: 'Last seen', value: fmtAgo(d.lastSeen) },
  ]
})

// Build-time feature flags from the heartbeat (older firmware doesn't send them).
function featureList(features) {
  if (!features || typeof features !== 'object') return '—'
  const on = Object.entries(features).filter(([, v]) => v).map(([k]) => k)
  return on.length ? on.join(', ') : 'none'
}

const newTag = ref('')
const site = ref('')
const savingSite = ref(false)
const notes = ref('')
const notesMsg = ref('')
const savingNotes = ref(false)
const result = ref('—')
const profiles = ref([])
const activeProfile = ref('')
const profilesMsg = ref('')
const loadingProfiles = ref(false)

async function saveTags(tags) {
  try {
    const updated = await api.put(`/api/devices/${encodeURIComponent(props.id)}/tags`, { tags })
    if (device.value) device.value.tags = updated.tags || tags
  } catch (e) { result.value = `error: ${e.message}` }
}
function addTag() {
  const t = newTag.value.trim()
  const tags = device.value?.tags || []
  if (t && !tags.includes(t)) saveTags([...tags, t])
  newTag.value = ''
}

async function saveSite() {
  savingSite.value = true
  try {
    const updated = await api.put(`/api/devices/${encodeURIComponent(props.id)}/site`, { site: site.value })
    if (device.value) device.value.site = updated.site
  } catch (e) { result.value = `error: ${e.message}` }
  finally { savingSite.value = false }
}

async function saveNotes() {
  savingNotes.value = true
  notesMsg.value = 'Saving…'
  try {
    await api.put(`/api/devices/${encodeURIComponent(props.id)}/notes`, { notes: notes.value })
    notesMsg.value = 'Saved.'
  } catch (e) { notesMsg.value = `Failed: ${e.message}` }
  finally { savingNotes.value = false }
}

async function cmd(type, payload) {
  result.value = `sending ${type}…`
  try {
    result.value = JSON.stringify(await api.post(`/api/devices/${encodeURIComponent(props.id)}/cmd`, { type, payload }), null, 2)
  } catch (e) { result.value = `error: ${e.message}` }
}

async function loadProfiles() {
  loadingProfiles.value = true
  profilesMsg.value = 'loading…'
  try {
    const body = await api.post(`/api/devices/${encodeURIComponent(props.id)}/cmd`, { type: 'get_profiles' })
    profiles.value = body.profiles || []
    activeProfile.value = body.active || ''
    profilesMsg.value = profiles.value.length ? '' : 'no profiles'
  } catch (e) { profilesMsg.value = `error: ${e.message}` }
  finally { loadingProfiles.value = false }
}
async function activateProfile(id) {
  await cmd('activate_profile', { id })
  loadProfiles()
}

// ── Screen preview (#57) ─────────────────────────────────────────────────────
const screens = ref([])
const loadingScreens = ref(false)
const screensMsg = ref('')
const screensLive = ref(false)
let screensTimer = null

async function loadScreens() {
  loadingScreens.value = true
  screensMsg.value = 'loading…'
  try {
    // Use the read-only GET route, not POST /cmd — the latter is blocked for
    // viewer-token holders by the read-method auth gate.
    const body = await api.get(`/api/devices/${encodeURIComponent(props.id)}/screens`)
    if (!body.ok) { screensMsg.value = `error: ${body.error || 'failed'}`; return }
    screens.value = body.screens || []
    screensMsg.value = screens.value.length ? '' : 'no displays configured'
  } catch (e) { screensMsg.value = `error: ${e.message}` }
  finally { loadingScreens.value = false }
}
watch(screensLive, (on) => {
  clearInterval(screensTimer)
  screensTimer = null
  if (on) { loadScreens(); screensTimer = setInterval(loadScreens, 3000) }
})

// ── Config diff + push (#62) ─────────────────────────────────────────────────
const configJson = ref('')
const configDiff = ref(null)
const pendingConfig = ref(null)
const baselineKey = computed(() => `of-cms-lastconfig-${props.id}`)

function loadBaseline() {
  try { const r = localStorage.getItem(baselineKey.value); return r ? JSON.parse(r) : null } catch { return null }
}

// Leaf-level diff of two JSON values → {path, type, before, after}.
function diffJson(base, next, path = '', out = []) {
  const isObj = (v) => v && typeof v === 'object' && !Array.isArray(v)
  if (isObj(base) && isObj(next)) {
    for (const key of new Set([...Object.keys(base), ...Object.keys(next)])) {
      diffJson(base[key], next[key], path ? `${path}.${key}` : key, out)
    }
  } else if (JSON.stringify(base) !== JSON.stringify(next)) {
    out.push({ path, type: base === undefined ? 'added' : next === undefined ? 'removed' : 'changed', before: base, after: next })
  }
  return out
}
const fmtVal = (v) => (v === undefined ? '∅' : typeof v === 'object' ? JSON.stringify(v) : String(v))

function previewConfig() {
  const raw = configJson.value.trim()
  if (!raw) { result.value = 'enter config JSON'; return }
  try { pendingConfig.value = JSON.parse(raw) }
  catch (e) { result.value = `invalid JSON: ${e.message}`; return }
  configDiff.value = diffJson(loadBaseline() ?? {}, pendingConfig.value)
}
function cancelConfig() { pendingConfig.value = null; configDiff.value = null }
async function pushConfig() {
  if (!pendingConfig.value) return
  try { localStorage.setItem(baselineKey.value, JSON.stringify(pendingConfig.value)) } catch { /* ignore */ }
  result.value = 'pushing config…'
  try {
    const res = await api.post(`/api/devices/${encodeURIComponent(props.id)}/cmd`,
      { type: 'apply_config', payload: pendingConfig.value })
    // Older firmware acks omit restartRequired — it always rebooted, so treat
    // "missing" as a reboot.
    result.value = res.ok
      ? (res.restartRequired === false
          ? 'Config applied live — no reboot needed.'
          : 'Config saved — device is rebooting to apply.')
      : JSON.stringify(res, null, 2)
  } catch (e) { result.value = `error: ${e.message}` }
  cancelConfig()
}

// ── Device log (#83) ─────────────────────────────────────────────────────────
const logEntries = computed(() => deviceLogs[props.id] || [])
const fmtLogTime = (t) => (t ? new Date(t).toLocaleTimeString() : '')
const logColor = (level) =>
  level === 'error' || level === 'fatal' ? 'error' : level === 'warning' ? 'warning' : 'grey'

async function load() {
  // Seed fields from REST if the device isn't already in the live store.
  if (!devices.value.find((d) => d.deviceId === props.id)) {
    try { loadedDevice.value = await api.get(`/api/devices/${encodeURIComponent(props.id)}`) }
    catch { notFound.value = true }
  }
  try { history.value = (await api.get(`/api/devices/${encodeURIComponent(props.id)}/history`)).samples || [] }
  catch { /* history is optional */ }
  try { seedLogs(props.id, (await api.get(`/api/devices/${encodeURIComponent(props.id)}/logs`)).entries || []) }
  catch { /* logs are optional */ }
}

// Don't clobber the editors while typing; seed them once the device resolves.
watch(device, (d) => {
  if (!d) return
  if (!notes.value) notes.value = d.notes || ''
  if (!site.value) site.value = d.site || ''
}, { immediate: true })

onMounted(load)
onUnmounted(() => clearInterval(screensTimer))
</script>

<style scoped>
.result {
  white-space: pre-wrap;
  word-break: break-word;
  max-height: 220px;
  overflow: auto;
  background: rgba(var(--v-theme-on-surface), 0.04);
  border-radius: 4px;
  padding: 8px;
  margin: 0;
}
.mono :deep(textarea) { font-family: 'Roboto Mono', monospace; font-size: 0.85rem; }
.mono { font-family: 'Roboto Mono', monospace; }
.logbox {
  max-height: 240px;
  overflow: auto;
  background: rgba(var(--v-theme-on-surface), 0.04);
  border-radius: 4px;
  padding: 4px 8px;
}
.logmsg { word-break: break-word; }
.diff-added { color: #22c55e; font-weight: 600; }
.diff-removed { color: #ef4444; font-weight: 600; }
.diff-changed { color: #f59e0b; font-weight: 600; }
</style>
