<template>
  <div>
    <v-row align="center">
      <v-col>
        <h1 class="text-h5 mb-0">
          <v-icon class="mr-2">mdi-gesture-tap</v-icon>
          {{ $t('nav.touch') }}
        </h1>
        <div class="text-medium-emphasis text-caption">
          Map the resistive touch panel's raw readings onto screen pixels
        </div>
      </v-col>
      <v-col cols="auto">
        <v-btn :loading="loading" prepend-icon="mdi-refresh" variant="tonal" @click="refresh">
          Refresh
        </v-btn>
      </v-col>
    </v-row>

    <v-row v-if="error">
      <v-col>
        <v-alert type="error" variant="tonal" closable @click:close="error = ''">
          {{ error }}
        </v-alert>
      </v-col>
    </v-row>
    <v-row v-if="success">
      <v-col>
        <v-alert type="success" variant="tonal" closable @click:close="success = ''">
          {{ success }}
        </v-alert>
      </v-col>
    </v-row>

    <!-- This build doesn't drive a touch controller (endpoint 404s). -->
    <v-row v-if="notSupported">
      <v-col>
        <v-alert type="info" variant="tonal">
          This device build doesn't drive a touch panel, so there's nothing to calibrate.
        </v-alert>
      </v-col>
    </v-row>

    <template v-else>
      <v-row>
        <!-- Live touch preview: a scaled model of the glass with a dot where the
             current mapping thinks your finger is. -->
        <v-col cols="12" md="6">
          <v-card>
            <v-card-title class="text-subtitle-1">Live preview</v-card-title>
            <v-card-subtitle>
              Drag your finger across the panel — the dot should track it. If it
              mirrors or transposes, adjust the switches below.
            </v-card-subtitle>
            <v-card-text>
              <div
                class="touch-stage"
                :style="{ aspectRatio: `${form.width} / ${form.height}` }"
              >
                <div
                  v-if="live.down"
                  class="touch-dot"
                  :style="dotStyle"
                />
                <div v-else class="touch-hint text-medium-emphasis">
                  Touch the panel…
                </div>
              </div>
              <div class="d-flex justify-space-between text-caption text-medium-emphasis mt-2">
                <span>raw (post-swap): <strong>{{ live.rawX }}, {{ live.rawY }}</strong></span>
                <span>mapped: <strong>{{ live.x }}, {{ live.y }}</strong> px</span>
              </div>
            </v-card-text>
          </v-card>
        </v-col>

        <!-- Guided four-corner capture: touch each physical corner, tap its
             button, then Apply to derive the raw extents + invert flags. -->
        <v-col cols="12" md="6">
          <v-card>
            <v-card-title class="text-subtitle-1">Guided calibration</v-card-title>
            <v-card-subtitle>
              Touch and hold each screen corner, then tap its button. Set
              <em>Swap axes</em> first if dragging horizontally moves the dot vertically.
            </v-card-subtitle>
            <v-card-text>
              <v-row dense>
                <v-col v-for="c in cornerDefs" :key="c.key" cols="6">
                  <v-btn
                    block
                    :color="corners[c.key] ? 'success' : undefined"
                    variant="tonal"
                    :disabled="!live.down"
                    @click="captureCorner(c.key)"
                  >
                    {{ c.label }}
                    <div class="text-caption">
                      {{ corners[c.key] ? `${corners[c.key].x}, ${corners[c.key].y}` : '—' }}
                    </div>
                  </v-btn>
                </v-col>
              </v-row>
              <div class="d-flex ga-2 mt-3">
                <v-btn
                  color="primary"
                  variant="flat"
                  :disabled="!allCornersCaptured"
                  @click="applyCorners"
                >
                  Apply corners
                </v-btn>
                <v-btn variant="text" @click="resetCorners">Reset</v-btn>
              </div>
            </v-card-text>
          </v-card>
        </v-col>
      </v-row>

      <!-- Manual values — also the target of Apply corners, so you can tweak. -->
      <v-row>
        <v-col cols="12">
          <v-card>
            <v-card-title class="text-subtitle-1">Calibration values</v-card-title>
            <v-card-text>
              <v-row dense>
                <v-col cols="6" sm="3">
                  <v-text-field
                    v-model.number="form.rawXMin"
                    type="number"
                    label="Raw X min"
                    prepend-inner-icon="mdi-axis-x-arrow"
                    density="compact"
                    hide-details
                  />
                </v-col>
                <v-col cols="6" sm="3">
                  <v-text-field
                    v-model.number="form.rawXMax"
                    type="number"
                    label="Raw X max"
                    prepend-inner-icon="mdi-axis-x-arrow"
                    density="compact"
                    hide-details
                  />
                </v-col>
                <v-col cols="6" sm="3">
                  <v-text-field
                    v-model.number="form.rawYMin"
                    type="number"
                    label="Raw Y min"
                    prepend-inner-icon="mdi-axis-y-arrow"
                    density="compact"
                    hide-details
                  />
                </v-col>
                <v-col cols="6" sm="3">
                  <v-text-field
                    v-model.number="form.rawYMax"
                    type="number"
                    label="Raw Y max"
                    prepend-inner-icon="mdi-axis-y-arrow"
                    density="compact"
                    hide-details
                  />
                </v-col>

                <v-col cols="6" sm="3">
                  <v-text-field
                    v-model.number="form.width"
                    type="number"
                    label="Screen width (px)"
                    density="compact"
                    hide-details
                  />
                </v-col>
                <v-col cols="6" sm="3">
                  <v-text-field
                    v-model.number="form.height"
                    type="number"
                    label="Screen height (px)"
                    density="compact"
                    hide-details
                  />
                </v-col>
              </v-row>

              <v-row dense class="mt-2">
                <v-col cols="12" sm="4">
                  <v-switch
                    v-model="form.swapXY"
                    label="Swap axes"
                    color="primary"
                    density="compact"
                    hide-details
                  />
                </v-col>
                <v-col cols="12" sm="4">
                  <v-switch
                    v-model="form.invertX"
                    label="Invert X"
                    color="primary"
                    density="compact"
                    hide-details
                  />
                </v-col>
                <v-col cols="12" sm="4">
                  <v-switch
                    v-model="form.invertY"
                    label="Invert Y"
                    color="primary"
                    density="compact"
                    hide-details
                  />
                </v-col>
              </v-row>
            </v-card-text>
            <v-card-actions>
              <v-spacer />
              <v-btn
                color="primary"
                variant="flat"
                prepend-icon="mdi-content-save"
                :loading="saving"
                @click="save"
              >
                Save &amp; apply
              </v-btn>
            </v-card-actions>
          </v-card>
        </v-col>
      </v-row>
    </template>
  </div>
</template>

<script setup>
import { computed, onMounted, onUnmounted, reactive, ref } from 'vue'
import api from '../api/client'

const loading = ref(false)
const saving = ref(false)
const error = ref('')
const success = ref('')
const notSupported = ref(false)

// The editable calibration. Populated from the device on first load; the live
// poll below deliberately does NOT overwrite it, so it can't fight your edits.
const form = reactive({
  rawXMin: 0, rawXMax: 4095, rawYMin: 0, rawYMax: 4095,
  width: 320, height: 240,
  swapXY: false, invertX: false, invertY: false,
})

// Latest raw + mapped point from the device (refreshed by the poll).
const live = reactive({ rawX: 0, rawY: 0, x: 0, y: 0, down: false })

const cornerDefs = [
  { key: 'tl', label: 'Top-left' },
  { key: 'tr', label: 'Top-right' },
  { key: 'bl', label: 'Bottom-left' },
  { key: 'br', label: 'Bottom-right' },
]
const corners = reactive({ tl: null, tr: null, bl: null, br: null })
const allCornersCaptured = computed(() => cornerDefs.every(c => corners[c.key]))

const dotStyle = computed(() => {
  const w = form.width || 1
  const h = form.height || 1
  return {
    left: `${(live.x / w) * 100}%`,
    top: `${(live.y / h) * 100}%`,
  }
})

function applyResponse(data) {
  form.rawXMin = data.rawXMin ?? form.rawXMin
  form.rawXMax = data.rawXMax ?? form.rawXMax
  form.rawYMin = data.rawYMin ?? form.rawYMin
  form.rawYMax = data.rawYMax ?? form.rawYMax
  form.width = data.width ?? form.width
  form.height = data.height ?? form.height
  form.swapXY = !!data.swapXY
  form.invertX = !!data.invertX
  form.invertY = !!data.invertY
}

function applyLive(data) {
  live.rawX = data.lastRawX ?? 0
  live.rawY = data.lastRawY ?? 0
  live.x = data.lastX ?? 0
  live.y = data.lastY ?? 0
  // The device reports the real press state, so held-still (unchanged raw) still
  // reads as down and released reads as up.
  live.down = !!data.down
}

async function refresh() {
  loading.value = true
  error.value = ''
  try {
    const data = await api.get('/api/touch')
    notSupported.value = false
    applyResponse(data)
    applyLive(data)
  } catch (e) {
    // The endpoint only exists on builds that drive a touch controller.
    if (String(e.message || '').includes('404')) {
      notSupported.value = true
    } else {
      error.value = String(e.message || e)
    }
  } finally {
    loading.value = false
  }
}

async function poll() {
  if (notSupported.value) return
  try {
    applyLive(await api.get('/api/touch'))
  } catch { /* transient — the next tick retries */ }
}

function captureCorner(key) {
  corners[key] = { x: live.rawX, y: live.rawY }
}
function resetCorners() {
  corners.tl = corners.tr = corners.bl = corners.br = null
}

// Derive raw extents (and the invert flags) from the four captured corners.
// Assumes Swap axes is already correct so captured X is the horizontal reading.
function applyCorners() {
  const left = (corners.tl.x + corners.bl.x) / 2
  const right = (corners.tr.x + corners.br.x) / 2
  const top = (corners.tl.y + corners.tr.y) / 2
  const bottom = (corners.bl.y + corners.br.y) / 2

  if (Math.round(left) === Math.round(right) || Math.round(top) === Math.round(bottom)) {
    error.value = 'Captured corners are too close — re-capture, and set Swap axes if needed.'
    return
  }

  if (left <= right) {
    form.rawXMin = Math.round(left); form.rawXMax = Math.round(right); form.invertX = false
  } else {
    form.rawXMin = Math.round(right); form.rawXMax = Math.round(left); form.invertX = true
  }
  if (top <= bottom) {
    form.rawYMin = Math.round(top); form.rawYMax = Math.round(bottom); form.invertY = false
  } else {
    form.rawYMin = Math.round(bottom); form.rawYMax = Math.round(top); form.invertY = true
  }
  success.value = 'Derived calibration from corners. Review the values, then Save.'
}

async function save() {
  error.value = ''
  success.value = ''
  if (form.rawXMin >= form.rawXMax || form.rawYMin >= form.rawYMax) {
    error.value = 'Raw min must be less than raw max on both axes.'
    return
  }
  saving.value = true
  try {
    const res = await api.post('/api/touch', {
      rawXMin: form.rawXMin, rawXMax: form.rawXMax,
      rawYMin: form.rawYMin, rawYMax: form.rawYMax,
      width: form.width, height: form.height,
      swapXY: form.swapXY, invertX: form.invertX, invertY: form.invertY,
    })
    success.value = res?.message || 'Touch calibration saved and applied.'
  } catch (e) {
    error.value = String(e.message || e)
  } finally {
    saving.value = false
  }
}

let timer = null
onMounted(async () => {
  await refresh()
  timer = setInterval(poll, 300)
})
onUnmounted(() => {
  if (timer) clearInterval(timer)
})
</script>

<style scoped>
.touch-stage {
  position: relative;
  width: 100%;
  max-width: 480px;
  margin: 0 auto;
  border: 2px solid rgba(var(--v-border-color), var(--v-border-opacity));
  border-radius: 6px;
  background: rgba(var(--v-theme-on-surface), 0.04);
  overflow: hidden;
}
.touch-dot {
  position: absolute;
  width: 18px;
  height: 18px;
  margin: -9px 0 0 -9px;
  border-radius: 50%;
  background: rgb(var(--v-theme-primary));
  box-shadow: 0 0 0 4px rgba(var(--v-theme-primary), 0.25);
  transition: left 0.06s linear, top 0.06s linear;
}
.touch-hint {
  position: absolute;
  inset: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 0.9rem;
}
</style>
