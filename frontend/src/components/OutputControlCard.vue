<template>
  <v-card variant="tonal">
    <v-card-text>
      <div class="d-flex align-center justify-space-between mb-2">
        <div class="d-flex align-center ga-2">
          <v-icon :color="model.on ? swatch : 'grey'">{{ typeIcon }}</v-icon>
          <div>
            <div class="text-subtitle-2">{{ model.id }}</div>
            <div class="text-caption text-medium-emphasis">
              {{ model.type }}<span v-if="isStrip"> · {{ model.led_count }} LED{{ model.led_count === 1 ? '' : 's' }}</span>
            </div>
          </div>
        </div>
        <v-switch
          :model-value="model.on"
          color="primary"
          density="compact"
          hide-details
          inset
          @update:model-value="setDigital"
        />
      </div>

      <!-- Colour (rgb + ws2812) -->
      <template v-if="hasColor">
        <div class="d-flex ga-2 mb-2">
          <div
            v-for="preset in presets"
            :key="preset"
            class="preset-swatch"
            :style="{ background: preset }"
            @click="applyHex(preset)"
          />
        </div>
        <v-color-picker
          :model-value="hex"
          mode="hexa"
          hide-inputs
          width="100%"
          class="mb-2"
          @update:model-value="onPickColor"
        />
      </template>

      <!-- Brightness (ws2812 + dimmable led) -->
      <div v-if="hasBrightness" class="mb-1">
        <div class="text-caption text-medium-emphasis">Brightness</div>
        <v-slider
          :model-value="model.brightness"
          :min="0"
          :max="255"
          :step="1"
          density="compact"
          hide-details
          @update:model-value="setBrightness"
        />
      </div>

      <!-- Animation (ws2812 only) -->
      <template v-if="isStrip">
        <v-select
          :model-value="model.animation"
          :items="animations"
          label="Animation"
          density="compact"
          hide-details
          class="mb-2"
          @update:model-value="setAnimation"
        />
        <div v-if="dynamicAnimation">
          <div class="text-caption text-medium-emphasis">Speed</div>
          <v-slider
            :model-value="model.speed"
            :min="1"
            :max="255"
            :step="1"
            density="compact"
            hide-details
            @update:model-value="setSpeed"
          />
        </div>
      </template>

      <!-- Buzzer -->
      <v-btn
        v-if="model.type === 'buzzer'"
        size="small"
        variant="tonal"
        prepend-icon="mdi-music-note"
        @click="beep"
      >
        Beep
      </v-btn>
    </v-card-text>
  </v-card>
</template>

<script setup>
import { computed, reactive, watch } from 'vue'
import api from '../api/client'

const props = defineProps({
  output: { type: Object, required: true },
})
const emit = defineEmits(['changed'])

// Local mirror so sliders/pickers feel responsive; kept in sync with the prop.
const model = reactive({ ...props.output })
watch(() => props.output, (v) => Object.assign(model, v), { deep: true })

const isStrip = computed(() => model.type === 'ws2812')
const hasColor = computed(() => model.type === 'ws2812' || model.type === 'rgb')
const hasBrightness = computed(() => model.type === 'ws2812' || model.type === 'led')
const dynamicAnimation = computed(() => !['solid', 'off'].includes(model.animation))

const animations = ['solid', 'off', 'blink', 'breathe', 'rainbow', 'chase', 'colorwipe']
const presets = ['#FF0000', '#00FF00', '#0000FF', '#FFFF00', '#00FFFF', '#FF00FF', '#FFFFFF', '#FF8800']

const typeIcon = computed(() => ({
  led: 'mdi-led-on', rgb: 'mdi-palette', ws2812: 'mdi-led-strip-variant',
  relay: 'mdi-electric-switch', buzzer: 'mdi-music-note',
}[model.type] || 'mdi-power-plug'))

const toHex = (n) => Math.max(0, Math.min(255, n | 0)).toString(16).padStart(2, '0')
const hex = computed(() => `#${toHex(model.r)}${toHex(model.g)}${toHex(model.b)}`)
const swatch = computed(() => hex.value)

function hexToRgb(h) {
  const m = /^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})/i.exec(h || '')
  return m ? { r: parseInt(m[1], 16), g: parseInt(m[2], 16), b: parseInt(m[3], 16) } : { r: 0, g: 0, b: 0 }
}

async function control(body) {
  try {
    const res = await api.post('/api/outputs/control', { id: model.id, ...body })
    if (res.outputs) emit('changed', res.outputs)
  } catch (err) {
    emit('changed', null, err.message || 'Control failed')
  }
}

// Sliders / colour picker fire rapidly while dragging; throttle to ~10 req/s with
// a trailing send so the final value always lands, without flooding the device's
// connection-limited async server.
let throttleTimer = null
let pendingBody = null
function controlThrottled(body) {
  pendingBody = body
  if (throttleTimer) return
  control(pendingBody); pendingBody = null
  throttleTimer = setTimeout(() => {
    throttleTimer = null
    if (pendingBody) controlThrottled(pendingBody)
  }, 100)
}

const setDigital = (on) => { model.on = on; control({ command: 'digital', on }) }
const setBrightness = (v) => { model.brightness = v; controlThrottled({ command: 'brightness', brightness: v }) }
const setSpeed = (v) => { model.speed = v; controlThrottled({ command: 'animation', animation: model.animation, speed: v }) }

function applyHex(h, throttled = false) {
  const { r, g, b } = hexToRgb(h)
  model.r = r; model.g = g; model.b = b
  const body = { command: 'rgb', r, g, b }
  throttled ? controlThrottled(body) : control(body)
}
function onPickColor(h) { applyHex(typeof h === 'string' ? h : (h?.hex || h?.hexa), true) }

function setAnimation(anim) {
  model.animation = anim
  const { r, g, b } = model
  control({ command: 'animation', animation: anim, speed: model.speed, r, g, b })
}

const beep = () => control({ command: 'beep', frequency: 1000, duration_ms: 200 })
</script>

<style scoped>
.preset-swatch {
  width: 22px;
  height: 22px;
  border-radius: 4px;
  cursor: pointer;
  border: 1px solid rgba(255, 255, 255, 0.2);
}
</style>
