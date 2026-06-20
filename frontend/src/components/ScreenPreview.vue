<template>
  <div>
    <div class="text-caption text-medium-emphasis mb-1">
      {{ screen.id }} · {{ screen.width }}×{{ screen.height }} · {{ screen.page || 'no page' }}
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

// Renders a monochrome OLED preview from the device's reported screen content
// (geometry + resolved widget text from /api/screens). It's an approximation, not
// a pixel-exact framebuffer: the SSD1306 default GFX font is ~6px wide × 8px tall
// per character at size 1, drawn from the top-left, which we mimic here.
const props = defineProps({
  screen: { type: Object, required: true },
  scale: { type: Number, default: 3 },
})

const canvas = ref(null)

const style = computed(() => ({
  width: `${props.screen.width * props.scale}px`,
  height: `${props.screen.height * props.scale}px`,
  imageRendering: 'pixelated',
  background: '#000',
  borderRadius: '4px',
  border: '1px solid rgba(255,255,255,0.15)',
}))

function draw() {
  const c = canvas.value
  if (!c) return
  const ctx = c.getContext('2d')
  ctx.fillStyle = '#000'
  ctx.fillRect(0, 0, c.width, c.height)
  ctx.fillStyle = '#fff'
  ctx.textBaseline = 'top'
  for (const w of props.screen.widgets || []) {
    const size = w.size || 1
    // Match the GFX glyph cell so text lands where it does on the panel.
    ctx.font = `${8 * size}px monospace`
    ctx.fillText(w.text ?? '', w.x || 0, w.y || 0)
  }
  // Notification overlay renders bottom-left at size 1 on the device.
  if (props.screen.notification) {
    ctx.font = '8px monospace'
    ctx.fillText(props.screen.notification, 0, c.height - 8)
  }
}

watch(() => props.screen, draw, { deep: true })
onMounted(draw)
</script>

<style scoped>
.screen-preview { display: block; }
</style>
