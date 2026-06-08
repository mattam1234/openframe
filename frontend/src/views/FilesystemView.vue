<template>
  <div>
    <v-row align="center">
      <v-col>
        <h1 class="text-h5 mb-0">
          <v-icon class="mr-2">mdi-folder-cog-outline</v-icon>
          Filesystem
        </h1>
        <div class="text-medium-emphasis text-caption">
          Browse what is stored on the device (LittleFS)
        </div>
      </v-col>
      <v-col cols="auto">
        <v-btn
          class="mr-2"
          prepend-icon="mdi-folder-plus-outline"
          variant="tonal"
          @click="openNameDialog('folder')"
        >
          New folder
        </v-btn>
        <v-btn
          class="mr-2"
          prepend-icon="mdi-file-plus-outline"
          variant="tonal"
          @click="openNameDialog('file')"
        >
          New file
        </v-btn>
        <v-btn
          class="mr-2"
          prepend-icon="mdi-upload"
          variant="tonal"
          :loading="uploadBusy"
          :disabled="uploadBusy"
          @click="triggerUpload"
        >
          Upload
        </v-btn>
        <v-btn :loading="loading" prepend-icon="mdi-refresh" variant="tonal" @click="refresh">
          Refresh
        </v-btn>
        <input
          ref="fileInput"
          type="file"
          class="d-none"
          @change="onFilePicked"
        >
      </v-col>
    </v-row>

    <v-row v-if="errorMessage">
      <v-col>
        <v-alert type="error" variant="tonal" closable @click:close="errorMessage = ''">
          {{ errorMessage }}
        </v-alert>
      </v-col>
    </v-row>

    <!-- Storage usage -->
    <v-row>
      <v-col>
        <v-card>
          <v-card-text>
            <div class="d-flex align-center justify-space-between mb-2">
              <span class="text-subtitle-2">Storage</span>
              <span class="text-caption text-medium-emphasis">
                {{ formatBytes(stat.used) }} used / {{ formatBytes(stat.total) }}
                ({{ formatBytes(stat.free) }} free)
              </span>
            </div>
            <v-progress-linear
              :model-value="usagePercent"
              :color="usagePercent > 90 ? 'error' : usagePercent > 75 ? 'warning' : 'primary'"
              height="10"
              rounded
            />
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>

    <!-- Breadcrumb -->
    <v-row>
      <v-col>
        <v-breadcrumbs :items="breadcrumbs" density="compact" class="px-0">
          <template #item="{ item }">
            <v-breadcrumbs-item
              :disabled="item.disabled"
              style="cursor: pointer"
              @click="!item.disabled && navigate(item.path)"
            >
              {{ item.title }}
            </v-breadcrumbs-item>
          </template>
        </v-breadcrumbs>
      </v-col>
    </v-row>

    <!-- Entries -->
    <v-row>
      <v-col>
        <v-card>
          <v-card-text class="pa-0">
            <v-table density="compact">
              <thead>
                <tr>
                  <th class="text-left">Name</th>
                  <th class="text-left">Type</th>
                  <th class="text-right">Size</th>
                  <th class="text-right">Actions</th>
                </tr>
              </thead>
              <tbody>
                <tr v-if="currentPath !== '/'">
                  <td colspan="4">
                    <a style="cursor: pointer" @click="navigate(parentPath)">
                      <v-icon size="small" class="mr-1">mdi-arrow-up-left</v-icon>..
                    </a>
                  </td>
                </tr>
                <tr v-for="entry in sortedEntries" :key="entry.path">
                  <td>
                    <v-icon size="small" class="mr-1">
                      {{ entry.type === 'dir' ? 'mdi-folder' : 'mdi-file-document-outline' }}
                    </v-icon>
                    <a
                      v-if="entry.type === 'dir'"
                      style="cursor: pointer"
                      @click="navigate(entry.path)"
                    >{{ entry.name }}</a>
                    <span v-else>{{ entry.name }}</span>
                  </td>
                  <td class="text-medium-emphasis">{{ entry.type }}</td>
                  <td class="text-right">{{ entry.type === 'dir' ? '—' : formatBytes(entry.size) }}</td>
                  <td class="text-right">
                    <template v-if="entry.type === 'file'">
                      <v-btn
                        icon="mdi-eye"
                        size="x-small"
                        variant="text"
                        title="Inspect"
                        @click="inspect(entry)"
                      />
                      <v-btn
                        icon="mdi-rename-box"
                        size="x-small"
                        variant="text"
                        title="Rename / move"
                        :disabled="isProtected(entry.path)"
                        @click="openNameDialog('rename', entry)"
                      />
                      <v-btn
                        icon="mdi-download"
                        size="x-small"
                        variant="text"
                        title="Download"
                        :href="downloadUrl(entry.path)"
                      />
                      <v-btn
                        icon="mdi-delete"
                        size="x-small"
                        variant="text"
                        color="error"
                        title="Delete"
                        :disabled="isProtected(entry.path)"
                        @click="confirmDelete(entry)"
                      />
                    </template>
                  </td>
                </tr>
                <tr v-if="sortedEntries.length === 0">
                  <td colspan="4" class="text-medium-emphasis">This directory is empty.</td>
                </tr>
              </tbody>
            </v-table>
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>

    <!-- Inspect / edit dialog -->
    <v-dialog v-model="editor.open" max-width="800">
      <v-card>
        <v-card-title class="d-flex align-center justify-space-between">
          <span class="text-truncate">{{ editor.path }}</span>
          <v-chip v-if="editor.dirty" size="x-small" color="warning">unsaved</v-chip>
        </v-card-title>
        <v-card-text>
          <v-alert v-if="editor.error" type="error" variant="tonal" density="compact" class="mb-2">
            {{ editor.error }}
          </v-alert>
          <v-textarea
            v-model="editor.content"
            :loading="editor.loading"
            variant="outlined"
            auto-grow
            rows="16"
            class="font-monospace"
            spellcheck="false"
            @update:model-value="editor.dirty = true"
          />
        </v-card-text>
        <v-card-actions>
          <v-btn
            v-if="editor.isJson"
            variant="text"
            size="small"
            prepend-icon="mdi-code-json"
            @click="formatJson"
          >
            Format
          </v-btn>
          <span v-if="editor.isJson" class="text-caption text-medium-emphasis ml-2">
            validated before saving
          </span>
          <v-spacer />
          <v-btn variant="text" @click="editor.open = false">Close</v-btn>
          <v-btn
            color="primary"
            variant="tonal"
            :loading="editor.saving"
            :disabled="isProtected(editor.path)"
            @click="saveEditor"
          >
            Save
          </v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>

    <!-- New folder / new file / rename -->
    <v-dialog v-model="nameDialog.open" max-width="480">
      <v-card>
        <v-card-title>{{ nameDialog.title }}</v-card-title>
        <v-card-text>
          <v-alert v-if="nameDialog.error" type="error" variant="tonal" density="compact" class="mb-2">
            {{ nameDialog.error }}
          </v-alert>
          <v-text-field
            v-model="nameDialog.value"
            :label="nameDialog.mode === 'rename' ? 'New path' : 'Name'"
            :hint="nameDialog.mode === 'rename'
              ? 'Full path, e.g. /templates/renamed.json'
              : `Created in ${currentPath}`"
            persistent-hint
            variant="outlined"
            autofocus
            @keyup.enter="submitNameDialog"
          />
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn variant="text" @click="nameDialog.open = false">Cancel</v-btn>
          <v-btn color="primary" variant="tonal" :loading="nameDialog.busy" @click="submitNameDialog">
            {{ nameDialog.mode === 'rename' ? 'Rename' : 'Create' }}
          </v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>

    <!-- Delete confirm -->
    <v-dialog v-model="deleteDialog.open" max-width="420">
      <v-card>
        <v-card-title>Delete file?</v-card-title>
        <v-card-text>
          This permanently removes <code>{{ deleteDialog.path }}</code> from the device.
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn variant="text" @click="deleteDialog.open = false">Cancel</v-btn>
          <v-btn color="error" variant="tonal" :loading="deleteDialog.busy" @click="doDelete">
            Delete
          </v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>

    <v-snackbar v-model="snackbar.open" :color="snackbar.color" timeout="3000">
      {{ snackbar.text }}
    </v-snackbar>
  </div>
</template>

<script setup>
import { computed, onMounted, reactive, ref } from 'vue'
import api from '../api/client'

const loading = ref(false)
const uploadBusy = ref(false)
const errorMessage = ref('')
const currentPath = ref('/')
const entries = ref([])
const stat = reactive({ total: 0, used: 0, free: 0 })

const fileInput = ref(null)

const editor = reactive({
  open: false, path: '', content: '', loading: false, saving: false,
  dirty: false, error: '', isJson: false,
})
const deleteDialog = reactive({ open: false, path: '', busy: false })
const nameDialog = reactive({ open: false, mode: 'folder', title: '', value: '', busy: false, error: '', original: '' })
const snackbar = reactive({ open: false, text: '', color: 'success' })

const usagePercent = computed(() =>
  stat.total > 0 ? Math.round((stat.used / stat.total) * 100) : 0
)

const sortedEntries = computed(() =>
  [...entries.value].sort((a, b) => {
    if (a.type !== b.type) return a.type === 'dir' ? -1 : 1
    return a.name.localeCompare(b.name)
  })
)

const parentPath = computed(() => {
  if (currentPath.value === '/') return '/'
  const trimmed = currentPath.value.replace(/\/$/, '')
  const idx = trimmed.lastIndexOf('/')
  return idx <= 0 ? '/' : trimmed.slice(0, idx)
})

const breadcrumbs = computed(() => {
  const crumbs = [{ title: 'root', path: '/', disabled: currentPath.value === '/' }]
  if (currentPath.value === '/') return crumbs
  let acc = ''
  for (const part of currentPath.value.split('/').filter(Boolean)) {
    acc += '/' + part
    crumbs.push({ title: part, path: acc, disabled: acc === currentPath.value })
  }
  return crumbs
})

function isProtected(path) {
  return path === '/www' || path.startsWith('/www/')
}

function downloadUrl(path) {
  return api.fs.downloadUrl(path)
}

function formatBytes(bytes) {
  if (bytes === undefined || bytes === null) return '—'
  if (bytes < 1024) return `${bytes} B`
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`
  return `${(bytes / (1024 * 1024)).toFixed(2)} MB`
}

function notify(text, color = 'success') {
  snackbar.text = text
  snackbar.color = color
  snackbar.open = true
}

async function refresh() {
  loading.value = true
  errorMessage.value = ''
  try {
    const [statRes, listRes] = await Promise.all([
      api.fs.stat(),
      api.fs.list(currentPath.value),
    ])
    Object.assign(stat, statRes)
    entries.value = listRes.entries || []
  } catch (error) {
    errorMessage.value = error.message || 'Failed to load filesystem'
  } finally {
    loading.value = false
  }
}

function navigate(path) {
  currentPath.value = path || '/'
  refresh()
}

async function inspect(entry) {
  editor.open = true
  editor.path = entry.path
  editor.content = ''
  editor.error = ''
  editor.dirty = false
  editor.isJson = entry.path.endsWith('.json')
  editor.loading = true
  try {
    editor.content = await api.fs.read(entry.path)
  } catch (error) {
    editor.error = error.message || 'Failed to read file'
  } finally {
    editor.loading = false
  }
}

function formatJson() {
  editor.error = ''
  try {
    editor.content = JSON.stringify(JSON.parse(editor.content), null, 2)
    editor.dirty = true
  } catch (e) {
    editor.error = `Invalid JSON: ${e.message}`
  }
}

async function saveEditor() {
  editor.error = ''
  if (editor.isJson) {
    try {
      JSON.parse(editor.content)
    } catch (e) {
      editor.error = `Invalid JSON: ${e.message}`
      return
    }
  }
  editor.saving = true
  try {
    await api.fs.upload(editor.path, editor.content)
    editor.dirty = false
    notify(`Saved ${editor.path}`)
    editor.open = false
    refresh()
  } catch (error) {
    editor.error = error.message || 'Save failed'
  } finally {
    editor.saving = false
  }
}

function confirmDelete(entry) {
  deleteDialog.path = entry.path
  deleteDialog.open = true
}

async function doDelete() {
  deleteDialog.busy = true
  try {
    await api.fs.remove(deleteDialog.path)
    notify(`Deleted ${deleteDialog.path}`)
    deleteDialog.open = false
    refresh()
  } catch (error) {
    notify(error.message || 'Delete failed', 'error')
  } finally {
    deleteDialog.busy = false
  }
}

function joinPath(dir, name) {
  const base = dir === '/' ? '' : dir
  return `${base}/${name}`
}

function openNameDialog(mode, entry = null) {
  nameDialog.mode = mode
  nameDialog.error = ''
  nameDialog.busy = false
  if (mode === 'rename') {
    nameDialog.title = `Rename ${entry.name}`
    nameDialog.value = entry.path
    nameDialog.original = entry.path
  } else {
    nameDialog.title = mode === 'folder' ? 'New folder' : 'New file'
    nameDialog.value = ''
    nameDialog.original = ''
  }
  nameDialog.open = true
}

async function submitNameDialog() {
  const raw = (nameDialog.value || '').trim()
  if (!raw) {
    nameDialog.error = 'A name is required'
    return
  }
  nameDialog.busy = true
  nameDialog.error = ''
  try {
    if (nameDialog.mode === 'rename') {
      const to = raw.startsWith('/') ? raw : joinPath(currentPath.value, raw)
      await api.fs.rename(nameDialog.original, to)
      notify(`Renamed to ${to}`)
    } else if (nameDialog.mode === 'folder') {
      const target = joinPath(currentPath.value, raw)
      await api.fs.mkdir(target)
      notify(`Created folder ${target}`)
    } else {
      const target = joinPath(currentPath.value, raw)
      // Seed new JSON files with {} so they pass the device's JSON validation.
      await api.fs.upload(target, target.endsWith('.json') ? '{}' : '')
      notify(`Created ${target}`)
    }
    nameDialog.open = false
    refresh()
  } catch (error) {
    nameDialog.error = error.message || 'Operation failed'
  } finally {
    nameDialog.busy = false
  }
}

function triggerUpload() {
  fileInput.value?.click()
}

async function onFilePicked(event) {
  const file = event.target.files?.[0]
  if (!file) return
  const base = currentPath.value === '/' ? '' : currentPath.value
  const target = `${base}/${file.name}`
  uploadBusy.value = true
  try {
    // Pass the File (a Blob) straight to fetch so raw bytes are sent unchanged —
    // file.text() would corrupt any non-UTF-8 binary (images, .gz, fonts).
    await api.fs.upload(target, file)
    notify(`Uploaded ${target}`)
    refresh()
  } catch (error) {
    notify(error.message || 'Upload failed', 'error')
  } finally {
    uploadBusy.value = false
    event.target.value = ''
  }
}

onMounted(refresh)
</script>

<style scoped>
.font-monospace :deep(textarea) {
  font-family: 'Roboto Mono', 'Consolas', monospace;
  font-size: 0.85rem;
}
</style>
