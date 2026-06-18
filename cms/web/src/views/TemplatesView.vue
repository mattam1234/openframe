<template>
  <v-row>
    <v-col cols="12" md="5">
      <v-card border flat>
        <v-card-title class="text-subtitle-1">{{ form.id ? `Edit: ${form.name}` : 'New template' }}</v-card-title>
        <v-card-text>
          <v-text-field v-model="form.name" label="Name" density="compact" variant="outlined" class="mb-2" hide-details />
          <v-text-field v-model="form.description" label="Description" density="compact" variant="outlined" class="mb-2" hide-details />
          <v-text-field v-model="form.type" label="Command type" placeholder="apply_config" density="compact" variant="outlined" class="mb-2" hide-details />
          <v-textarea v-model="form.payload" label="Payload JSON (optional)" rows="6" density="compact" variant="outlined" class="mb-2 mono" hide-details />
          <div class="d-flex align-center ga-2">
            <v-btn color="primary" @click="save">Save</v-btn>
            <v-btn variant="text" @click="reset">Reset</v-btn>
            <span class="text-caption text-medium-emphasis">{{ msg }}</span>
          </div>
        </v-card-text>
      </v-card>
    </v-col>

    <v-col cols="12" md="7">
      <v-card border flat>
        <v-card-title class="text-subtitle-1">Templates</v-card-title>
        <v-table density="compact">
          <tbody>
            <tr v-for="t in templates" :key="t.id">
              <th>
                {{ t.name }}<br>
                <span class="mono text-medium-emphasis">{{ t.command.type }}</span>
              </th>
              <td>
                <div v-if="t.description" class="text-caption mb-1">{{ t.description }}</div>
                <v-btn size="x-small" variant="tonal" class="mr-1" @click="edit(t)">Edit</v-btn>
                <v-btn size="x-small" variant="text" color="error" @click="remove(t.id)">Delete</v-btn>
              </td>
            </tr>
            <tr v-if="!templates.length"><td class="text-medium-emphasis">No templates yet.</td></tr>
          </tbody>
        </v-table>
      </v-card>
    </v-col>
  </v-row>
</template>

<script setup>
import { onMounted, reactive, ref } from 'vue'
import api from '../api'

const templates = ref([])
const form = reactive({ id: '', name: '', description: '', type: '', payload: '' })
const msg = ref('')

function reset() {
  Object.assign(form, { id: '', name: '', description: '', type: '', payload: '' })
  msg.value = ''
}
function edit(t) {
  Object.assign(form, {
    id: t.id, name: t.name, description: t.description || '', type: t.command.type,
    payload: t.command.payload == null ? '' : JSON.stringify(t.command.payload, null, 2),
  })
  msg.value = ''
}

async function load() {
  try { templates.value = (await api.get('/api/templates')).templates || [] } catch { /* ignore */ }
}

async function save() {
  if (!form.name.trim() || !form.type.trim()) { msg.value = 'name and command type are required'; return }
  let payload = null
  if (form.payload.trim()) {
    try { payload = JSON.parse(form.payload) } catch (e) { msg.value = `invalid payload JSON: ${e.message}`; return }
  }
  const body = { name: form.name.trim(), description: form.description.trim() || undefined, command: { type: form.type.trim(), payload } }
  try {
    const saved = form.id
      ? await api.put(`/api/templates/${encodeURIComponent(form.id)}`, body)
      : await api.post('/api/templates', body)
    form.id = saved.id
    msg.value = 'saved ✓'
    load()
  } catch (e) { msg.value = `error: ${e.message}` }
}

async function remove(id) {
  try {
    await fetch(`/api/templates/${encodeURIComponent(id)}`, { method: 'DELETE' })
    if (form.id === id) reset()
    load()
  } catch { /* ignore */ }
}

onMounted(load)
</script>

<style scoped>
.mono :deep(textarea) { font-family: 'Roboto Mono', monospace; font-size: 0.85rem; }
.mono { font-family: 'Roboto Mono', monospace; }
</style>
