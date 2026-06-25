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
              :class="{ 'sd-widget--selected': selectedWidgetIdx === idx, 'sd-widget--shape': !isTextType(widget) && widget.type !== 'icon' }"
              :style="widgetStyle(widget)"
              tabindex="0"
              role="button"
              :aria-label="widgetAriaLabel(widget)"
              @pointerdown.stop="onWidgetPointerDown($event, idx)"
              @focus="selectedWidgetIdx = idx"
              @keydown="onWidgetKeydown($event, idx)"
            >
              <template v-if="isTextType(widget)">{{ widgetPreviewText(widget) }}</template>
              <v-icon v-else-if="widget.type === 'icon'" :icon="iconMdi(widget.text)" :size="(widget.w || 12) * scale" />
              <div v-else-if="widget.type === 'bar'" :style="barFillStyle(widget)" />
              <img v-else-if="widget.type === 'image' && imageUrls[widget.text]" :src="imageUrls[widget.text]" class="sd-img" alt="" />
              <div v-else-if="widget.type === 'image'" class="sd-img-ph"><v-icon size="small">mdi-image</v-icon></div>
              <svg v-else-if="widget.type === 'gauge'" class="sd-svg" :viewBox="`0 0 ${gaugeSize(widget)} ${gaugeSize(widget)}`">
                <path :d="gaugeArc(widget, 1)" :stroke="gaugeTrack(widget)" fill="none" stroke-width="2" stroke-linecap="round" />
                <path :d="gaugeArc(widget, 0.66)" :stroke="previewColor(widget.color)" fill="none" stroke-width="2" stroke-linecap="round" />
                <text :x="gaugeSize(widget) / 2" :y="gaugeSize(widget) / 2 + 2" text-anchor="middle" :fill="previewColor(widget.color)" font-size="6">{{ gaugeLabel(widget) }}</text>
              </svg>
              <svg v-else-if="widget.type === 'sparkline'" class="sd-svg" :viewBox="`0 0 ${widget.w || 60} ${widget.h || 20}`" preserveAspectRatio="none">
                <polyline :points="sparkSample(widget)" :stroke="previewColor(widget.color)" fill="none" stroke-width="1" vector-effect="non-scaling-stroke" />
              </svg>
            </div>
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
          <v-select
            v-model="selectedWidget.type"
            :items="typeItems"
            label="Type"
            density="compact"
            variant="outlined"
            hide-details
            class="mb-3"
          />

          <!-- Static text -->
          <v-text-field
            v-if="selectedWidget.type === 'text'"
            v-model="selectedWidget.text"
            label="Text" density="compact" variant="outlined" hide-details class="mb-3"
          />

          <!-- Date / time -->
          <template v-else-if="selectedWidget.type === 'datetime'">
            <v-text-field v-model="selectedWidget.text" label="Format (strftime)" placeholder="%H:%M:%S"
              density="compact" variant="outlined" hide-details class="mb-1" />
            <p class="text-caption text-medium-emphasis mb-3">e.g. <code>%H:%M</code>, <code>%d/%m</code>, <code>%a %H:%M</code></p>
          </template>

          <!-- Variable-bound (value / bar / led) -->
          <template v-else-if="['value', 'bar', 'led', 'gauge', 'sparkline'].includes(selectedWidget.type)">
            <v-combobox v-model="selectedWidget.variableId" :items="variableIds" label="Variable"
              density="compact" variant="outlined" hide-details class="mb-3" />
            <template v-if="['value', 'gauge'].includes(selectedWidget.type)">
              <div v-if="selectedWidget.type === 'value'" class="d-flex ga-2 mb-3">
                <v-text-field v-model="selectedWidget.prefix" label="Prefix" density="compact" variant="outlined" hide-details />
                <v-text-field v-model="selectedWidget.suffix" label="Suffix" density="compact" variant="outlined" hide-details />
              </div>
              <v-text-field v-model.number="selectedWidget.decimals" label="Decimals" type="number" min="0" max="6"
                density="compact" variant="outlined" hide-details class="mb-3" />
            </template>
            <div v-if="['bar', 'led', 'gauge'].includes(selectedWidget.type)" class="d-flex ga-2 mb-3">
              <v-text-field v-model.number="selectedWidget.min" label="Min" type="number" density="compact" variant="outlined" hide-details />
              <v-text-field v-model.number="selectedWidget.max" label="Max" type="number" density="compact" variant="outlined" hide-details />
            </div>
            <p v-if="selectedWidget.type === 'sparkline'" class="text-caption text-medium-emphasis mb-3">
              Auto-scales to recent history (1 sample/sec, last {{ selectedWidget.w || 60 }} s).
            </p>
          </template>

          <!-- Icon picker -->
          <v-select
            v-else-if="selectedWidget.type === 'icon'"
            v-model="selectedWidget.text"
            :items="ICONS"
            label="Icon"
            density="compact" variant="outlined" hide-details class="mb-3"
          >
            <template #selection="{ item }"><v-icon :icon="iconMdi(item.value)" class="mr-2" />{{ item.value }}</template>
            <template #item="{ item, props: p }"><v-list-item v-bind="p" :title="item.value" :prepend-icon="iconMdi(item.value)" /></template>
          </v-select>

          <!-- Image -->
          <template v-else-if="selectedWidget.type === 'image'">
            <v-btn block variant="tonal" color="deep-purple" prepend-icon="mdi-upload" :loading="uploadingImage" class="mb-2" @click="pickImageFile">
              {{ selectedWidget.text ? 'Replace image' : 'Upload image' }}
            </v-btn>
            <input ref="imageFileInput" type="file" accept="image/*" hidden @change="onImageFileChosen" />
            <v-select v-if="imageItems.length" v-model="selectedWidget.text" :items="imageItems" label="Image file"
              density="compact" variant="outlined" hide-details clearable class="mb-2" />
            <v-btn block size="small" variant="text" prepend-icon="mdi-fit-to-page-outline" class="mb-1" @click="fitImageToScreen">
              Fit to screen (background)
            </v-btn>
            <p class="text-caption text-medium-emphasis mb-2">
              Converted to {{ isColorDisplay ? 'colour (RGB565)' : '1-bit mono' }} at the W×H below, on upload.
            </p>
          </template>

          <!-- Position -->
          <div class="d-flex ga-2 mb-3">
            <v-text-field v-model.number="selectedWidget.x" label="X" type="number" density="compact" variant="outlined" hide-details @update:model-value="constrainSelected" />
            <v-text-field v-model.number="selectedWidget.y" label="Y" type="number" density="compact" variant="outlined" hide-details @update:model-value="constrainSelected" />
          </div>

          <!-- Size: text types get font size; shaped types get W/H -->
          <div v-if="isTextType(selectedWidget)" class="d-flex ga-2 mb-3">
            <v-select v-model.number="selectedWidget.textSize" :items="[1,2,3,4,5,6,7,8]" label="Font size" density="compact" variant="outlined" hide-details />
            <v-select v-model.number="selectedWidget.align" :items="ALIGN_ITEMS" label="Align" density="compact" variant="outlined" hide-details />
          </div>
          <div v-else-if="['circle', 'led', 'icon', 'gauge'].includes(selectedWidget.type)" class="mb-3">
            <v-text-field v-model.number="selectedWidget.w" :label="selectedWidget.type === 'icon' ? 'Icon size (px)' : 'Radius (px)'"
              type="number" min="1" density="compact" variant="outlined" hide-details />
          </div>
          <div v-else class="d-flex ga-2 mb-3">
            <v-text-field v-model.number="selectedWidget.w" :label="selectedWidget.type === 'line' ? 'ΔX' : 'Width'" type="number" density="compact" variant="outlined" hide-details />
            <v-text-field v-model.number="selectedWidget.h" :label="selectedWidget.type === 'line' ? 'ΔY' : 'Height'" type="number" density="compact" variant="outlined" hide-details />
          </div>

          <!-- Filled toggle for rect/circle -->
          <v-switch
            v-if="selectedWidget.type === 'rect' || selectedWidget.type === 'circle'"
            v-model="selectedWidget.filled" label="Filled" density="compact" color="primary" hide-details inset class="mb-2"
          />

          <!-- Max chars for text types -->
          <v-text-field
            v-if="isTextType(selectedWidget)"
            v-model.number="selectedWidget.maxChars" label="Max chars (0 = no limit)" type="number" min="0"
            density="compact" variant="outlined" hide-details class="mb-3"
          />

          <!-- Colour -->
          <div class="sd-color-row mb-2">
            <label class="sd-color-label">Colour</label>
            <input type="color" v-model="selectedWidget.color" class="sd-color-input" aria-label="Widget colour" />
          </div>
          <div class="sd-color-row mb-1">
            <v-switch :model-value="selectedWidget.bg != null" density="compact" color="primary" hide-details inset
              label="Background" @update:model-value="toggleWidgetBg" />
            <input v-if="selectedWidget.bg != null" type="color" v-model="selectedWidget.bg" class="sd-color-input" aria-label="Background colour" />
          </div>
          <p v-if="!isColorDisplay" class="text-caption text-medium-emphasis mt-1 mb-0">
            Monochrome panel — any non-black colour shows as “on”.
          </p>
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
          <div class="sd-color-row mb-3">
            <v-switch :model-value="selectedPage.bg != null" density="compact" color="primary" hide-details inset
              label="Background colour" @update:model-value="togglePageBg" />
            <input v-if="selectedPage.bg != null" type="color" v-model="selectedPage.bg" class="sd-color-input" aria-label="Page background colour" />
          </div>
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
import { fileToOfim } from '../lib/imageConvert'

// Widget catalogue — shared by the content library (drag source) and the inspector
// type selector. Each renders on both OLED (monochrome: colour maps to on/off) and
// colour TFT panels.
const WIDGET_TYPES = [
  { type: 'text',     label: 'Static text',    hint: 'A fixed label',      icon: 'mdi-format-text',       color: 'primary' },
  { type: 'value',    label: 'Variable value', hint: 'A live reading',     icon: 'mdi-variable',          color: 'teal' },
  { type: 'datetime', label: 'Date / time',    hint: 'A live clock',       icon: 'mdi-clock-outline',     color: 'indigo' },
  { type: 'bar',      label: 'Progress bar',   hint: 'A variable level',   icon: 'mdi-chart-bar',         color: 'green' },
  { type: 'led',      label: 'Indicator',      hint: 'On/off status dot',  icon: 'mdi-led-on',            color: 'amber' },
  { type: 'icon',     label: 'Icon',           hint: 'A built-in glyph',   icon: 'mdi-star-circle',       color: 'pink' },
  { type: 'rect',     label: 'Rectangle',      hint: 'Filled or outline',  icon: 'mdi-rectangle-outline', color: 'blue-grey' },
  { type: 'line',     label: 'Line',           hint: 'A divider',          icon: 'mdi-minus',             color: 'blue-grey' },
  { type: 'circle',   label: 'Circle',         hint: 'Filled or outline',  icon: 'mdi-circle-outline',    color: 'blue-grey' },
  { type: 'image',    label: 'Image',          hint: 'Upload a picture',   icon: 'mdi-image',             color: 'deep-purple' },
  { type: 'gauge',    label: 'Gauge',          hint: 'Arc dial of a value', icon: 'mdi-gauge',            color: 'cyan' },
  { type: 'sparkline', label: 'Sparkline',     hint: 'History mini-chart', icon: 'mdi-chart-line-variant', color: 'light-blue' },
]
const LIBRARY = WIDGET_TYPES

// Built-in icon set (matches the firmware renderIcon names) → an MDI glyph for the
// editor preview. The device draws these from primitives, so they render on OLED too.
const ICONS = ['dot', 'circle', 'check', 'cross', 'arrow_up', 'arrow_down', 'battery', 'thermometer', 'heart', 'wifi']
const ICON_MDI = {
  dot: 'mdi-circle', circle: 'mdi-circle-outline', check: 'mdi-check-bold', cross: 'mdi-close-thick',
  arrow_up: 'mdi-arrow-up-bold', arrow_down: 'mdi-arrow-down-bold', battery: 'mdi-battery',
  thermometer: 'mdi-thermometer', heart: 'mdi-heart', wifi: 'mdi-wifi',
}
const ALIGN_ITEMS = [
  { title: 'Left', value: 0 }, { title: 'Center', value: 1 }, { title: 'Right', value: 2 },
]
const TEXT_TYPES = ['text', 'value', 'datetime']
const isTextType = (w) => TEXT_TYPES.includes(w?.type)
const iconMdi = (name) => ICON_MDI[name] || 'mdi-help-box'

const loading = ref(false)
const saving = ref(false)
const statusMessage = ref(null)
const pages = ref([])
const selectedPage = ref(null)
const selectedWidgetIdx = ref(null)
const displayList = ref([])
const variableIds = ref([])
const imagesList = ref([])          // [{ name, size }] from the device
const imageUrls = ref({})           // name → object URL (originals, for the editor preview)
const uploadingImage = ref(false)
const imageFileInput = ref(null)

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
const typeItems = WIDGET_TYPES.map((t) => ({ title: t.label, value: t.type }))

// Background colour is optional per widget/page — toggling it on seeds black, off
// removes the field entirely (so the device treats it as "no background").
function toggleWidgetBg(on) {
  if (!selectedWidget.value) return
  if (on) selectedWidget.value.bg = '#000000'
  else delete selectedWidget.value.bg
}
function togglePageBg(on) {
  if (!selectedPage.value) return
  if (on) selectedPage.value.bg = '#000000'
  else delete selectedPage.value.bg
}

const imageItems = computed(() => imagesList.value.map((i) => i.name))

// Upload an image for the selected image widget: convert it to OFIM (RGB565 for
// colour panels, 1-bit for mono) at the widget's render size — defaulting to the
// full screen for a background — then store it on the device under a derived name.
function pickImageFile() {
  imageFileInput.value?.click()
}
async function onImageFileChosen(event) {
  const file = event.target.files?.[0]
  event.target.value = ''  // allow re-selecting the same file
  if (!file || !selectedWidget.value) return
  const wdg = selectedWidget.value
  const tw = wdg.w > 0 ? wdg.w : activeDisplay.value.width
  const th = wdg.h > 0 ? wdg.h : activeDisplay.value.height
  const base = (file.name.replace(/\.[^.]+$/, '') || 'image').replace(/[^A-Za-z0-9._-]/g, '').slice(0, 40) || 'image'
  const name = `${base}.ofi`
  uploadingImage.value = true
  statusMessage.value = null
  try {
    const { bytes, width, height } = await fileToOfim(file, tw, th, isColorDisplay.value)
    await api.images.upload(name, bytes)
    wdg.text = name
    wdg.w = width
    wdg.h = height
    imageUrls.value[name] = URL.createObjectURL(file)
    if (!imagesList.value.some((i) => i.name === name)) imagesList.value.push({ name, size: bytes.length })
    statusMessage.value = { type: 'success', text: `Uploaded “${name}” (${width}×${height}).` }
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'Image upload failed' }
  } finally {
    uploadingImage.value = false
  }
}
function fitImageToScreen() {
  if (!selectedWidget.value) return
  selectedWidget.value.x = 0
  selectedWidget.value.y = 0
  selectedWidget.value.w = activeDisplay.value.width
  selectedWidget.value.h = activeDisplay.value.height
}

const selectedWidget = computed(() =>
  selectedWidgetIdx.value != null ? selectedPage.value?.widgets[selectedWidgetIdx.value] : null,
)

// Colour panels (TFT) get full colour; monochrome panels (OLED) map any non-black
// colour to "on". Used to tint the preview and hint in the inspector.
const COLOR_TYPES = ['st7789', 'ili9341', 'ili9342']
const isColorDisplay = computed(() => COLOR_TYPES.includes((activeDisplay.value.type || '').toLowerCase()))

// On a monochrome panel everything renders as the "on" colour; mimic that in the
// preview so the designer matches the glass.
function previewColor(c) {
  if (isColorDisplay.value) return c || '#FFFFFF'
  return (c && c.toLowerCase() !== '#000000') ? '#cfe8ff' : '#070b10'
}

const panelStyle = computed(() => ({
  width: `${activeDisplay.value.width * scale.value}px`,
  height: `${activeDisplay.value.height * scale.value}px`,
  backgroundSize: `${gridStep.value * scale.value}px ${gridStep.value * scale.value}px`,
}))

// Position + per-type appearance for a widget in the editor panel. Shapes get an
// explicit box; text/icon are content-sized. Colours pass through previewColor() so
// monochrome panels render in the OLED "on" tint.
function widgetStyle(widget) {
  const s = scale.value
  const base = { left: `${(widget.x || 0) * s}px`, top: `${(widget.y || 0) * s}px` }
  const col = previewColor(widget.color)
  switch (widget.type) {
    case 'rect':
      return { ...base, width: `${(widget.w || 1) * s}px`, height: `${(widget.h || 1) * s}px`,
        background: widget.filled ? col : 'transparent', border: `${Math.max(1, s)}px solid ${col}` }
    case 'circle': {
      const d = (widget.w || 6) * 2 * s
      return { ...base, width: `${d}px`, height: `${d}px`, borderRadius: '50%',
        background: widget.filled ? col : 'transparent', border: `${Math.max(1, s)}px solid ${col}` }
    }
    case 'led': {
      const d = (widget.w || 6) * 2 * s
      return { ...base, width: `${d}px`, height: `${d}px`, borderRadius: '50%', background: col }
    }
    case 'line': {
      const w = Math.abs(widget.w || 0) * s
      const h = Math.abs(widget.h || 0) * s
      return { ...base, width: `${Math.max(w, 1)}px`, height: `${Math.max(h, 1)}px`,
        background: `linear-gradient(to bottom right, transparent calc(50% - 1px), ${col} calc(50% - 1px), ${col} calc(50% + 1px), transparent calc(50% + 1px))` }
    }
    case 'bar':
      return { ...base, width: `${(widget.w || 40) * s}px`, height: `${(widget.h || 8) * s}px`,
        border: `${Math.max(1, s)}px solid ${col}`, background: 'transparent' }
    case 'image':
      return { ...base, width: `${(widget.w || 16) * s}px`, height: `${(widget.h || 16) * s}px`,
        border: imageUrls.value[widget.text] ? 'none' : `1px dashed ${col}` }
    case 'gauge': {
      const d = (widget.w || 24) * 2 * s
      return { ...base, width: `${d}px`, height: `${d}px` }
    }
    case 'sparkline':
      return { ...base, width: `${(widget.w || 60) * s}px`, height: `${(widget.h || 20) * s}px` }
    case 'icon':
      return { ...base, color: col, fontSize: `${(widget.w || 12) * s}px`, lineHeight: 1 }
    default: {  // text / value / datetime
      const size = Math.max(1, widget.textSize || 1)
      return { ...base, color: col,
        fontSize: `${GFX_CHAR_H * size * s}px`,
        letterSpacing: `${(GFX_CHAR_W - GFX_CHAR_H * 0.6) * size * s}px`,
        transform: widget.align === 1 ? 'translateX(-50%)' : widget.align === 2 ? 'translateX(-100%)' : 'none' }
    }
  }
}

// Inner fill of a bar widget (variable level → coloured fill over the track).
function barFillStyle(widget) {
  const frac = Math.max(0, Math.min(1, ((widget._val ?? widget.min) - widget.min) / ((widget.max - widget.min) || 1)))
  const col = previewColor(widget.color)
  const horizontal = (widget.w || 40) >= (widget.h || 8)
  return horizontal
    ? { position: 'absolute', left: 0, bottom: 0, top: 0, width: `${frac * 100}%`, background: col }
    : { position: 'absolute', left: 0, right: 0, bottom: 0, height: `${frac * 100}%`, background: col }
}

// ── Gauge / sparkline editor previews ────────────────────────────────────────
// The editor has no live value, so the gauge shows a representative 66% fill and the
// sparkline a stylised sample wave — enough to read the widget's shape and colour.
function gaugeSize(widget) { return (widget.w || 24) * 2 }
function gaugeTrack(widget) {
  return widget.bg != null ? previewColor(widget.bg) : 'rgba(207,232,255,0.18)'
}
// SVG arc path over a 270° sweep (gap at the bottom), matching the firmware.
function gaugeArc(widget, frac) {
  const size = gaugeSize(widget)
  const cx = size / 2, cy = size / 2, r = size / 2 - 2
  const start = 135, sweep = 270 * Math.max(0, Math.min(1, frac))
  const rad = (d) => (d * Math.PI) / 180
  const x0 = cx + r * Math.cos(rad(start)), y0 = cy + r * Math.sin(rad(start))
  const x1 = cx + r * Math.cos(rad(start + sweep)), y1 = cy + r * Math.sin(rad(start + sweep))
  const large = sweep > 180 ? 1 : 0
  return `M ${x0.toFixed(1)} ${y0.toFixed(1)} A ${r} ${r} 0 ${large} 1 ${x1.toFixed(1)} ${y1.toFixed(1)}`
}
function gaugeLabel(widget) {
  const min = widget.min ?? 0, max = widget.max ?? 100
  return String(Math.round(min + 0.66 * (max - min)))
}
function sparkSample(widget) {
  const w = widget.w || 60, h = widget.h || 20, n = 16
  let pts = ''
  for (let i = 0; i < n; i++) {
    const x = (i * (w - 1)) / (n - 1)
    const y = h - 1 - ((Math.sin(i / 2) * 0.5 + 0.5) * (h - 2) + 1)
    pts += `${x.toFixed(1)},${y.toFixed(1)} `
  }
  return pts.trim()
}

function widgetPreviewText(widget) {
  if (widget.type === 'text') return widget.text || 'Text'
  if (widget.type === 'datetime') return widget.text || '%H:%M:%S'
  const inner = widget.variableId ? `{${widget.variableId}}` : '{value}'
  return `${widget.prefix || ''}${inner}${widget.suffix || ''}`
}

// Fill missing fields on widgets loaded from the device so the inspector binds
// cleanly (older pages have only text/value fields). bg stays absent unless set.
function normalizeWidget(w) {
  const out = {
    id: w.id || '', type: w.type || 'text',
    x: w.x || 0, y: w.y || 0, w: w.w || 0, h: w.h || 0,
    textSize: w.textSize || 1, align: w.align || 0,
    text: w.text || '', variableId: w.variableId || '',
    prefix: w.prefix || '', suffix: w.suffix || '',
    decimals: w.decimals ?? 1, maxChars: w.maxChars || 0,
    color: w.color || '#FFFFFF', filled: !!w.filled,
    min: w.min ?? 0, max: w.max ?? 100,
  }
  if (w.bg != null) out.bg = w.bg
  return out
}

function newWidget(type, x, y) {
  const n = (selectedPage.value?.widgets.length || 0) + 1
  const w = {
    id: `w${n}`, type, x, y, textSize: 1, align: 0,
    text: '', variableId: '', prefix: '', suffix: '', decimals: 1, maxChars: 0,
    color: '#FFFFFF', w: 0, h: 0, filled: false, min: 0, max: 100,
  }
  switch (type) {
    case 'text':     w.text = 'Text'; break
    case 'datetime': w.text = '%H:%M:%S'; break
    case 'rect':     w.w = 40; w.h = 20; break
    case 'line':     w.w = 40; w.h = 0; break
    case 'circle':   w.w = 10; break
    case 'bar':      w.w = 60; w.h = 10; w.color = '#33CC66'; break
    case 'led':      w.w = 6;  w.color = '#33CC66'; break
    case 'icon':     w.text = 'dot'; w.w = 12; break
    case 'image':    w.text = ''; w.w = 0; w.h = 0; break
    case 'gauge':    w.w = 24; w.color = '#22CCDD'; break
    case 'sparkline': w.w = 60; w.h = 20; w.color = '#44AAFF'; break
    default: break
  }
  return w
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
  const what = widget.type === 'text' ? `text "${widget.text}"`
    : isTextType(widget) ? `${widget.type} ${widget.variableId || ''}`.trim()
    : `${widget.type} widget`
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
    const [dispData, pageData, varData, imgData] = await Promise.all([
      api.get('/api/displays').catch(() => ({ displays: [] })),
      api.get('/api/displays/pages').catch(() => ({ pages: [] })),
      api.get('/api/variables').catch(() => ({ variables: [] })),
      api.images.list().catch(() => ({ images: [] })),
    ])
    displayList.value = dispData.displays || []
    imagesList.value = imgData.images || []
    // /api/variables returns variables as an id-keyed object (matching the live
    // store and variable_change frames), not an array — normalize either shape.
    const varList = Array.isArray(varData.variables)
      ? varData.variables
      : Object.values(varData.variables || {})
    variableIds.value = varList.map((v) => v.id).filter(Boolean)
    pages.value = (pageData.pages || []).map((p) => ({
      ...p,
      widgets: (p.widgets || []).map(normalizeWidget),
    }))
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
/* Shape widgets (rect/line/circle/bar/led) are boxes, not glowing text. */
.sd-widget--shape {
  box-sizing: border-box;
  text-shadow: none;
  overflow: hidden;
}
.sd-widget--selected {
  outline: 1px dashed rgba(96, 165, 250, 0.9);
  outline-offset: 2px;
}

/* Image widget preview. */
.sd-img { width: 100%; height: 100%; object-fit: fill; image-rendering: pixelated; display: block; }
.sd-img-ph {
  width: 100%; height: 100%;
  display: flex; align-items: center; justify-content: center;
  color: rgba(207, 232, 255, 0.5);
}
.sd-svg { width: 100%; height: 100%; display: block; overflow: visible; }

/* Colour controls in the inspector. */
.sd-color-row { display: flex; align-items: center; gap: 0.6rem; }
.sd-color-label { font-size: 0.85rem; opacity: 0.85; flex: 1 1 auto; }
.sd-color-input {
  inline-size: 38px;
  block-size: 28px;
  border: 1px solid rgba(var(--v-border-color), var(--v-border-opacity));
  border-radius: 6px;
  background: transparent;
  cursor: pointer;
  padding: 2px;
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
