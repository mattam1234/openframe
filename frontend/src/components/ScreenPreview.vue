<template>
  <div>
    <div class="text-caption text-medium-emphasis mb-1">
      {{ screen.id }} · {{ screen.width }}×{{ screen.height }} · {{ screen.page || 'no page' }}
      <span v-if="screen.color" class="ml-1">· colour</span>
    </div>
    <canvas
      ref="canvas"
      :width="screen.width"
      :height="screen.height"
      class="screen-preview"
      :style="style"
    />
  </div>
</template>

<script setup>
import { ref, computed, watch, onMounted } from 'vue'

// Renders a preview from the device's reported screen content (/api/screens):
// geometry, the resolved widgets, and colour. It's an approximation, not a pixel-
// exact framebuffer — the GFX glyph cell is ~6px wide × 8px tall per char at size 1,
// drawn from the top-left, which we mimic. Colour panels render in colour; mono
// (OLED) panels render every non-black colour as the "on" tint.
const props = defineProps({
  screen: { type: Object, required: true },
  scale: { type: Number, default: 3 },
})

const canvas = ref(null)

// Mono panels draw "on" pixels in this tint; colour panels use the widget colour.
const ON = '#cfe8ff'
const isColor = computed(() => !!props.screen.color)

const style = computed(() => ({
  width: `${props.screen.width * props.scale}px`,
  height: `${props.screen.height * props.scale}px`,
  imageRendering: 'pixelated',
  background: bgColor(props.screen.bg, '#000'),
  borderRadius: '4px',
  border: '1px solid rgba(255,255,255,0.15)',
}))

// Resolve a widget/page colour to a CSS colour for this panel kind.
function col(c) {
  if (isColor.value) return c || ON
  return (c && c.toLowerCase() !== '#000000') ? ON : '#000'
}
function bgColor(c, fallback) {
  if (!c) return fallback
  return isColor.value ? c : (c.toLowerCase() !== '#000000' ? ON : '#000')
}

function draw() {
  const c = canvas.value
  if (!c) return
  const ctx = c.getContext('2d')
  ctx.fillStyle = bgColor(props.screen.bg, '#000')
  ctx.fillRect(0, 0, c.width, c.height)
  ctx.textBaseline = 'top'

  for (const w of props.screen.widgets || []) {
    const color = col(w.color)
    const type = w.type || 'text'   // text/value/datetime arrive without `type`
    if (type === 'rect') {
      drawRect(ctx, w.x || 0, w.y || 0, w.w || 1, w.h || 1, color, w.filled)
    } else if (type === 'circle') {
      drawCircle(ctx, w.x || 0, w.y || 0, w.w || 6, color, w.filled)
    } else if (type === 'led') {
      drawCircle(ctx, w.x || 0, w.y || 0, w.w || 5, color, true)
    } else if (type === 'line') {
      ctx.strokeStyle = color
      ctx.lineWidth = 1
      ctx.beginPath()
      ctx.moveTo(w.x || 0, w.y || 0)
      ctx.lineTo((w.x || 0) + (w.w || 0), (w.y || 0) + (w.h || 0))
      ctx.stroke()
    } else if (type === 'bar') {
      drawBar(ctx, w, color)
    } else if (type === 'icon') {
      // Approximate the device's primitive icons with an outlined box + label.
      drawRect(ctx, w.x || 0, w.y || 0, w.w || 12, w.w || 12, color, false)
    } else if (type === 'image') {
      // The live preview can't decode the device's OFIM blob — show a placeholder.
      drawRect(ctx, w.x || 0, w.y || 0, w.w || 16, w.h || 16, color, false)
    } else if (type === 'gauge') {
      drawGauge(ctx, w, color)
    } else if (type === 'button' || type === 'nav' || type === 'momentary'
            || type === 'setvalue' || type === 'cycle') {
      // Filled face (if a bg is set) + border + centred label — mirrors renderWidget.
      const bw = w.w || 60, bh = w.h || 24
      if (w.bg != null) drawRect(ctx, w.x || 0, w.y || 0, bw, bh, col(w.bg), true)
      drawRect(ctx, w.x || 0, w.y || 0, bw, bh, color, false)
      const size = w.size || 1
      const text = w.text ?? ''
      const tw = text.length * 6 * size
      ctx.fillStyle = color
      ctx.font = `${8 * size}px monospace`
      ctx.fillText(text, (w.x || 0) + Math.max(0, (bw - tw) / 2),
        (w.y || 0) + Math.max(0, (bh - 8 * size) / 2))
    } else if (type === 'toggle') {
      const bw = w.w || 40, bh = w.h || 20
      const on = (w.val ?? 0) >= ((w.min ?? 0) + (w.max ?? 1)) / 2
      drawRect(ctx, w.x || 0, w.y || 0, bw, bh, color, on)   // filled when on
      const knob = bh - 4
      const kx = on ? (w.x || 0) + bw - knob - 2 : (w.x || 0) + 2
      drawRect(ctx, kx, (w.y || 0) + 2, knob, knob, on ? col(w.bg) || '#000' : color, true)
    } else if (type === 'slider') {
      const bw = w.w || 80, bh = w.h || 16
      const midY = (w.y || 0) + bh / 2
      ctx.strokeStyle = color; ctx.lineWidth = 1
      ctx.beginPath(); ctx.moveTo(w.x || 0, midY); ctx.lineTo((w.x || 0) + bw, midY); ctx.stroke()
      const span = (w.max ?? 100) - (w.min ?? 0)
      let frac = span ? ((w.val ?? w.min ?? 0) - (w.min ?? 0)) / span : 0
      frac = Math.max(0, Math.min(1, frac))
      drawRect(ctx, (w.x || 0) + frac * bw - 3, w.y || 0, 6, bh, color, true)
    } else if (type === 'stepper') {
      const bw = w.w || 70, bh = w.h || 24
      drawRect(ctx, w.x || 0, w.y || 0, bw, bh, color, false)
      const size = w.size || 1
      const text = w.text ?? ''
      const tw = text.length * 6 * size
      ctx.fillStyle = color
      ctx.font = `${8 * size}px monospace`
      ctx.fillText('-', (w.x || 0) + 3, (w.y || 0) + bh / 2 - 4 * size)
      ctx.fillText('+', (w.x || 0) + bw - 8, (w.y || 0) + bh / 2 - 4 * size)
      ctx.fillText(text, (w.x || 0) + Math.max(0, (bw - tw) / 2), (w.y || 0) + bh / 2 - 4 * size)
    } else if (type === 'sparkline') {
      // No history client-side — draw a flat baseline at the current value.
      const bw = w.w || 40, bh = w.h || 16
      const span = (w.max ?? 100) - (w.min ?? 0)
      let frac = span ? ((w.val ?? w.min ?? 0) - (w.min ?? 0)) / span : 0.5
      frac = Math.max(0, Math.min(1, frac))
      const y = (w.y || 0) + bh - 1 - frac * (bh - 1)
      ctx.strokeStyle = color
      ctx.lineWidth = 1
      ctx.beginPath()
      ctx.moveTo(w.x || 0, y)
      ctx.lineTo((w.x || 0) + bw, y)
      ctx.stroke()
    } else {
      // text / value / datetime — already resolved to a string on the device.
      const size = w.size || 1
      ctx.fillStyle = color
      ctx.font = `${8 * size}px monospace`
      let x = w.x || 0
      const text = w.text ?? ''
      if (w.align) {
        const tw = text.length * 6 * size
        x = w.align === 1 ? x - tw / 2 : x - tw
      }
      ctx.fillText(text, x, w.y || 0)
    }
  }

  // Notification overlay renders bottom-left at size 1 on the device.
  if (props.screen.notification) {
    ctx.fillStyle = isColor.value ? '#fff' : ON
    ctx.font = '8px monospace'
    ctx.fillText(props.screen.notification, 0, c.height - 8)
  }
}

function drawRect(ctx, x, y, w, h, color, filled) {
  if (filled) {
    ctx.fillStyle = color
    ctx.fillRect(x, y, w, h)
  } else {
    ctx.strokeStyle = color
    ctx.lineWidth = 1
    ctx.strokeRect(x + 0.5, y + 0.5, w - 1, h - 1)
  }
}
function drawCircle(ctx, x, y, r, color, filled) {
  ctx.beginPath()
  ctx.arc(x + r, y + r, r, 0, Math.PI * 2)
  if (filled) { ctx.fillStyle = color; ctx.fill() }
  else { ctx.strokeStyle = color; ctx.lineWidth = 1; ctx.stroke() }
}
function drawGauge(ctx, w, color) {
  const r = w.w || 20
  const cx = (w.x || 0) + r, cy = (w.y || 0) + r
  const span = (w.max ?? 100) - (w.min ?? 0)
  let frac = span ? ((w.val ?? w.min ?? 0) - (w.min ?? 0)) / span : 0
  frac = Math.max(0, Math.min(1, frac))
  const start = (135 * Math.PI) / 180
  const full = start + (270 * Math.PI) / 180
  const val = start + ((270 * frac) * Math.PI) / 180
  ctx.lineWidth = 2
  // Track (dim) then value arc.
  ctx.strokeStyle = isColor.value ? 'rgba(255,255,255,0.25)' : '#333'
  ctx.beginPath(); ctx.arc(cx, cy, r - 1, start, full); ctx.stroke()
  ctx.strokeStyle = color
  ctx.beginPath(); ctx.arc(cx, cy, r - 1, start, val); ctx.stroke()
  // Centre value.
  ctx.fillStyle = color
  ctx.font = '8px monospace'
  const txt = String((w.val ?? 0).toFixed(0))
  ctx.fillText(txt, cx - txt.length * 3, cy - 4)
}
function drawBar(ctx, w, color) {
  const bw = w.w || 40
  const bh = w.h || 8
  const x = w.x || 0
  const y = w.y || 0
  drawRect(ctx, x, y, bw, bh, color, false)
  const span = (w.max ?? 100) - (w.min ?? 0)
  let frac = span ? ((w.val ?? w.min ?? 0) - (w.min ?? 0)) / span : 0
  frac = Math.max(0, Math.min(1, frac))
  if (bw >= bh) {
    const fw = Math.round((bw - 2) * frac)
    if (fw > 0) drawRect(ctx, x + 1, y + 1, fw, bh - 2, color, true)
  } else {
    const fh = Math.round((bh - 2) * frac)
    if (fh > 0) drawRect(ctx, x + 1, y + bh - 1 - fh, bw - 2, fh, color, true)
  }
}

watch(() => props.screen, draw, { deep: true })
onMounted(draw)
</script>

<style scoped>
.screen-preview { display: block; }
</style>
