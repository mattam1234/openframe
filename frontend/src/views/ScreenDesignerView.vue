<template>
  <div class="screen-designer">
    <!-- Toolbar -->
    <div class="sd-toolbar mb-4">
      <div class="sd-title">
        <v-icon size="28" class="mr-2">mdi-monitor-edit</v-icon>
        <div>
          <h1 class="text-h5 mb-0">Screen Designer</h1>
          <div class="text-caption text-medium-emphasis">
            Drag widgets onto the panel, position them, and save to the device
          </div>
        </div>
      </div>
      <div class="sd-actions">
        <v-btn
          prepend-icon="mdi-monitor-star"
          variant="tonal"
          :disabled="!displayList.length"
          @click="openNewPage"
        >
          New page
        </v-btn>
        <v-btn icon="mdi-refresh" variant="text" :loading="loading" aria-label="Reload" @click="refresh" />
      </div>
    </div>

    <v-alert
      v-if="statusMessage"
      :type="statusMessage.type"
      variant="tonal"
      class="mb-4"
      closable
      @click:close="statusMessage = null"
    >
      {{ statusMessage.text }}
    </v-alert>

    <!-- Page tabs -->
    <div v-if="pages.length" class="sd-pagebar mb-3">
      <v-chip
        v-for="page in pages"
        :key="page.id"
        :color="selectedPage && selectedPage.id === page.id ? 'primary' : undefined"
        :variant="selectedPage && selectedPage.id === page.id ? 'flat' : 'tonal'"
        size="small"
        label
        @click="selectPage(page)"
      >
        <v-icon start size="14">mdi-television</v-icon>
        {{ page.title || page.id }}
      </v-chip>
    </div>

    <div v-if="selectedPage" class="sd-editor">
      <!-- Content library -->
      <aside class="sd-library">
        <div class="sd-rail-head">Content library</div>
        <p class="text-caption text-medium-emphasis mb-3">Drag onto the screen</p>
        <div
          v-for="item in LIBRARY"
          :key="item.type"
          class="sd-lib-item"
          role="button"
          tabindex="0"
          :aria-label="`Add ${item.label} widget`"
          draggable="true"
          @dragstart="onLibraryDragStart($event, item.type)"
          @keydown.enter.prevent="addFromLibrary(item.type)"
          @keydown.space.prevent="addFromLibrary(item.type)"
        >
          <v-icon class="mr-2" :color="item.color">{{ item.icon }}</v-icon>
          <div class="sd-lib-text">
            <div class="sd-lib-label">{{ item.label }}</div>
            <div class="sd-lib-hint">{{ item.hint }}</div>
          </div>
          <v-icon size="16" class="sd-lib-grip">mdi-drag</v-icon>
        </div>
        <v-divider class="my-4" />
        <div class="sd-rail-head">Snap</div>
        <v-btn-toggle v-model="gridStep" mandatory density="comfortable" divided class="sd-snap">
          <v-btn :value="1" size="small">1px</v-btn>
          <v-btn :value="4" size="small">4px</v-btn>
          <v-btn :value="8" size="small">8px</v-btn>
        </v-btn-toggle>
      </aside>

      <!-- Canvas -->
      <section class="sd-stage">
        <div class="sd-stage-bar">
          <span class="text-caption text-medium-emphasis">
            {{ activeDisplay.width }}×{{ activeDisplay.height }} · {{ displayLabel }}
          </span>
          <v-spacer />
          <v-btn icon="mdi-magnify-minus-outline" size="x-small" variant="text" aria-label="Zoom out" @click="zoom(-1)" />
          <span class="text-caption mx-1">{{ scale }}×</span>
          <v-btn icon="mdi-magnify-plus-outline" size="x-small" variant="text" aria-label="Zoom in" @click="zoom(1)" />
        </div>

        <div class="sd-bezel">
          <!-- The panel is a drag-and-drop canvas (a drop target), not a control;
               keyboard users add via the library tiles and nudge with arrow keys. -->
          <!-- eslint-disable-next-line vuejs-accessibility/no-static-element-interactions -->
          <div
            ref="canvasEl"
            class="sd-panel"
            :class="{ 'sd-grid': gridStep > 1 }"
            :style="panelStyle"
            role="group"
            :aria-label="`Device screen, ${activeDisplay.width} by ${activeDisplay.height} pixels`"
            @dragover.prevent
            @drop="onCanvasDrop"
            @pointerdown="onCanvasPointerDown"
          >
            <div
              v-for="(widget, idx) in selectedPage.widgets"
              :key="idx"
              ref="widgetEls"
              class="sd-widget"
              :class="{ 'sd-widget--selected': selectedWidgetIdx === idx }"
              :style="widgetStyle(widget)"
              tabindex="0"
              role="button"
              :aria-label="widgetAriaLabel(widget)"
              @pointerdown.stop="onWidgetPointerDown($event, idx)"
              @focus="selectedWidgetIdx = idx"
              @keydown="onWidgetKeydown($event, idx)"
            >{{ widgetPreviewText(widget) }}</div>
          </div>
        </div>
        <p class="sd-hint text-caption text-medium-emphasis">
          Drag widgets, or focus one and nudge with the arrow keys (Delete removes it).
          Saving restarts the device to apply the layout.
        </p>
      </section>

      <!-- Inspector -->
      <aside class="sd-inspector">
        <!-- Widget properties -->
        <div v-if="selectedWidget" class="sd-inspect-block">
          <div class="sd-rail-head sd-rail-head--row">
            <span>Widget</span>
            <v-btn
              icon="mdi-delete-outline"
              size="x-small"
              variant="text"
              color="error"
              aria-label="Delete widget"
              @click="removeWidget(selectedWidgetIdx)"
            />
          </div>
          <v-btn-toggle v-model="selectedWidget.type" mandatory divided density="comfortable" class="mb-3 w-100">
            <v-btn value="text" size="small" class="flex-1-1"><v-icon start size="16">mdi-format-text</v-icon>Text</v-btn>
            <v-btn value="value" size="small" class="flex-1-1"><v-icon start size="16">mdi-variable</v-icon>Value</v-btn>
          </v-btn-toggle>

          <v-text-field
            v-if="selectedWidget.type === 'text'"
            v-model="selectedWidget.text"
            label="Text"
            density="compact"
            variant="outlined"
            hide-details
            class="mb-3"
          />
          <template v-else>
            <v-combobox
              v-model="selectedWidget.variableId"
              :items="variableIds"
              label="Variable"
              density="compact"
              variant="outlined"
              hide-details
              class="mb-3"
            />
            <div class="d-flex ga-2 mb-3">
              <v-text-field v-model="selectedWidget.prefix" label="Prefix" density="compact" variant="outlined" hide-details />
              <v-text-field v-model="selectedWidget.suffix" label="Suffix" density="compact" variant="outlined" hide-details />
            </div>
            <v-text-field
              v-model.number="selectedWidget.decimals"
              label="Decimals"
              type="number"
              min="0"
              max="6"
              density="compact"
              variant="outlined"
              hide-details
              class="mb-3"
            />
          </template>

          <div class="d-flex ga-2 mb-3">
            <v-text-field v-model.number="selectedWidget.x" label="X" type="number" density="compact" variant="outlined" hide-details @update:model-value="constrainSelected" />
            <v-text-field v-model.number="selectedWidget.y" label="Y" type="number" density="compact" variant="outlined" hide-details @update:model-value="constrainSelected" />
          </div>
          <div class="d-flex ga-2">
            <v-select v-model.number="selectedWidget.textSize" :items="[1,2,3,4]" label="Size" density="compact" variant="outlined" hide-details />
            <v-text-field v-model.number="selectedWidget.maxChars" label="Max chars" type="number" min="0" density="compact" variant="outlined" hide-details />
          </div>
          <p class="text-caption text-medium-emphasis mt-2 mb-0">0 max chars = no limit.</p>
        </div>

        <v-divider v-if="selectedWidget" class="my-4" />

        <!-- Page properties -->
        <div class="sd-inspect-block">
          <div class="sd-rail-head">Page</div>
          <v-text-field v-model="selectedPage.id" label="Page ID" density="compact" variant="outlined" hide-details class="mb-3" />
          <v-text-field v-model="selectedPage.title" label="Title" density="compact" variant="outlined" hide-details class="mb-3" />
          <v-select
            v-model="selectedPage.displayId"
            :items="displayItems"
            label="Display"
            density="compact"
            variant="outlined"
            hide-details
            class="mb-3"
          />
          <v-btn block color="primary" :loading="saving" prepend-icon="mdi-content-save" @click="savePage">
            Save page
          </v-btn>
        </div>
      </aside>
    </div>

    <!-- Empty state -->
    <div v-else class="sd-empty">
      <v-icon size="64" class="mb-3 text-medium-emphasis">mdi-monitor-off</v-icon>
      <p class="text-h6 mb-1">No page selected</p>
      <p class="text-body-2 text-medium-emphasis mb-4" style="max-width:360px">
        {{ displayList.length
          ? 'Create a page to start dragging widgets onto a display.'
          : 'Add a display in the Layout Designer first, then design its screens here.' }}
      </p>
      <v-btn
        v-if="displayList.length"
        color="primary"
        prepend-icon="mdi-monitor-star"
        @click="openNewPage"
      >
        New page
      </v-btn>
      <v-btn v-else color="primary" prepend-icon="mdi-developer-board" to="/layout">
        Go to Layout Designer
      </v-btn>
    </div>
  </div>
</template>

<script setup>
import { computed, nextTick, onMounted, onUnmounted, ref } from 'vue'
import api from '../api/client'
import {
  GFX_CHAR_W, GFX_CHAR_H, fitScale, pointerToDevicePx, clampWidgetPos,
} from '../lib/screenEditor'

const LIBRARY = [
  { type: 'text', label: 'Static text', hint: 'A fixed label', icon: 'mdi-format-text', color: 'primary' },
  { type: 'value', label: 'Variable value', hint: 'A live reading', icon: 'mdi-variable', color: 'teal' },
]

const loading = ref(false)
const saving = ref(false)
const statusMessage = ref(null)
const pages = ref([])
const selectedPage = ref(null)
const selectedWidgetIdx = ref(null)
const displayList = ref([])
const variableIds = ref([])

const gridStep = ref(1)
const scale = ref(4)
const canvasEl = ref(null)
const widgetEls = ref([])

const activeDisplay = computed(() =>
  displayList.value.find((d) => d.id === selectedPage.value?.displayId) || { width: 128, height: 64 },
)
const displayLabel = computed(() => activeDisplay.value.id || selectedPage.value?.displayId || 'no display')
const displayItems = computed(() =>
  displayList.value.length
    ? displayList.value.map((d) => ({ title: `${d.id} (${d.type})`, value: d.id }))
    : [{ title: selectedPage.value?.displayId || 'display1', value: selectedPage.value?.displayId || 'display1' }],
)

const selectedWidget = computed(() =>
  selectedWidgetIdx.value != null ? selectedPage.value?.widgets[selectedWidgetIdx.value] : null,
)

const panelStyle = computed(() => ({
  width: `${activeDisplay.value.width * scale.value}px`,
  height: `${activeDisplay.value.height * scale.value}px`,
  backgroundSize: `${gridStep.value * scale.value}px ${gridStep.value * scale.value}px`,
}))

function widgetStyle(widget) {
  const size = Math.max(1, widget.textSize || 1)
  return {
    left: `${(widget.x || 0) * scale.value}px`,
    top: `${(widget.y || 0) * scale.value}px`,
    fontSize: `${GFX_CHAR_H * size * scale.value}px`,
    letterSpacing: `${(GFX_CHAR_W - GFX_CHAR_H * 0.6) * size * scale.value}px`,
  }
}

function widgetPreviewText(widget) {
  if (widget.type === 'text') return widget.text || 'Text'
  const inner = widget.variableId ? `{${widget.variableId}}` : '{value}'
  return `${widget.prefix || ''}${inner}${widget.suffix || ''}`
}

function newWidget(type, x, y) {
  const n = (selectedPage.value?.widgets.length || 0) + 1
  return {
    id: `w${n}`, type, x, y, textSize: 1,
    text: type === 'text' ? 'Text' : '', variableId: '',
    prefix: '', suffix: '', decimals: 1, maxChars: 0,
  }
}

// ── Drag from the library onto the panel ─────────────────────────────────────
function onLibraryDragStart(event, type) {
  event.dataTransfer.setData('text/openframe-widget', type)
  event.dataTransfer.effectAllowed = 'copy'
}

function onCanvasDrop(event) {
  const type = event.dataTransfer.getData('text/openframe-widget')
  if (!type) return
  const rect = canvasEl.value.getBoundingClientRect()
  const { x, y } = pointerToDevicePx(event.clientX, event.clientY, rect, scale.value, activeDisplay.value, gridStep.value)
  selectedPage.value.widgets.push(newWidget(type, x, y))
  selectedWidgetIdx.value = selectedPage.value.widgets.length - 1
}

// Keyboard alternative to dragging from the library: add at the origin, select
// it, and move focus to it so the arrow keys can position it.
function addFromLibrary(type) {
  selectedPage.value.widgets.push(newWidget(type, 0, 0))
  const idx = selectedPage.value.widgets.length - 1
  selectedWidgetIdx.value = idx
  nextTick(() => widgetEls.value[idx]?.focus())
}

function widgetAriaLabel(widget) {
  const what = widget.type === 'text' ? `text "${widget.text}"` : `value ${widget.variableId || ''}`.trim()
  return `${what} at ${widget.x}, ${widget.y}. Arrow keys move, Delete removes.`
}

// Arrow keys nudge the focused widget by the snap step; Delete/Backspace removes.
function onWidgetKeydown(event, idx) {
  const widget = selectedPage.value.widgets[idx]
  if (!widget) return
  const step = gridStep.value
  const moves = {
    ArrowLeft: [-step, 0], ArrowRight: [step, 0], ArrowUp: [0, -step], ArrowDown: [0, step],
  }
  if (moves[event.key]) {
    event.preventDefault()
    const [dx, dy] = moves[event.key]
    const { x, y } = clampWidgetPos((widget.x || 0) + dx, (widget.y || 0) + dy, activeDisplay.value)
    widget.x = x
    widget.y = y
  } else if (event.key === 'Delete' || event.key === 'Backspace') {
    event.preventDefault()
    removeWidget(idx)
  }
}

// ── Reposition an existing widget by dragging it ─────────────────────────────
let dragState = null
function onWidgetPointerDown(event, idx) {
  selectedWidgetIdx.value = idx
  const widget = selectedPage.value.widgets[idx]
  const rect = canvasEl.value.getBoundingClientRect()
  // Where inside the widget did we grab it (device px)? Keeps it under the cursor.
  const grab = {
    x: (event.clientX - rect.left) / scale.value - (widget.x || 0),
    y: (event.clientY - rect.top) / scale.value - (widget.y || 0),
  }
  dragState = { idx, grab }
  event.target.setPointerCapture?.(event.pointerId)
}

function onWidgetPointerMove(event) {
  if (!dragState) return
  const widget = selectedPage.value.widgets[dragState.idx]
  // Re-read the rect each frame — caching it at pointer-down makes the widget
  // drift if the page scrolls or reflows mid-drag.
  const rect = canvasEl.value.getBoundingClientRect()
  const { x, y } = pointerToDevicePx(
    event.clientX, event.clientY, rect, scale.value, activeDisplay.value, gridStep.value, dragState.grab,
  )
  widget.x = x
  widget.y = y
}

function onWidgetPointerUp() {
  dragState = null
}

// Clicking empty panel space deselects.
function onCanvasPointerDown(event) {
  if (event.target === canvasEl.value) selectedWidgetIdx.value = null
}

// Keep manually-typed X/Y on the panel.
function constrainSelected() {
  const w = selectedWidget.value
  if (!w) return
  const { x, y } = clampWidgetPos(w.x, w.y, activeDisplay.value)
  w.x = x
  w.y = y
}

function zoom(delta) {
  scale.value = Math.min(8, Math.max(2, scale.value + delta))
}

// ── Page CRUD ────────────────────────────────────────────────────────────────
function selectPage(page) {
  selectedPage.value = JSON.parse(JSON.stringify(page))
  selectedWidgetIdx.value = null
  scale.value = fitScale(activeDisplay.value.width)
}

function openNewPage() {
  const n = pages.value.length + 1
  const page = {
    id: `page${n}`,
    title: `Page ${n}`,
    displayId: displayList.value[0]?.id || 'display1',
    widgets: [],
    _new: true,
  }
  pages.value.push(page)
  selectPage(page)
}

function removeWidget(idx) {
  selectedPage.value.widgets.splice(idx, 1)
  selectedWidgetIdx.value = null
}

async function savePage() {
  if (!selectedPage.value) return
  if (!selectedPage.value.id) {
    statusMessage.value = { type: 'error', text: 'Give the page an ID before saving.' }
    return
  }
  saving.value = true
  statusMessage.value = null
  try {
    await api.post('/api/displays', { pages: [selectedPage.value] })
    const clone = JSON.parse(JSON.stringify(selectedPage.value))
    const existing = pages.value.findIndex((p) => p.id === selectedPage.value.id)
    if (existing >= 0) pages.value[existing] = clone
    else pages.value.push(clone)
    statusMessage.value = { type: 'success', text: `Saved “${selectedPage.value.title || selectedPage.value.id}”. The device is restarting to apply it.` }
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'Save failed' }
  } finally {
    saving.value = false
  }
}

async function refresh() {
  loading.value = true
  statusMessage.value = null
  try {
    const [dispData, pageData, varData] = await Promise.all([
      api.get('/api/displays').catch(() => ({ displays: [] })),
      api.get('/api/displays/pages').catch(() => ({ pages: [] })),
      api.get('/api/variables').catch(() => ({ variables: [] })),
    ])
    displayList.value = dispData.displays || []
    variableIds.value = (varData.variables || []).map((v) => v.id).filter(Boolean)
    pages.value = (pageData.pages || []).map((p) => ({ ...p, widgets: p.widgets || [] }))
    // Re-bind the open page to the freshly loaded copy (or clear if it's gone).
    if (selectedPage.value) {
      const match = pages.value.find((p) => p.id === selectedPage.value.id)
      if (match) selectPage(match)
      else selectedPage.value = null
    }
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'Failed to load' }
  } finally {
    loading.value = false
  }
}

onMounted(() => {
  refresh()
  window.addEventListener('pointermove', onWidgetPointerMove)
  window.addEventListener('pointerup', onWidgetPointerUp)
})

onUnmounted(() => {
  window.removeEventListener('pointermove', onWidgetPointerMove)
  window.removeEventListener('pointerup', onWidgetPointerUp)
})
</script>

<style scoped>
.sd-toolbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 1rem;
  flex-wrap: wrap;
}
.sd-title { display: flex; align-items: center; }
.sd-actions { display: flex; align-items: center; gap: 0.5rem; }

.sd-pagebar { display: flex; flex-wrap: wrap; gap: 0.4rem; }

/* Three-pane editor: library · stage · inspector */
.sd-editor {
  display: grid;
  grid-template-columns: 220px minmax(0, 1fr) 280px;
  gap: 1rem;
  align-items: start;
}

.sd-library,
.sd-inspector {
  background: rgba(var(--v-theme-surface), 1);
  border: 1px solid rgba(var(--v-border-color), var(--v-border-opacity));
  border-radius: 12px;
  padding: 1rem;
  position: sticky;
  top: 1rem;
}

.sd-rail-head {
  font-size: 0.7rem;
  font-weight: 700;
  letter-spacing: 0.08em;
  text-transform: uppercase;
  color: rgb(var(--v-theme-primary));
  margin-bottom: 0.25rem;
}
.sd-rail-head--row { display: flex; align-items: center; justify-content: space-between; }

/* Library items read like draggable component chips. */
.sd-lib-item {
  display: flex;
  align-items: center;
  gap: 0.25rem;
  padding: 0.5rem 0.6rem;
  margin-bottom: 0.5rem;
  border: 1px solid rgba(var(--v-border-color), var(--v-border-opacity));
  border-radius: 10px;
  cursor: grab;
  transition: border-color 0.15s, transform 0.05s, background 0.15s;
  background: rgba(var(--v-theme-on-surface), 0.02);
}
.sd-lib-item:hover {
  border-color: rgb(var(--v-theme-primary));
  background: rgba(var(--v-theme-primary), 0.06);
}
.sd-lib-item:active { cursor: grabbing; transform: scale(0.98); }
.sd-lib-text { flex: 1 1 auto; min-width: 0; }
.sd-lib-label { font-size: 0.85rem; font-weight: 600; line-height: 1.1; }
.sd-lib-hint { font-size: 0.7rem; opacity: 0.7; }
.sd-lib-grip { opacity: 0.4; }
.sd-snap { width: 100%; }
.sd-snap .v-btn { flex: 1 1 0; }

/* Stage with the device panel as the hero. */
.sd-stage { display: flex; flex-direction: column; align-items: center; }
.sd-stage-bar { display: flex; align-items: center; width: 100%; max-width: 640px; margin-bottom: 0.5rem; }

.sd-bezel {
  background: linear-gradient(160deg, #2a2f3a, #14171d);
  padding: 22px;
  border-radius: 18px;
  box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.08), 0 12px 30px rgba(0, 0, 0, 0.45);
}
.sd-panel {
  position: relative;
  background: #070b10;
  border-radius: 3px;
  overflow: hidden;
  box-shadow: 0 0 22px rgba(96, 165, 250, 0.18), inset 0 0 0 1px rgba(120, 160, 200, 0.12);
  touch-action: none;
  cursor: crosshair;
}
/* Faint pixel grid, shown when snapping to >1px. */
.sd-grid {
  background-image:
    linear-gradient(rgba(120, 160, 200, 0.12) 1px, transparent 1px),
    linear-gradient(90deg, rgba(120, 160, 200, 0.12) 1px, transparent 1px);
}

.sd-widget {
  position: absolute;
  color: #cfe8ff;
  font-family: 'Roboto Mono', 'Consolas', monospace;
  font-weight: 600;
  line-height: 1;
  white-space: nowrap;
  cursor: grab;
  user-select: none;
  text-shadow: 0 0 4px rgba(96, 165, 250, 0.5);
}
.sd-widget:active { cursor: grabbing; }
.sd-widget--selected {
  outline: 1px dashed rgba(96, 165, 250, 0.9);
  outline-offset: 2px;
}
.sd-widget:focus-visible,
.sd-lib-item:focus-visible {
  outline: 2px solid rgb(var(--v-theme-primary));
  outline-offset: 2px;
}
.sd-hint { margin-top: 0.75rem; }

.flex-1-1 { flex: 1 1 0; }

.sd-empty {
  display: flex;
  flex-direction: column;
  align-items: center;
  text-align: center;
  padding: 4rem 1rem;
}

@media (max-width: 960px) {
  .sd-editor { grid-template-columns: 1fr; }
  .sd-library, .sd-inspector { position: static; }
}
</style>
