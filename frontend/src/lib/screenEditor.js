/**
 * Pure helpers for the Screen Designer's drag-and-drop canvas.
 *
 * The device renders display pages with the Adafruit-GFX default font: every
 * glyph is 6px wide × 8px tall at text size 1, anchored from its top-left. The
 * editor canvas draws the panel at an integer zoom (`scale`) so on-screen pixels
 * map cleanly back to device pixels. These pure functions do that mapping, the
 * bounds/snap math, and small list/grouping logic (z-order, page ordering,
 * variable-picker groups); the Vue component owns the DOM and pointer wiring.
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

/**
 * Clamp a widget's size so it stays on the panel from its origin (x, y) —
 * snapped to `step`, then bounded to [min.w … panelW - x] × [min.h … panelH - y].
 * `min` defaults to 1×1; pass { w: 0, h: 0 } for line deltas, which may be zero.
 * Returns whole device pixels (the clamp may land off-step at the panel edge,
 * matching how position snapping behaves in pointerToDevicePx).
 */
export function clampWidgetSize(w, h, x, y, display, step = 1, min = { w: 1, h: 1 }) {
  const dw = Math.max(1, Math.round(display?.width) || 128)
  const dh = Math.max(1, Math.round(display?.height) || 64)
  const { x: ox, y: oy } = clampWidgetPos(Number(x) || 0, Number(y) || 0, display)
  const minW = Math.max(0, min?.w ?? 1)
  const minH = Math.max(0, min?.h ?? 1)
  return {
    w: clamp(snap(w, step), minW, dw - ox),
    h: clamp(snap(h, step), minH, dh - oy),
  }
}

/** Integer zoom that fits a display of `deviceWidth` px into ~`targetWidth` px. */
export function fitScale(deviceWidth, targetWidth = 512, min = 2, max = 8) {
  const w = Math.max(1, deviceWidth || 128)
  return clamp(Math.floor(targetWidth / w), min, max)
}

/**
 * Move list[from] to position `to`, in place (used for widget z-order and page
 * reordering — array order is z/rotation order). Returns true when it moved.
 */
export function moveItem(list, from, to) {
  if (!Array.isArray(list) || !Number.isInteger(from) || !Number.isInteger(to)) return false
  if (from === to || from < 0 || to < 0 || from >= list.length || to >= list.length) return false
  const [item] = list.splice(from, 1)
  list.splice(to, 0, item)
  return true
}

/**
 * Bucket a variable for the designer's grouped picker. Trusts the firmware's
 * `source` tag when it's specific, and falls back to id-prefix heuristics for
 * namespaces the firmware files under "local" (weather.*, node.*, node/*).
 */
export function variableGroupOf(id, source) {
  const known = ['sensor', 'input', 'output', 'weather', 'node']
  if (source && known.includes(source)) return source
  const vid = String(id || '')
  if (vid.startsWith('sensor.')) return 'sensor'
  if (vid.startsWith('input.')) return 'input'
  if (vid.startsWith('output.')) return 'output'
  if (vid.startsWith('weather.')) return 'weather'
  if (vid.startsWith('node.') || vid.startsWith('node/')) return 'node'
  return 'local'
}

/**
 * Order pages for the designer's page bar: grouped by display (in `displays`
 * order), then by that display's `page_order` array; pages missing from
 * page_order (or displays missing from the list) keep their relative order at
 * the end of their group. Returns a new array — the input is not mutated.
 */
export function sortPages(pages, displays) {
  const list = Array.isArray(pages) ? pages : []
  const disps = Array.isArray(displays) ? displays : []
  const dispIndex = new Map(disps.map((d, i) => [d.id, i]))
  // Per-display page order: display id -> Map(page id -> index). Nested maps
  // avoid composite string keys (both ids are user-chosen strings).
  const orderIndex = new Map()
  for (const d of disps) {
    const order = new Map()
    ;(Array.isArray(d.page_order) ? d.page_order : []).forEach((pid, i) => order.set(pid, i))
    orderIndex.set(d.id, order)
  }
  return list
    .map((p, i) => ({ p, i }))
    .sort((a, b) => {
      const da = dispIndex.get(a.p.displayId) ?? disps.length
      const db = dispIndex.get(b.p.displayId) ?? disps.length
      if (da !== db) return da - db
      const oa = orderIndex.get(a.p.displayId)?.get(a.p.id) ?? Number.MAX_SAFE_INTEGER
      const ob = orderIndex.get(b.p.displayId)?.get(b.p.id) ?? Number.MAX_SAFE_INTEGER
      if (oa !== ob) return oa - ob
      return a.i - b.i
    })
    .map((entry) => entry.p)
}
