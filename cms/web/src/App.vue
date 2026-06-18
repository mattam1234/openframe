<template>
  <v-app>
    <v-app-bar flat density="comfortable" border>
      <v-app-bar-nav-icon @click="drawer = !drawer" />
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

    <v-navigation-drawer v-model="drawer">
      <v-list density="compact" nav>
        <v-list-item
          v-for="link in links"
          :key="link.to"
          :to="link.to"
          :prepend-icon="link.icon"
          :title="link.title"
        >
          <template v-if="link.to === '/alerts' && alertCount" #append>
            <v-chip size="x-small" color="error" variant="flat">{{ alertCount }}</v-chip>
          </template>
        </v-list-item>
      </v-list>
    </v-navigation-drawer>

    <v-main>
      <v-container fluid class="pa-4">
        <router-view />
      </v-container>
    </v-main>
  </v-app>
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import { useFleet } from './store'

const drawer = ref(true)
const { connected, activeAlerts, init } = useFleet()
const alertCount = computed(() => activeAlerts.value.length)

const links = [
  { to: '/', title: 'Fleet', icon: 'mdi-view-grid' },
  { to: '/topology', title: 'Topology', icon: 'mdi-lan' },
  { to: '/templates', title: 'Templates', icon: 'mdi-file-document-multiple-outline' },
  { to: '/jobs', title: 'Jobs', icon: 'mdi-clock-outline' },
  { to: '/firmware', title: 'Firmware', icon: 'mdi-chip' },
  { to: '/alerts', title: 'Alerts', icon: 'mdi-bell-outline' },
  { to: '/provision', title: 'Provision', icon: 'mdi-qrcode' },
]

// One shared WebSocket for the whole app; the badge reflects its state.
onMounted(init)
</script>

<style scoped>
.brand { color: inherit; text-decoration: none; display: inline-flex; align-items: center; }
</style>
