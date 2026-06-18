<template>
  <v-app>
    <v-app-bar flat density="comfortable" border>
      <v-app-bar-title>
        <router-link to="/" class="brand">
          <v-icon class="mr-2" color="primary">mdi-view-grid</v-icon>
          OpenFrame Fleet
        </router-link>
      </v-app-bar-title>
      <template #append>
        <v-chip :color="connected ? 'success' : 'error'" size="small" variant="flat" class="mr-2">
          <v-icon start size="14">{{ connected ? 'mdi-lan-connect' : 'mdi-lan-disconnect' }}</v-icon>
          {{ connected ? 'live' : 'reconnecting…' }}
        </v-chip>
      </template>
    </v-app-bar>

    <v-main>
      <v-container fluid class="pa-4">
        <router-view />
      </v-container>
    </v-main>
  </v-app>
</template>

<script setup>
import { onMounted } from 'vue'
import { useFleet } from './store'

// One shared WebSocket for the whole app; the badge reflects its state.
const { connected, init } = useFleet()
onMounted(init)
</script>

<style scoped>
.brand { color: inherit; text-decoration: none; display: inline-flex; align-items: center; }
</style>
