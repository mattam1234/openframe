<template>
  <v-dialog v-model="open" max-width="540" @afterLeave="reset">
    <v-card>
      <v-text-field
        ref="inputRef"
        v-model="query"
        autofocus
        placeholder="Jump to… (↑↓ to move, Enter to go, Esc to close)"
        prepend-inner-icon="mdi-magnify"
        variant="solo"
        hide-details
        @keydown.down.prevent="move(1)"
        @keydown.up.prevent="move(-1)"
        @keydown.enter.prevent="run(filtered[cursor])"
        @keydown.esc="open = false"
      />
      <v-list density="compact" max-height="360" class="overflow-y-auto">
        <v-list-item
          v-for="(cmd, i) in filtered"
          :key="cmd.to"
          :active="i === cursor"
          @click="run(cmd)"
          @mouseenter="cursor = i"
          @focusin="cursor = i"
        >
          <template #prepend><v-icon :icon="cmd.icon" /></template>
          <v-list-item-title>{{ cmd.label }}</v-list-item-title>
        </v-list-item>
        <v-list-item v-if="!filtered.length" class="text-medium-emphasis">No matches</v-list-item>
      </v-list>
    </v-card>
  </v-dialog>
</template>

<script setup>
import { computed, onMounted, onUnmounted, ref, watch } from 'vue'
import { useRouter } from 'vue-router'

const router = useRouter()
const open = ref(false)
const query = ref('')
const cursor = ref(0)

// Friendly labels/icons for the named routes; anything unmapped still appears by name.
const meta = {
  dashboard: { label: 'Dashboard', icon: 'mdi-view-dashboard' },
  layout: { label: 'Layout Designer', icon: 'mdi-pencil-ruler' },
  screens: { label: 'Screen Designer', icon: 'mdi-monitor' },
  sensors: { label: 'Sensors', icon: 'mdi-thermometer' },
  variables: { label: 'Variables', icon: 'mdi-variable' },
  outputs: { label: 'Outputs', icon: 'mdi-power-plug' },
  actions: { label: 'Action Manager', icon: 'mdi-cog-transfer' },
  modules: { label: 'Modules', icon: 'mdi-puzzle' },
  ha: { label: 'Home Assistant', icon: 'mdi-home-assistant' },
  profiles: { label: 'Profiles', icon: 'mdi-account-switch' },
  logs: { label: 'Logs', icon: 'mdi-console' },
  events: { label: 'Event Inspector', icon: 'mdi-pulse' },
  files: { label: 'Filesystem', icon: 'mdi-folder' },
  settings: { label: 'Settings', icon: 'mdi-cog' },
  setup: { label: 'Setup Wizard', icon: 'mdi-rocket-launch' },
}

const commands = router.getRoutes()
  .filter((r) => r.name && meta[r.name])
  .map((r) => ({ to: r.path, label: meta[r.name].label, icon: meta[r.name].icon }))

const filtered = computed(() => {
  const q = query.value.trim().toLowerCase()
  if (!q) return commands
  return commands.filter((c) => c.label.toLowerCase().includes(q))
})

watch(filtered, () => { cursor.value = 0 })

function move(delta) {
  const n = filtered.value.length
  if (!n) return
  cursor.value = (cursor.value + delta + n) % n
}

function run(cmd) {
  if (!cmd) return
  open.value = false
  if (router.currentRoute.value.path !== cmd.to) router.push(cmd.to)
}

function reset() {
  query.value = ''
  cursor.value = 0
}

function onKey(e) {
  if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'k') {
    e.preventDefault()
    open.value = !open.value
  }
}

onMounted(() => window.addEventListener('keydown', onKey))
onUnmounted(() => window.removeEventListener('keydown', onKey))
</script>
