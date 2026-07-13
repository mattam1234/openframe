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

    <!-- Page tabs (ordered per each display's page_order) -->
    <div v-if="pages.length" class="sd-pagebar mb-3">
      <v-chip
        v-for="page in orderedPages"
        :key="page.id"
        :color="selectedPage && selectedPage.id === page.id ? 'primary' : undefined"
        :variant="selectedPage && selectedPage.id === page.id ? 'flat' : 'tonal'"
        size="small"
        label
        @click="selectPage(page)"
      >
        <v-icon start size="14">mdi-television</v-icon>
        {{ page.title || page.id }}
        <!-- Reorder affordance on the active chip — writes the display's page_order. -->
        <template v-if="selectedPage && selectedPage.id === page.id">
          <v-btn
            icon="mdi-chevron-left" size="x-small" density="compact" variant="text" class="ml-1"
            :disabled="savingOrder || !canMovePage(-1)" aria-label="Move page earlier in rotation"
            @click.stop="movePage(-1)"
          />
          <v-btn
            icon="mdi-chevron-right" size="x-small" density="compact" variant="text"
            :disabled="savingOrder || !canMovePage(1)" aria-label="Move page later in rotation"
            @click.stop="movePage(1)"
          />
        </template>
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
          <v-btn icon="mdi-undo" size="x-small" variant="text" :disabled="!canUndo" aria-label="Undo (Ctrl+Z)" title="Undo (Ctrl+Z)" @click="undo" />
          <v-btn icon="mdi-redo" size="x-small" variant="text" :disabled="!canRedo" aria-label="Redo (Ctrl+Shift+Z)" title="Redo (Ctrl+Shift+Z)" @click="redo" />
          <v-divider vertical class="mx-1 my-1" />
          <v-btn icon="mdi-content-copy" size="x-small" variant="text" :disabled="!selectedWidget" aria-label="Copy widget (Ctrl+C)" title="Copy widget (Ctrl+C)" @click="copySelected" />
          <v-btn icon="mdi-import" size="x-small" variant="text" :disabled="!clipboard" aria-label="Paste widget (Ctrl+V)" title="Paste widget (Ctrl+V)" @click="pasteClipboard" />
          <v-btn icon="mdi-content-duplicate" size="x-small" variant="text" :disabled="!selectedWidget" aria-label="Duplicate widget (Ctrl+D)" title="Duplicate widget (Ctrl+D)" @click="duplicateSelected" />
          <v-divider vertical class="mx-1 my-1" />
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
                <path :d="gaugeArc(widget, gaugeFrac(widget))" :stroke="previewColor(widget.color)" fill="none" stroke-width="2" stroke-linecap="round" />
                <text :x="gaugeSize(widget) / 2" :y="gaugeSize(widget) / 2 + 2" text-anchor="middle" :fill="previewColor(widget.color)" font-size="6">{{ gaugeLabel(widget) }}</text>
              </svg>
              <svg v-else-if="widget.type === 'sparkline'" class="sd-svg" :viewBox="`0 0 ${widget.w || 60} ${widget.h || 20}`" preserveAspectRatio="none">
                <polyline :points="sparkSample(widget)" :stroke="previewColor(widget.color)" fill="none" stroke-width="1" vector-effect="non-scaling-stroke" />
              </svg>
              <template v-else-if="['button','toggle','momentary','setvalue','cycle','nav'].includes(widget.type)">{{ previewLabel(widget) }}</template>
              <template v-else-if="widget.type === 'stepper'">−&nbsp;{{ widget.variableId || 'val' }}&nbsp;+</template>
              <div v-else-if="widget.type === 'slider'" class="sd-slider-track">
                <span class="sd-slider-knob" :style="{ left: `${(liveFracOf(widget) ?? 0.5) * 100}%`, background: previewColor(widget.color) }" />
              </div>
              <!-- Corner resize handle for widgets with an on-panel box (drag = resize). -->
              <span
                v-if="selectedWidgetIdx === idx && isResizable(widget)"
                class="sd-resize-handle"
                aria-hidden="true"
                @pointerdown.stop.prevent="onResizePointerDown($event, idx)"
              />
            </div>
          </div>
        </div>
        <p class="sd-hint text-caption text-medium-emphasis">
          Drag widgets (corner handle resizes), nudge with the arrow keys (Shift = snap step, Delete removes).
          Ctrl+Z/Y undo·redo, Ctrl+C/V/D copy·paste·duplicate. Changes apply live — no device restart needed.
        </p>
      </section>

      <!-- Inspector -->
      <aside class="sd-inspector">
        <!-- Widget properties -->
        <div v-if="selectedWidget" class="sd-inspect-block">
          <div class="sd-rail-head sd-rail-head--row">
            <span>Widget</span>
            <span>
              <v-btn
                icon="mdi-arrange-send-backward"
                size="x-small"
                variant="text"
                :disabled="selectedWidgetIdx === 0"
                aria-label="Send backward"
                title="Send backward"
                @click="moveWidgetLayer(-1)"
              />
              <v-btn
                icon="mdi-arrange-bring-forward"
                size="x-small"
                variant="text"
                :disabled="selectedWidgetIdx === selectedPage.widgets.length - 1"
                aria-label="Bring forward"
                title="Bring forward"
                @click="moveWidgetLayer(1)"
              />
              <v-btn
                icon="mdi-delete-outline"
                size="x-small"
                variant="text"
                color="error"
                aria-label="Delete widget"
                @click="removeWidget(selectedWidgetIdx)"
              />
            </span>
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

          <!-- Variable-bound (value / bar / led) — searchable, grouped by source,
               with the live value beside each item. Still a combobox so custom
               (not-yet-defined) variable ids can be typed freely. -->
          <template v-else-if="['value', 'bar', 'led', 'gauge', 'sparkline'].includes(selectedWidget.type)">
            <v-combobox
              v-model="selectedWidget.variableId"
              v-model:search="variableSearch"
              :items="groupedVariableItems"
              no-filter
              label="Variable"
              :menu-props="{ maxHeight: 320 }"
              density="compact" variant="outlined" hide-details class="mb-3"
            >
              <template #item="{ item, props: itemProps }">
                <v-list-subheader v-if="item.raw.type === 'subheader'">{{ item.raw.title }}</v-list-subheader>
                <v-list-item v-else v-bind="itemProps" :title="item.raw.title">
                  <template #append>
                    <span class="text-caption text-medium-emphasis ml-3">{{ item.raw.live }}</span>
                  </template>
                </v-list-item>
              </template>
            </v-combobox>
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
            <input ref="imageFileInput" type="file" accept="image/*" hidden aria-label="Image file" @change="onImageFileChosen" />
            <v-select v-if="imageItems.length" v-model="selectedWidget.text" :items="imageItems" label="Image file"
              density="compact" variant="outlined" hide-details clearable class="mb-2" />
            <v-btn block size="small" variant="text" prepend-icon="mdi-fit-to-page-outline" class="mb-1" @click="fitImageToScreen">
              Fit to screen (background)
            </v-btn>
            <p class="text-caption text-medium-emphasis mb-2">
              Converted to {{ isColorDisplay ? 'colour (RGB565)' : '1-bit mono' }} at the W×H below, on upload.
            </p>
          </template>

          <!-- Button: label + the action fired on tap (touch panels only). -->
          <template v-else-if="selectedWidget.type === 'button'">
            <v-text-field
              v-model="selectedWidget.text"
              label="Label" density="compact" variant="outlined" hide-details class="mb-3"
            />
            <v-combobox
              v-model="selectedWidget.action"
              :items="actionItems"
              label="Action on tap"
              :menu-props="{ maxHeight: 320 }"
              density="compact" variant="outlined" hide-details class="mb-1"
            />
            <p class="text-caption text-medium-emphasis mb-3">
              Fires this action when tapped. Needs a display with a touch panel
              (calibrate under <router-link to="/touch" class="text-decoration-none">Touch Calibration</router-link>).
            </p>
          </template>

          <!-- Nav button: label + destination page -->
          <template v-else-if="selectedWidget.type === 'nav'">
            <v-text-field v-model="selectedWidget.text" label="Label (blank = arrow)"
              density="compact" variant="outlined" hide-details class="mb-3" />
            <v-combobox v-model="selectedWidget.target" :items="navTargetItems" label="Go to"
              density="compact" variant="outlined" hide-details class="mb-1" />
            <p class="text-caption text-medium-emphasis mb-3">
              <code>next</code>/<code>prev</code> cycle this display's pages, or pick a page id.
            </p>
          </template>

          <!-- Interactive variable widgets: toggle / slider / stepper / momentary /
               cycle / set value — all bind a variable, then type-specific fields. -->
          <template v-else-if="['toggle','slider','stepper','momentary','cycle','setvalue'].includes(selectedWidget.type)">
            <v-combobox
              v-model="selectedWidget.variableId"
              v-model:search="variableSearch"
              :items="groupedVariableItems"
              no-filter
              label="Variable"
              :menu-props="{ maxHeight: 320 }"
              density="compact" variant="outlined" hide-details class="mb-3"
            >
              <template #item="{ item, props: itemProps }">
                <v-list-subheader v-if="item.raw.type === 'subheader'">{{ item.raw.title }}</v-list-subheader>
                <v-list-item v-else v-bind="itemProps" :title="item.raw.title">
                  <template #append>
                    <span class="text-caption text-medium-emphasis ml-3">{{ item.raw.live }}</span>
                  </template>
                </v-list-item>
              </template>
            </v-combobox>

            <!-- Set value: just the preset -->
            <v-text-field v-if="selectedWidget.type === 'setvalue'" v-model.number="selectedWidget.max"
              label="Value to set" type="number" density="compact" variant="outlined" hide-details class="mb-3" />

            <!-- Cycle: value list (or numeric range) -->
            <template v-else-if="selectedWidget.type === 'cycle'">
              <v-text-field v-model="selectedWidget.target" label="Values (comma-separated)"
                placeholder="off, low, high" density="compact" variant="outlined" hide-details class="mb-1" />
              <p class="text-caption text-medium-emphasis mb-2">Leave blank to cycle numbers 0…Max by Step.</p>
              <div class="d-flex ga-2 mb-3">
                <v-text-field v-model.number="selectedWidget.max" label="Max" type="number" density="compact" variant="outlined" hide-details />
                <v-text-field v-model.number="selectedWidget.step" label="Step" type="number" density="compact" variant="outlined" hide-details />
              </div>
            </template>

            <!-- Toggle / slider / stepper / momentary: min + max (+ step for stepper) -->
            <template v-else>
              <div class="d-flex ga-2 mb-3">
                <v-text-field v-model.number="selectedWidget.min" :label="interactiveMinLabel" type="number" density="compact" variant="outlined" hide-details />
                <v-text-field v-model.number="selectedWidget.max" :label="interactiveMaxLabel" type="number" density="compact" variant="outlined" hide-details />
              </div>
              <v-text-field v-if="selectedWidget.type === 'stepper'" v-model.number="selectedWidget.step"
                label="Step" type="number" density="compact" variant="outlined" hide-details class="mb-3" />
            </template>
            <p class="text-caption text-medium-emphasis mb-3">
              Touch-panel widget — drives the variable live.
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
          <label class="sd-color-row mb-2" for="sd-widget-colour">
            <span class="sd-color-label">Colour</span>
            <input id="sd-widget-colour" type="color" v-model="selectedWidget.color" class="sd-color-input" />
          </label>
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

    <v-snackbar v-model="snackbar" color="success" :timeout="3000">
      {{ snackbarText }}
    </v-snackbar>
  </div>
</template>

<script setup>
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from 'vue'
import api from '../api/client'
import {
  GFX_CHAR_W, GFX_CHAR_H, fitScale, pointerToDevicePx, clampWidgetPos,
  clampWidgetSize, moveItem, variableGroupOf, sortPages,
} from '../lib/screenEditor'
import { fileToOfim } from '../lib/imageConvert'
import { useWebSocketStore } from '../stores/websocket'

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
  { type: 'button',   label: 'Button',         hint: 'Tap to run an action', icon: 'mdi-gesture-tap',    color: 'orange' },
  { type: 'toggle',   label: 'Toggle',         hint: 'Flip a variable on/off', icon: 'mdi-toggle-switch', color: 'orange' },
  { type: 'slider',   label: 'Slider',         hint: 'Drag to set a value',  icon: 'mdi-tune',            color: 'orange' },
  { type: 'stepper',  label: 'Stepper',        hint: 'Tap − / + to adjust',  icon: 'mdi-plus-minus-variant', color: 'orange' },
  { type: 'momentary', label: 'Momentary',     hint: 'Hold sets, release resets', icon: 'mdi-gesture-tap-button', color: 'orange' },
  { type: 'cycle',    label: 'Cycle',          hint: 'Tap cycles through values', icon: 'mdi-rotate-right', color: 'orange' },
  { type: 'setvalue', label: 'Set value',      hint: 'Tap sets a fixed value', icon: 'mdi-numeric',       color: 'orange' },
  { type: 'nav',      label: 'Nav button',     hint: 'Tap to change page',   icon: 'mdi-arrow-right-bold', color: 'orange' },
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
const actionItems = ref([])         // [{ title, value }] for the Button action picker
const imagesList = ref([])          // [{ name, size }] from the device
const imageUrls = ref({})           // name → object URL (originals, for the editor preview)
const uploadingImage = ref(false)
const imageFileInput = ref(null)

const gridStep = ref(1)
const scale = ref(4)
const canvasEl = ref(null)
const widgetEls = ref([])

// Live variable values (variable_snapshot / variable_change frames) — the app
// shell opens the socket, so this is just a read-through to the shared store.
const wsStore = useWebSocketStore()

const clipboard = ref(null)         // in-memory widget clipboard (deep clone)
const variableSearch = ref('')      // combobox search text for the variable picker
const savingOrder = ref(false)      // page_order write in flight
const snackbar = ref(false)
const snackbarText = ref('')

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

// ── Live preview values ──────────────────────────────────────────────────────
// Variable-bound widgets render the real value from the websocket store when the
// socket is up and the variable exists; otherwise they fall back to the static
// design-time samples (sparkline history stays a stylised sample either way).
function liveVarOf(widget) {
  const id = widget?.variableId
  if (!id || !wsStore.connected) return null
  const v = wsStore.variables[id]
  return v && v.value !== undefined && v.value !== null ? v : null
}
function liveNumberOf(widget) {
  const v = liveVarOf(widget)
  if (!v) return null
  const n = typeof v.value === 'boolean' ? (v.value ? 1 : 0) : Number(v.value)
  return Number.isFinite(n) ? n : null
}
// Fraction of a min…max range (defaults 0…100), clamped to 0…1. A missing value
// sits at the bottom of the range.
function fracOf(value, min, max) {
  const lo = min ?? 0, hi = max ?? 100
  return Math.max(0, Math.min(1, ((value ?? lo) - lo) / ((hi - lo) || 1)))
}
// Fraction of the widget's min…max range for bar/gauge fills (null = no live value).
function liveFracOf(widget) {
  const n = liveNumberOf(widget)
  return n == null ? null : fracOf(n, widget.min, widget.max)
}
function formatLiveValue(value, decimals) {
  if (typeof value === 'boolean') return value ? 'on' : 'off'
  if (typeof value === 'number') {
    return Number.isInteger(value) && (decimals == null) ? String(value) : value.toFixed(decimals ?? 1)
  }
  return String(value)
}

// Interactive-widget min/max labels change meaning per type (toggle = off/on,
// momentary = released/pressed, else a plain range).
const interactiveMinLabel = computed(() => {
  const t = selectedWidget.value?.type
  return t === 'toggle' ? 'Off value' : t === 'momentary' ? 'Released' : 'Min'
})
const interactiveMaxLabel = computed(() => {
  const t = selectedWidget.value?.type
  return t === 'toggle' ? 'On value' : t === 'momentary' ? 'Pressed' : 'Max'
})
// Nav destinations: the two relative moves plus every page id (combobox still
// accepts a freely-typed id).
const navTargetItems = computed(() => ['next', 'prev', ...pages.value.map((p) => p.id)])

// ── Searchable, grouped variable picker ──────────────────────────────────────
// Union of the REST snapshot (variableIds) and the live store, bucketed by the
// firmware's `source` tag (with id-prefix fallbacks for weather./node.), filtered
// by the combobox search text, with subheader sentinel rows between groups.
const VAR_GROUPS = [
  { key: 'local',   title: 'Local' },
  { key: 'sensor',  title: 'Sensors' },
  { key: 'input',   title: 'Inputs' },
  { key: 'output',  title: 'Outputs' },
  { key: 'weather', title: 'Weather' },
  { key: 'node',    title: 'Node' },
]
const groupedVariableItems = computed(() => {
  const q = (variableSearch.value || '').toLowerCase().trim()
  const byId = new Map()
  for (const id of variableIds.value) byId.set(id, wsStore.variables[id] || { id })
  for (const [id, v] of Object.entries(wsStore.variables)) if (!byId.has(id)) byId.set(id, v)

  const buckets = {}
  for (const [id, v] of byId) {
    if (q && !`${id} ${v.label || ''}`.toLowerCase().includes(q)) continue
    const g = variableGroupOf(id, v.source)
    ;(buckets[g] ||= []).push(v.id ? v : { ...v, id })
  }

  const items = []
  for (const group of VAR_GROUPS) {
    const rows = (buckets[group.key] || []).sort((a, b) => a.id.localeCompare(b.id))
    if (!rows.length) continue
    // Disabled so keyboard navigation skips the group header sentinel rows.
    items.push({ type: 'subheader', title: group.title, props: { disabled: true } })
    for (const v of rows) {
      const live = v.value !== undefined && v.value !== null && wsStore.connected
        ? `${formatLiveValue(v.value, null)}${v.unit ? ` ${v.unit}` : ''}`
        : ''
      items.push({ title: v.id, value: v.id, live })
    }
  }
  return items
})

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
      // Live value drives on/off: off renders as an outline dot. No live value →
      // keep the static "on" look so the widget stays visible while designing.
      const n = liveNumberOf(widget)
      const off = n != null && n === 0
      return off
        ? { ...base, width: `${d}px`, height: `${d}px`, borderRadius: '50%',
            background: 'transparent', border: `${Math.max(1, s)}px solid ${col}` }
        : { ...base, width: `${d}px`, height: `${d}px`, borderRadius: '50%', background: col }
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
    case 'button':
    case 'nav':
    case 'momentary':
    case 'setvalue':
    case 'toggle':
    case 'cycle':
    case 'stepper': {
      const size = Math.max(1, widget.textSize || 1)
      return { ...base, width: `${(widget.w || 60) * s}px`, height: `${(widget.h || 24) * s}px`,
        border: `${Math.max(1, s)}px solid ${col}`,
        background: widget.bg != null ? previewColor(widget.bg) : 'transparent',
        color: col, display: 'flex', alignItems: 'center', justifyContent: 'center',
        fontSize: `${GFX_CHAR_H * size * s}px`, overflow: 'hidden', whiteSpace: 'nowrap' }
    }
    case 'slider':
      return { ...base, color: col, width: `${(widget.w || 80) * s}px`, height: `${(widget.h || 16) * s}px` }
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
// Prefers the live value; falls back to the static sample when unknown.
function barFillStyle(widget) {
  const frac = liveFracOf(widget) ?? fracOf(widget._val, widget.min, widget.max)
  const col = previewColor(widget.color)
  const horizontal = (widget.w || 40) >= (widget.h || 8)
  return horizontal
    ? { position: 'absolute', left: 0, bottom: 0, top: 0, width: `${frac * 100}%`, background: col }
    : { position: 'absolute', left: 0, right: 0, bottom: 0, height: `${frac * 100}%`, background: col }
}

// ── Gauge / sparkline editor previews ────────────────────────────────────────
// The gauge fills from the live value when one is available, else a representative
// 66%; the sparkline stays a stylised sample wave (no history in the editor).
function gaugeSize(widget) { return (widget.w || 24) * 2 }
function gaugeFrac(widget) { return liveFracOf(widget) ?? 0.66 }
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
  const n = liveNumberOf(widget)
  if (n != null) return String(Math.round(n))
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
  // Value widgets show the live reading when available, else the {placeholder}.
  const live = liveVarOf(widget)
  const inner = live ? formatLiveValue(live.value, widget.decimals)
    : widget.variableId ? `{${widget.variableId}}` : '{value}'
  return `${widget.prefix || ''}${inner}${widget.suffix || ''}`
}

// Centred label shown on the box-style interactive widgets in the canvas preview.
function previewLabel(widget) {
  if (widget.text) return widget.text
  switch (widget.type) {
    case 'toggle':    return 'Toggle'
    case 'momentary': return 'Hold'
    case 'setvalue':  return 'Set'
    case 'cycle':     return widget.variableId || 'Cycle'
    case 'nav':       return widget.target === 'prev' ? '<' : '>'
    default:          return 'Button'
  }
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
  if (w.action != null) out.action = w.action   // button widgets only
  if (w.target != null) out.target = w.target   // nav destination / cycle value list
  if (w.step != null) out.step = w.step          // stepper / cycle numeric step
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
    case 'button':   w.w = 60; w.h = 24; w.text = 'Button'; w.action = ''; break
    case 'toggle':   w.w = 40; w.h = 20; w.text = 'Toggle'; w.min = 0; w.max = 1; break
    case 'slider':   w.w = 80; w.h = 16; w.min = 0; w.max = 100; break
    case 'stepper':  w.w = 70; w.h = 24; w.min = 0; w.max = 100; w.step = 1; break
    case 'momentary': w.w = 60; w.h = 24; w.text = 'Hold'; w.min = 0; w.max = 1; break
    case 'cycle':    w.w = 60; w.h = 24; w.target = ''; w.min = 0; w.max = 3; w.step = 1; break
    case 'setvalue': w.w = 60; w.h = 24; w.text = 'Set'; w.max = 1; break
    case 'nav':      w.w = 40; w.h = 24; w.text = ''; w.target = 'next'; break
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

// Arrow keys nudge the focused widget by 1px (Shift = the snap step);
// Delete/Backspace removes. Ctrl-combos bubble up to the global shortcut handler.
function onWidgetKeydown(event, idx) {
  const widget = selectedPage.value.widgets[idx]
  if (!widget || event.ctrlKey || event.metaKey) return
  const moves = {
    ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1],
  }
  if (moves[event.key]) {
    event.preventDefault()
    const step = event.shiftKey ? gridStep.value : 1
    const [dx, dy] = moves[event.key]
    nudgeWidget(widget, dx * step, dy * step)
  } else if (event.key === 'Delete' || event.key === 'Backspace') {
    event.preventDefault()
    removeWidget(idx)
  }
}

function nudgeWidget(widget, dx, dy) {
  const { x, y } = clampWidgetPos((widget.x || 0) + dx, (widget.y || 0) + dy, activeDisplay.value)
  widget.x = x
  widget.y = y
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
  if (resizeState) {
    const widget = selectedPage.value.widgets[resizeState.idx]
    if (!widget) return
    const rect = canvasEl.value.getBoundingClientRect()
    // Raw pointer position in device px — clampWidgetSize does the snap/bounds.
    const px = (event.clientX - rect.left) / scale.value
    const py = (event.clientY - rect.top) / scale.value
    applyResize(widget, px, py)
    return
  }
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
  resizeState = null
}

// ── Resize by dragging the corner handle ─────────────────────────────────────
// Same pointer math and snap step as move-drag, clamped by clampWidgetSize so the
// widget stays on the panel. Gauges resize their radius; lines their ΔX/ΔY endpoint.
const RESIZABLE_TYPES = ['rect', 'bar', 'image', 'sparkline', 'line', 'gauge', 'button',
  'toggle', 'slider', 'stepper', 'momentary', 'cycle', 'setvalue', 'nav']
const isResizable = (w) => RESIZABLE_TYPES.includes(w?.type)
let resizeState = null

function onResizePointerDown(event, idx) {
  selectedWidgetIdx.value = idx
  resizeState = { idx }
  event.target.setPointerCapture?.(event.pointerId)
}

function applyResize(widget, px, py) {
  const disp = activeDisplay.value
  const step = gridStep.value
  const x = widget.x || 0, y = widget.y || 0
  if (widget.type === 'gauge') {
    // The gauge box is (2·radius)² from its top-left — resize via the diameter.
    const d = clampWidgetSize(px - x, py - y, x, y, disp, step, { w: 2, h: 2 })
    widget.w = Math.max(1, Math.round(Math.min(d.w, d.h) / 2))
    return
  }
  // Line w/h are endpoint deltas and may be 0 (the handle keeps them ≥ 0; type
  // negative deltas in the inspector for up/left lines).
  const min = widget.type === 'line' ? { w: 0, h: 0 } : { w: 1, h: 1 }
  const { w, h } = clampWidgetSize(px - x, py - y, x, y, disp, step, min)
  widget.w = w
  widget.h = h
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

// ── Undo / redo ──────────────────────────────────────────────────────────────
// The history is a stack of JSON snapshots of the edited page. A deep watcher
// (debounced 400 ms) commits after any mutation — drags, nudges and rapid prop
// typing coalesce into one entry. Undo/redo re-apply a snapshot; the follow-up
// watcher commit then compares equal and skips, so applying history never loops.
const HISTORY_CAP = 50
const history = ref([])
const historyIdx = ref(-1)
let historyTimer = null

const canUndo = computed(() => historyIdx.value > 0)
const canRedo = computed(() => historyIdx.value < history.value.length - 1)

function resetHistory() {
  clearTimeout(historyTimer)
  historyTimer = null
  history.value = selectedPage.value ? [JSON.stringify(selectedPage.value)] : []
  historyIdx.value = history.value.length - 1
}

function commitHistory() {
  clearTimeout(historyTimer)
  historyTimer = null
  if (!selectedPage.value) return
  const snap = JSON.stringify(selectedPage.value)
  if (snap === history.value[historyIdx.value]) return
  history.value.splice(historyIdx.value + 1)  // a new edit invalidates the redo tail
  history.value.push(snap)
  if (history.value.length > HISTORY_CAP) history.value.splice(0, history.value.length - HISTORY_CAP)
  historyIdx.value = history.value.length - 1
}

watch(selectedPage, () => {
  clearTimeout(historyTimer)
  historyTimer = setTimeout(commitHistory, 400)
}, { deep: true })

function applyHistory(idx) {
  clearTimeout(historyTimer)
  historyTimer = null
  historyIdx.value = idx
  const page = JSON.parse(history.value[idx])
  selectedPage.value = page
  if (selectedWidgetIdx.value != null && selectedWidgetIdx.value >= page.widgets.length) {
    selectedWidgetIdx.value = null
  }
}

function undo() {
  commitHistory()  // flush a pending debounce so Ctrl+Z right after an edit undoes that edit
  if (canUndo.value) applyHistory(historyIdx.value - 1)
}

function redo() {
  commitHistory()
  if (canRedo.value) applyHistory(historyIdx.value + 1)
}

// ── Duplicate / copy / paste / z-order ───────────────────────────────────────
const cloneWidget = (w) => JSON.parse(JSON.stringify(w))

function uniqueWidgetId() {
  const ids = new Set(selectedPage.value.widgets.map((w) => w.id))
  let n = selectedPage.value.widgets.length + 1
  while (ids.has(`w${n}`)) n++
  return `w${n}`
}

// Insert a clone offset +4px (clamped) and select it.
function addWidgetClone(src) {
  if (!selectedPage.value) return
  const w = cloneWidget(src)
  w.id = uniqueWidgetId()
  const pos = clampWidgetPos((w.x || 0) + 4, (w.y || 0) + 4, activeDisplay.value)
  w.x = pos.x
  w.y = pos.y
  selectedPage.value.widgets.push(w)
  selectedWidgetIdx.value = selectedPage.value.widgets.length - 1
}

function duplicateSelected() {
  if (selectedWidget.value) addWidgetClone(selectedWidget.value)
}

function copySelected() {
  if (selectedWidget.value) clipboard.value = cloneWidget(selectedWidget.value)
}

function pasteClipboard() {
  if (!clipboard.value || !selectedPage.value) return
  addWidgetClone(clipboard.value)
  // Re-arm the clipboard at the pasted position so repeated pastes cascade.
  clipboard.value = cloneWidget(selectedWidget.value)
}

// Z-order = array order (later paints on top). Move the selected widget one slot.
function moveWidgetLayer(delta) {
  const idx = selectedWidgetIdx.value
  if (idx == null || !selectedPage.value) return
  if (moveItem(selectedPage.value.widgets, idx, idx + delta)) selectedWidgetIdx.value = idx + delta
}

// ── Global keyboard shortcuts ────────────────────────────────────────────────
// Skipped while typing in a field, and when a focused widget's own keydown
// handler already consumed the event (it preventDefault()s first).
function isEditableTarget(t) {
  return !!t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.tagName === 'SELECT' || t.isContentEditable)
}

// Delete/Backspace and arrow nudges only act when focus is "on the canvas":
// the page body (nothing focused) or an element inside the panel. Otherwise a
// focused toolbar button (or slider) would have its keys hijacked.
function isStageTarget(t) {
  return t === document.body || (!!canvasEl.value && canvasEl.value.contains(t))
}

function onGlobalKeydown(event) {
  if (!selectedPage.value || event.defaultPrevented || isEditableTarget(event.target)) return
  const ctrl = event.ctrlKey || event.metaKey
  const key = event.key.toLowerCase()
  if (ctrl && key === 'z' && !event.shiftKey) { event.preventDefault(); undo(); return }
  if (ctrl && ((key === 'z' && event.shiftKey) || key === 'y')) { event.preventDefault(); redo(); return }
  if (ctrl && key === 'd') { event.preventDefault(); duplicateSelected(); return }
  if (ctrl && key === 'c' && selectedWidget.value) { event.preventDefault(); copySelected(); return }
  if (ctrl && key === 'v' && clipboard.value) { event.preventDefault(); pasteClipboard(); return }
  if (ctrl || !selectedWidget.value || !isStageTarget(event.target)) return
  if (event.key === 'Delete' || event.key === 'Backspace') {
    event.preventDefault()
    removeWidget(selectedWidgetIdx.value)
    return
  }
  const moves = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] }
  if (moves[event.key]) {
    event.preventDefault()
    const step = event.shiftKey ? gridStep.value : 1
    nudgeWidget(selectedWidget.value, moves[event.key][0] * step, moves[event.key][1] * step)
  }
}

// ── Page CRUD ────────────────────────────────────────────────────────────────
function selectPage(page) {
  selectedPage.value = JSON.parse(JSON.stringify(page))
  selectedWidgetIdx.value = null
  scale.value = fitScale(activeDisplay.value.width)
  resetHistory()
}

// ── Page ordering (display page_order) ───────────────────────────────────────
// The chips follow each display's canonical page_order; moving the active page
// left/right reorders within its display and persists via the displays config.
const orderedPages = computed(() => sortPages(pages.value, displayList.value))

function siblingPageIds() {
  if (!selectedPage.value) return []
  return orderedPages.value
    .filter((p) => p.displayId === selectedPage.value.displayId)
    .map((p) => p.id)
}

function canMovePage(delta) {
  const ids = siblingPageIds()
  const from = ids.indexOf(selectedPage.value?.id)
  return from >= 0 && from + delta >= 0 && from + delta < ids.length
}

async function movePage(delta) {
  const page = selectedPage.value
  if (!page || !canMovePage(delta)) return
  const ids = siblingPageIds()
  moveItem(ids, ids.indexOf(page.id), ids.indexOf(page.id) + delta)
  savingOrder.value = true
  statusMessage.value = null
  try {
    // Re-fetch the displays config and patch only page_order — POST /api/displays
    // replaces the whole file, so send back everything the device just gave us.
    const data = await api.get('/api/displays')
    const displays = data.displays || []
    const target = displays.find((d) => d.id === page.displayId)
    if (!target) throw new Error(`Display "${page.displayId}" not found on the device`)
    target.page_order = ids
    const res = await api.post('/api/displays', { displays })
    displayList.value = displays  // re-sorts the chips via orderedPages
    snackbarText.value = res?.restartRequired
      ? 'Page order saved. Restarting to apply.'
      : (res?.message ? 'Page order saved and applied.' : 'Page order saved.')
    snackbar.value = true
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'Failed to save page order' }
  } finally {
    savingOrder.value = false
  }
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
    const res = await api.post('/api/displays', { pages: [selectedPage.value] })
    const clone = JSON.parse(JSON.stringify(selectedPage.value))
    const existing = pages.value.findIndex((p) => p.id === selectedPage.value.id)
    if (existing >= 0) pages.value[existing] = clone
    else pages.value.push(clone)
    const name = selectedPage.value.title || selectedPage.value.id
    // Display/page changes apply live now (restartRequired=false) — show what the
    // device reported and don't imply a reboot unless it actually asked for one.
    statusMessage.value = {
      type: 'success',
      text: res?.restartRequired ? `Saved “${name}”. Restarting to apply.` : (res?.message || `Saved “${name}” — applied.`),
    }
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
    const [dispData, pageData, varData, imgData, actData] = await Promise.all([
      api.get('/api/displays').catch(() => ({ displays: [] })),
      api.get('/api/displays/pages').catch(() => ({ pages: [] })),
      api.get('/api/variables').catch(() => ({ variables: [] })),
      api.images.list().catch(() => ({ images: [] })),
      api.get('/api/actions').catch(() => ({ actions: [] })),
    ])
    displayList.value = dispData.displays || []
    imagesList.value = imgData.images || []
    // Actions for the Button widget's "action on tap" picker (id is what the
    // device fires; show the friendly name when present).
    actionItems.value = (actData.actions || [])
      .filter((a) => a && a.id)
      .map((a) => ({ title: a.name ? `${a.name} (${a.id})` : a.id, value: a.id }))
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
  window.addEventListener('keydown', onGlobalKeydown)
})

onUnmounted(() => {
  window.removeEventListener('pointermove', onWidgetPointerMove)
  window.removeEventListener('pointerup', onWidgetPointerUp)
  window.removeEventListener('keydown', onGlobalKeydown)
  clearTimeout(historyTimer)
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
  /* Let the corner resize handle poke past the widget box (shapes normally clip). */
  overflow: visible;
}

/* Corner resize handle on the selected widget. */
.sd-resize-handle {
  position: absolute;
  right: -5px;
  bottom: -5px;
  width: 10px;
  height: 10px;
  border-radius: 2px;
  background: rgb(96, 165, 250);
  border: 1px solid #0b1017;
  cursor: nwse-resize;
  touch-action: none;
  z-index: 2;
}

/* Image widget preview. */
.sd-img { width: 100%; height: 100%; object-fit: fill; image-rendering: pixelated; display: block; }
.sd-img-ph {
  width: 100%; height: 100%;
  display: flex; align-items: center; justify-content: center;
  color: rgba(207, 232, 255, 0.5);
}
.sd-svg { width: 100%; height: 100%; display: block; overflow: visible; }

/* Slider preview: a centred track with a draggable-looking knob. */
.sd-slider-track {
  position: relative;
  width: 100%;
  height: 100%;
}
.sd-slider-track::before {
  content: '';
  position: absolute;
  left: 0; right: 0; top: 50%;
  height: 2px;
  transform: translateY(-50%);
  background: currentColor;
  opacity: 0.6;
}
.sd-slider-knob {
  position: absolute;
  top: 0; bottom: 0;
  width: 6px;
  transform: translateX(-50%);
  border-radius: 2px;
}

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
