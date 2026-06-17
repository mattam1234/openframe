<template>
  <div>
    <h1 class="text-h5 mb-6">
      <v-icon class="mr-2">mdi-account-box-multiple</v-icon>
      Profiles &amp; Templates
    </h1>

    <v-row>
      <!-- ── Profiles ──────────────────────────────────────────────────────── -->
      <v-col cols="12" md="6">
        <v-card>
          <v-card-title class="d-flex align-center justify-space-between">
            <span>Profiles</span>
            <v-btn color="primary" size="small" prepend-icon="mdi-plus" @click="openCreateDialog">
              New Profile
            </v-btn>
          </v-card-title>

          <v-card-subtitle class="text-caption pb-2">
            Profiles capture actions, macros, variables, and layout config. Switching a profile
            reloads actions, macros, and variables immediately. Hardware layout changes (inputs,
            outputs, displays) take effect on next boot.
          </v-card-subtitle>

          <v-divider />

          <v-list v-if="profiles.length">
            <v-list-item
              v-for="profile in profiles"
              :key="profile.id"
              :title="profile.name"
              :subtitle="profile.active ? 'Active' : formatDate(profile.createdMs)"
            >
              <template #prepend>
                <v-icon :color="profile.active ? 'success' : 'default'">
                  {{ profile.active ? 'mdi-check-circle' : 'mdi-circle-outline' }}
                </v-icon>
              </template>
              <template #append>
                <v-btn
                  v-if="!profile.active"
                  size="small"
                  variant="tonal"
                  color="primary"
                  class="mr-2"
                  :loading="activatingId === profile.id"
                  @click="activateProfile(profile.id)"
                >
                  Activate
                </v-btn>
                <v-chip v-else size="small" color="success" class="mr-2">Active</v-chip>
                <v-btn
                  icon="mdi-delete"
                  size="small"
                  variant="text"
                  color="error"
                  aria-label="Delete profile"
                  :disabled="profile.active"
                  @click="confirmDelete(profile)"
                />
              </template>
            </v-list-item>
          </v-list>

          <v-card-text v-else class="text-center text-medium-emphasis py-8">
            No profiles yet. Create one to save the current device state.
          </v-card-text>
        </v-card>
      </v-col>

      <!-- ── Templates ────────────────────────────────────────────────────── -->
      <v-col cols="12" md="6">
        <v-card>
          <v-card-title class="d-flex align-center justify-space-between">
            <span>Templates</span>
            <v-btn color="secondary" size="small" prepend-icon="mdi-export" @click="openExportDialog">
              Export
            </v-btn>
          </v-card-title>

          <v-card-subtitle class="text-caption pb-2">
            Templates bundle a full profile snapshot plus hardware config for sharing between devices.
            Import a template JSON to apply it to this device.
          </v-card-subtitle>

          <v-divider />

          <v-list v-if="templates.length">
            <v-list-item
              v-for="tmpl in templates"
              :key="tmpl.id"
              :title="tmpl.name"
              :subtitle="formatDate(tmpl.createdMs)"
            >
              <template #prepend>
                <v-icon color="secondary">mdi-file-export</v-icon>
              </template>
              <template #append>
                <v-btn
                  size="small"
                  variant="tonal"
                  color="secondary"
                  class="mr-2"
                  :loading="importingId === tmpl.id"
                  @click="importTemplate(tmpl.id)"
                >
                  Apply
                </v-btn>
                <v-btn
                  icon="mdi-delete"
                  size="small"
                  variant="text"
                  color="error"
                  aria-label="Delete template"
                  @click="deleteTemplate(tmpl.id)"
                />
              </template>
            </v-list-item>
          </v-list>

          <v-card-text v-else class="text-center text-medium-emphasis py-8">
            No templates. Export the current device state as a template to share it.
          </v-card-text>

          <v-divider />
          <v-card-actions>
            <v-btn prepend-icon="mdi-import" variant="tonal" @click="openImportDialog">
              Import Template JSON
            </v-btn>
          </v-card-actions>
        </v-card>
      </v-col>
    </v-row>

    <!-- ── Create Profile Dialog ──────────────────────────────────────────── -->
    <v-dialog v-model="createDialog" max-width="400">
      <v-card title="Create Profile">
        <v-card-text>
          <v-text-field
            v-model="newProfileName"
            label="Profile Name"
            placeholder="e.g. Work Setup"
            autofocus
            @keyup.enter="createProfile"
          />
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn @click="createDialog = false">Cancel</v-btn>
          <v-btn color="primary" :loading="creating" @click="createProfile">Create</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>

    <!-- ── Export Template Dialog ─────────────────────────────────────────── -->
    <v-dialog v-model="exportDialog" max-width="400">
      <v-card title="Export Template">
        <v-card-text>
          <v-text-field
            v-model="exportName"
            label="Template Name"
            placeholder="e.g. My OpenFrame v1"
            autofocus
            @keyup.enter="exportTemplate"
          />
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn @click="exportDialog = false">Cancel</v-btn>
          <v-btn color="secondary" :loading="exporting" @click="exportTemplate">Export</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>

    <!-- ── Import Template Dialog ─────────────────────────────────────────── -->
    <v-dialog v-model="importDialog" max-width="500">
      <v-card title="Import Template JSON">
        <v-card-text>
          <p class="text-body-2 mb-3 text-medium-emphasis">
            Paste the JSON content of a previously exported template. This will overwrite the
            current device configuration.
          </p>
          <v-textarea
            v-model="importJson"
            label="Template JSON"
            rows="8"
            font-family="monospace"
            :error-messages="importError ? [importError] : []"
          />
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn @click="importDialog = false">Cancel</v-btn>
          <v-btn color="warning" :loading="importing" @click="submitImport">Apply Template</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>

    <!-- ── Delete Confirm Dialog ──────────────────────────────────────────── -->
    <v-dialog v-model="deleteDialog" max-width="360">
      <v-card title="Delete Profile">
        <v-card-text>
          Are you sure you want to delete <strong>{{ deletingProfile?.name }}</strong>?
          This cannot be undone.
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn @click="deleteDialog = false">Cancel</v-btn>
          <v-btn color="error" :loading="deleting" @click="doDelete">Delete</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>

    <v-snackbar v-model="snack.show" :color="snack.color" timeout="3000">
      {{ snack.message }}
    </v-snackbar>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import api from '../api/client'

const profiles = ref([])
const templates = ref([])

const createDialog = ref(false)
const newProfileName = ref('')
const creating = ref(false)
const activatingId = ref(null)
const importingId = ref(null)

const exportDialog = ref(false)
const exportName = ref('')
const exporting = ref(false)

const importDialog = ref(false)
const importJson = ref('')
const importError = ref('')
const importing = ref(false)

const deleteDialog = ref(false)
const deletingProfile = ref(null)
const deleting = ref(false)

const snack = ref({ show: false, message: '', color: 'success' })

function showSnack(message, color = 'success') {
  snack.value = { show: true, message, color }
}

function formatDate(ms) {
  if (!ms) return ''
  return new Date(ms).toLocaleString()
}

async function loadProfiles() {
  try {
    const data = await api.get('/api/profiles')
    profiles.value = data.profiles || []
  } catch (e) {
    showSnack('Failed to load profiles', 'error')
  }
}

async function loadTemplates() {
  try {
    const data = await api.get('/api/templates')
    templates.value = data.templates || []
  } catch (e) {
    showSnack('Failed to load templates', 'error')
  }
}

function openCreateDialog() {
  newProfileName.value = ''
  createDialog.value = true
}

async function createProfile() {
  if (!newProfileName.value.trim()) return
  creating.value = true
  try {
    await api.post('/api/profiles', { name: newProfileName.value.trim() })
    createDialog.value = false
    await loadProfiles()
    showSnack('Profile created')
  } catch (e) {
    showSnack('Failed to create profile: ' + e.message, 'error')
  } finally {
    creating.value = false
  }
}

async function activateProfile(id) {
  activatingId.value = id
  try {
    await api.post('/api/profiles/activate', { id })
    await loadProfiles()
    showSnack('Profile activated')
  } catch (e) {
    showSnack('Failed to activate profile: ' + e.message, 'error')
  } finally {
    activatingId.value = null
  }
}

function confirmDelete(profile) {
  deletingProfile.value = profile
  deleteDialog.value = true
}

async function doDelete() {
  if (!deletingProfile.value) return
  deleting.value = true
  try {
    await api.delete('/api/profiles/' + deletingProfile.value.id)
    deleteDialog.value = false
    await loadProfiles()
    showSnack('Profile deleted')
  } catch (e) {
    showSnack('Failed to delete profile: ' + e.message, 'error')
  } finally {
    deleting.value = false
    deletingProfile.value = null
  }
}

function openExportDialog() {
  exportName.value = 'Template ' + new Date().toISOString().split('T')[0]
  exportDialog.value = true
}

async function exportTemplate() {
  if (!exportName.value.trim()) return
  exporting.value = true
  try {
    await api.post('/api/templates/export', { name: exportName.value.trim() })
    exportDialog.value = false
    await loadTemplates()
    showSnack('Template exported')
  } catch (e) {
    showSnack('Failed to export template: ' + e.message, 'error')
  } finally {
    exporting.value = false
  }
}

async function importTemplate(id) {
  importingId.value = id
  try {
    // Fetch the full template JSON from the device (templates are stored on device)
    const tmpl = await api.get('/api/templates/' + id)
    await api.post('/api/templates/import', tmpl)
    showSnack('Template applied')
  } catch (e) {
    showSnack('Failed to apply template: ' + e.message, 'error')
  } finally {
    importingId.value = null
  }
}

async function deleteTemplate(id) {
  try {
    await api.delete('/api/templates/' + id)
    await loadTemplates()
    showSnack('Template deleted')
  } catch (e) {
    showSnack('Failed to delete template: ' + e.message, 'error')
  }
}

function openImportDialog() {
  importJson.value = ''
  importError.value = ''
  importDialog.value = true
}

async function submitImport() {
  importError.value = ''
  let parsed
  try {
    parsed = JSON.parse(importJson.value)
  } catch {
    importError.value = 'Invalid JSON'
    return
  }
  importing.value = true
  try {
    await api.post('/api/templates/import', parsed)
    importDialog.value = false
    showSnack('Template applied successfully')
  } catch (e) {
    importError.value = e.message
  } finally {
    importing.value = false
  }
}

onMounted(() => {
  loadProfiles()
  loadTemplates()
})
</script>
