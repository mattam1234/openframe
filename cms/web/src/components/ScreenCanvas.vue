<template>
  <div class="screen">
    <div class="text-caption text-medium-emphasis">
      {{ screen.id }} · {{ screen.type }} · {{ width }}×{{ height }}
      <template v-if="screen.title"> · {{ screen.title }}</template>
      <span v-if="screen.truncated" class="text-warning"> · truncated</span>
    </div>
    <canvas ref="canvas" class="screen-canvas" />
  </div>
</template>

<script setup>
import { onMounted, ref, watch } from 'vue'

// Reconstruct what a device display is showing (from get_screens) the same way
// the firmware renders it: Adafruit-GFX default font is 6px wide × 8px tall per
// glyph at text size 1, top-left anchored.
const SCALE = 3
const GFX_CHAR_W = 6
const GFX_CHAR_H = 8

const props = defineProps({ screen: { type: Object, required: true } })
const canvas = ref(null)
const width = ref(Math.max(1, Number(props.screen.width) || 128))
const height = ref(Math.max(1, Number(props.screen.height) || 64))

function draw() {
  const el = canvas.value
  if (!el) return
  const w = Math.max(1, Number(props.screen.width) || 128)
  const h = Math.max(1, Number(props.screen.height) || 64)
  width.value = w
  height.value = h
  el.width = w * SCALE
  el.height = h * SCALE
  const ctx = el.getContext('2d')
  ctx.fillStyle = '#070b10'
  ctx.fillRect(0, 0, el.width, el.height)
  ctx.fillStyle = '#cfe8ff'
  ctx.textBaseline = 'top'

  const drawText = (text, x, y, size) => {
    const s = Math.max(1, Number(size) || 1)
    ctx.font = `${GFX_CHAR_H * s * SCALE}px ui-monospace, Menlo, Consolas, monospace`
    let cx = (Number(x) || 0) * SCALE
    const cy = (Number(y) || 0) * SCALE
    for (const ch of String(text)) {
      ctx.fillText(ch, cx, cy)
      cx += GFX_CHAR_W * s * SCALE
    }
  }

  for (const widget of props.screen.widgets || []) {
    if (widget.text) drawText(widget.text, widget.x, widget.y, widget.size)
  }
  if (props.screen.notification) drawText(props.screen.notification, 0, Math.max(0, h - GFX_CHAR_H), 1)
}

onMounted(draw)
watch(() => props.screen, draw, { deep: true })
</script>

<style scoped>
.screen { display: inline-block; margin: 4px 8px 4px 0; }
.screen-canvas {
  display: block;
  image-rendering: pixelated;
  border-radius: 4px;
  border: 1px solid rgba(var(--v-border-color), var(--v-border-opacity));
  background: #070b10;
}
</style>
