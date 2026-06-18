/**
 * Geometry helpers for the Screen Designer's drag-and-drop canvas.
 *
 * The device renders display pages with the Adafruit-GFX default font: every
 * glyph is 6px wide × 8px tall at text size 1, anchored from its top-left. The
 * editor canvas draws the panel at an integer zoom (`scale`) so on-screen pixels
 * map cleanly back to device pixels. These pure functions do that mapping and the
 * bounds/snap math; the Vue component owns the DOM and pointer wiring.
 */

export const GFX_CHAR_W = 6
export const GFX_CHAR_H = 8

export function clamp(value, min, max) {
  if (!Number.isFinite(value)) return min
  return Math.min(max, Math.max(min, value))
}

/** Round to the nearest multiple of `step` (step ≤ 1 → whole pixels). */
export function snap(value, step = 1) {
  const s = step > 0 ? step : 1
  return Math.round(value / s) * s
}

/**
 * Clamp a widget's top-left so its origin stays on the panel — at least the
 * first glyph remains visible. Returns whole device pixels.
 */
export function clampWidgetPos(x, y, display) {
  const w = Math.max(1, Math.round(display?.width) || 128)
  const h = Math.max(1, Math.round(display?.height) || 64)
  return {
    x: clamp(Math.round(x), 0, w - 1),
    y: clamp(Math.round(y), 0, h - 1),
  }
}

/**
 * Convert a pointer position (client coordinates) over the scaled canvas into
 * on-panel device pixels — snapped to `step` and clamped to the display bounds.
 * `rect` is the canvas's getBoundingClientRect(); `grab` is the pointer's offset
 * from the widget origin in device px (0 for a fresh drop from the library).
 */
export function pointerToDevicePx(clientX, clientY, rect, scale, display, step = 1, grab = { x: 0, y: 0 }) {
  const s = scale > 0 ? scale : 1
  const px = snap((clientX - rect.left) / s - (grab.x || 0), step)
  const py = snap((clientY - rect.top) / s - (grab.y || 0), step)
  return clampWidgetPos(px, py, display)
}

/** Integer zoom that fits a display of `deviceWidth` px into ~`targetWidth` px. */
export function fitScale(deviceWidth, targetWidth = 512, min = 2, max = 8) {
  const w = Math.max(1, deviceWidth || 128)
  return clamp(Math.floor(targetWidth / w), min, max)
}
