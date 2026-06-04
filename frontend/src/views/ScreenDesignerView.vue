<template>
  <div>
    <v-row>
      <v-col>
        <h1 class="text-h5 mb-4">
          <v-icon class="mr-2">mdi-monitor-edit</v-icon>
          Screen Designer
        </h1>
      </v-col>
      <v-col cols="auto" class="d-flex align-center ga-2">
        <v-btn color="primary" prepend-icon="mdi-plus" @click="openNewPage">New Page</v-btn>
        <v-btn :loading="loading" prepend-icon="mdi-refresh" variant="text" @click="refresh" />
      </v-col>
    </v-row>

    <v-row v-if="statusMessage">
      <v-col>
        <v-alert :type="statusMessage.type" variant="tonal" closable @click:close="statusMessage = null">
          {{ statusMessage.text }}
        </v-alert>
      </v-col>
    </v-row>

    <v-row>
      <!-- Page list -->
      <v-col cols="12" md="3">
        <v-card>
          <v-card-title>Pages</v-card-title>
          <v-list density="compact" nav>
            <v-list-item
              v-for="page in pages"
              :key="page.id"
              :active="selectedPage?.id === page.id"
              :title="page.title || page.id"
              :subtitle="`${page.displayId} · ${page.widgets.length} widgets`"
              prepend-icon="mdi-television"
              @click="selectPage(page)"
            />
            <v-list-item v-if="pages.length === 0">
              <v-list-item-title class="text-medium-emphasis">No pages yet.</v-list-item-title>
            </v-list-item>
          </v-list>
        </v-card>
      </v-col>

      <!-- Page editor -->
      <v-col cols="12" md="9">
        <v-card v-if="selectedPage">
          <v-card-title class="d-flex align-center justify-space-between">
            <span>{{ selectedPage.title || selectedPage.id }}</span>
            <div class="d-flex ga-2">
              <v-btn prepend-icon="mdi-plus" size="small" variant="tonal" @click="addWidget">Widget</v-btn>
              <v-btn color="primary" :loading="saving" prepend-icon="mdi-content-save" size="small" @click="savePage">Save</v-btn>
            </div>
          </v-card-title>
          <v-divider />
          <v-card-text>
            <v-row>
              <v-col cols="6">
                <v-text-field v-model="selectedPage.id" label="Page ID" density="compact" />
              </v-col>
              <v-col cols="6">
                <v-text-field v-model="selectedPage.title" label="Title" density="compact" />
              </v-col>
              <v-col cols="6">
                <v-text-field v-model="selectedPage.displayId" label="Display ID" density="compact" />
              </v-col>
            </v-row>

            <v-divider class="my-3" />
            <div class="text-subtitle-2 mb-2">Widgets</div>

            <!-- Visual preview -->
            <div
              class="position-relative mb-4"
              :style="`width:${previewWidth}px;height:${previewHeight}px;background:#111;border:1px solid #444;`"
            >
              <div
                v-for="(widget, idx) in selectedPage.widgets"
                :key="idx"
                class="position-absolute"
                :style="`left:${widget.x}px;top:${widget.y}px;color:white;font-size:${(widget.textSize||1)*8}px;line-height:1;cursor:pointer;`"
                @click="selectWidget(idx)"
              >
                <span>{{ widgetPreviewText(widget) }}</span>
              </div>
            </div>

            <!-- Widget table -->
            <v-table density="compact">
              <thead>
                <tr>
                  <th>Type</th>
                  <th>X</th>
                  <th>Y</th>
                  <th>Size</th>
                  <th>Text / Variable</th>
                  <th></th>
                </tr>
              </thead>
              <tbody>
                <tr
                  v-for="(widget, idx) in selectedPage.widgets"
                  :key="idx"
                  :class="selectedWidgetIdx === idx ? 'bg-primary-lighten-5' : ''"
                  @click="selectWidget(idx)"
                >
                  <td>
                    <v-select
                      v-model="widget.type"
                      :items="['text','value']"
                      density="compact"
                      hide-details
                      variant="plain"
                      style="min-width:80px"
                    />
                  </td>
                  <td>
                    <v-text-field v-model.number="widget.x" type="number" density="compact" hide-details variant="plain" style="max-width:55px" />
                  </td>
                  <td>
                    <v-text-field v-model.number="widget.y" type="number" density="compact" hide-details variant="plain" style="max-width:55px" />
                  </td>
                  <td>
                    <v-text-field v-model.number="widget.textSize" type="number" density="compact" hide-details variant="plain" style="max-width:55px" />
                  </td>
                  <td>
                    <v-text-field
                      v-if="widget.type === 'text'"
                      v-model="widget.text"
                      label="text"
                      density="compact"
                      hide-details
                      variant="plain"
                    />
                    <v-text-field
                      v-else
                      v-model="widget.variableId"
                      label="variable"
                      density="compact"
                      hide-details
                      variant="plain"
                    />
                  </td>
                  <td>
                    <v-btn icon="mdi-delete" size="small" variant="text" color="error" @click.stop="removeWidget(idx)" />
                  </td>
                </tr>
                <tr v-if="selectedPage.widgets.length === 0">
                  <td colspan="6" class="text-medium-emphasis text-center py-3">No widgets. Add one above.</td>
                </tr>
              </tbody>
            </v-table>
          </v-card-text>
        </v-card>

        <v-alert v-else type="info" variant="tonal">
          Select a page from the list, or create a new one.
        </v-alert>
      </v-col>
    </v-row>
  </div>
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import api from '../api/client'

const loading = ref(false)
const saving = ref(false)
const statusMessage = ref(null)
const pages = ref([])
const selectedPage = ref(null)
const selectedWidgetIdx = ref(null)
const displayList = ref([])

const previewWidth = computed(() => {
  const disp = displayList.value.find(d => d.id === selectedPage.value?.displayId)
  return disp?.width || 128
})

const previewHeight = computed(() => {
  const disp = displayList.value.find(d => d.id === selectedPage.value?.displayId)
  return disp?.height || 64
})

function selectPage(page) {
  selectedPage.value = JSON.parse(JSON.stringify(page))
  selectedWidgetIdx.value = null
}

function selectWidget(idx) {
  selectedWidgetIdx.value = idx
}

function widgetPreviewText(widget) {
  if (widget.type === 'text') return widget.text || ''
  return `[${widget.variableId || 'var'}]`
}

function openNewPage() {
  const page = {
    id: `page${pages.value.length + 1}`,
    title: `Page ${pages.value.length + 1}`,
    displayId: displayList.value[0]?.id || 'display1',
    widgets: [],
    _new: true,
  }
  pages.value.push(page)
  selectPage(page)
}

function addWidget() {
  if (!selectedPage.value) return
  selectedPage.value.widgets.push({
    id: `w${selectedPage.value.widgets.length + 1}`,
    type: 'text',
    x: 0,
    y: selectedPage.value.widgets.length * 10,
    textSize: 1,
    text: 'Hello',
    variableId: '',
  })
}

function removeWidget(idx) {
  selectedPage.value.widgets.splice(idx, 1)
  if (selectedWidgetIdx.value === idx) selectedWidgetIdx.value = null
}

async function savePage() {
  if (!selectedPage.value) return
  saving.value = true
  statusMessage.value = null
  try {
    const payload = {
      pages: [selectedPage.value],
    }
    await api.post('/api/displays', payload)
    // Update local list
    const existing = pages.value.findIndex(p => p.id === selectedPage.value.id)
    if (existing >= 0) pages.value[existing] = JSON.parse(JSON.stringify(selectedPage.value))
    else pages.value.push(JSON.parse(JSON.stringify(selectedPage.value)))
    statusMessage.value = { type: 'success', text: 'Page saved.' }
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
    const [dispData, pageData] = await Promise.all([
      api.get('/api/displays').catch(() => ({ displays: [] })),
      api.get('/api/displays/pages').catch(() => ({ pages: [] })),
    ])
    displayList.value = dispData.displays || []
    pages.value = (pageData.pages || []).map(p => ({
      ...p,
      widgets: p.widgets || [],
    }))
  } catch (err) {
    statusMessage.value = { type: 'error', text: err.message || 'Failed to load' }
  } finally {
    loading.value = false
  }
}

onMounted(() => {
  refresh()
})
</script>
