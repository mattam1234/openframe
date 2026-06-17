<!--
  Dependency-free SVG sparkline. Renders a series of numbers as a small inline
  trend line — no charting library, so it adds nothing to the LittleFS bundle.
-->
<template>
  <svg
    v-if="points.length >= 2"
    :width="width"
    :height="height"
    :viewBox="`0 0 ${width} ${height}`"
    preserveAspectRatio="none"
    class="sparkline"
  >
    <polyline
      :points="polyline"
      fill="none"
      :stroke="color"
      stroke-width="1.5"
      stroke-linejoin="round"
      stroke-linecap="round"
    />
    <circle :cx="lastX" :cy="lastY" :r="1.8" :fill="color" />
  </svg>
  <span v-else class="text-caption text-disabled">—</span>
</template>

<script setup>
import { computed } from 'vue'

const props = defineProps({
  values: { type: Array, default: () => [] },
  width: { type: Number, default: 80 },
  height: { type: Number, default: 24 },
  color: { type: String, default: 'currentColor' },
})

const points = computed(() =>
  props.values.filter((v) => typeof v === 'number' && Number.isFinite(v))
)

// Map values into the SVG box, leaving 2px padding so the stroke isn't clipped.
const coords = computed(() => {
  const vals = points.value
  if (vals.length < 2) return []
  const min = Math.min(...vals)
  const max = Math.max(...vals)
  const span = max - min || 1
  const pad = 2
  const w = props.width - pad * 2
  const h = props.height - pad * 2
  return vals.map((v, i) => {
    const x = pad + (i / (vals.length - 1)) * w
    // Invert Y so larger values sit higher.
    const y = pad + (1 - (v - min) / span) * h
    return [x, y]
  })
})

const polyline = computed(() => coords.value.map(([x, y]) => `${x.toFixed(1)},${y.toFixed(1)}`).join(' '))
const lastX = computed(() => (coords.value.length ? coords.value[coords.value.length - 1][0] : 0))
const lastY = computed(() => (coords.value.length ? coords.value[coords.value.length - 1][1] : 0))
</script>

<style scoped>
.sparkline {
  display: block;
}
</style>
