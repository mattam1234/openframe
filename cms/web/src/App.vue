<template>
  <v-app>
    <v-app-bar flat density="comfortable" border>
      <v-app-bar-title>
        <v-icon class="mr-2" color="primary">mdi-view-grid</v-icon>
        OpenFrame Fleet
      </v-app-bar-title>
      <template #append>
        <v-chip
          :color="connected ? 'success' : 'error'"
          size="small"
          variant="flat"
          class="mr-2"
        >
          <v-icon start size="14">{{ connected ? 'mdi-lan-connect' : 'mdi-lan-disconnect' }}</v-icon>
          {{ connected ? 'live' : 'reconnecting…' }}
        </v-chip>
      </template>
    </v-app-bar>

    <v-main>
      <v-container fluid class="pa-4">
        <FleetView v-model:connected="connected" />
      </v-container>
    </v-main>
  </v-app>
</template>

<script setup>
import { ref } from 'vue'
import FleetView from './views/FleetView.vue'

// Surfaced by the fleet view's WebSocket; shown in the app bar.
const connected = ref(false)
</script>
