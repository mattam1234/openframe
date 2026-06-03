<template>
  <v-app>
    <v-navigation-drawer v-model="drawer" :rail="rail" permanent>
      <v-list-item
        prepend-icon="mdi-layers"
        title="OpenFrame"
        nav
      >
        <template #append>
          <v-btn
            :icon="rail ? 'mdi-chevron-right' : 'mdi-chevron-left'"
            variant="text"
            @click="rail = !rail"
          />
        </template>
      </v-list-item>

      <v-divider />

      <v-list density="compact" nav>
        <v-list-item
          v-for="item in navItems"
          :key="item.to"
          :prepend-icon="item.icon"
          :title="item.title"
          :to="item.to"
        />
      </v-list>
    </v-navigation-drawer>

    <v-app-bar elevation="1">
      <v-app-bar-title>
        <router-link to="/" class="text-decoration-none text-white">
          OpenFrame
        </router-link>
      </v-app-bar-title>
      <template #append>
        <v-chip
          :color="wsStore.connected ? 'success' : 'error'"
          size="small"
          class="mr-2"
        >
          {{ wsStore.connected ? 'Connected' : 'Offline' }}
        </v-chip>
      </template>
    </v-app-bar>

    <v-main>
      <v-container fluid>
        <router-view />
      </v-container>
    </v-main>
  </v-app>
</template>

<script setup>
import { ref } from 'vue'
import { useWebSocketStore } from './stores/websocket'

const drawer = ref(true)
const rail = ref(false)
const wsStore = useWebSocketStore()
wsStore.connect()

const navItems = [
  { title: 'Dashboard',       icon: 'mdi-view-dashboard',  to: '/' },
  { title: 'Layout Designer', icon: 'mdi-developer-board', to: '/layout' },
  { title: 'Screen Designer', icon: 'mdi-monitor-edit',    to: '/screens' },
  { title: 'Sensors',         icon: 'mdi-thermometer',     to: '/sensors' },
  { title: 'Actions',         icon: 'mdi-lightning-bolt',  to: '/actions' },
  { title: 'Modules',         icon: 'mdi-expansion-card',  to: '/modules' },
  { title: 'Home Assistant',  icon: 'mdi-home-assistant',  to: '/ha' },
  { title: 'Logs',            icon: 'mdi-text-box-outline', to: '/logs' },
  { title: 'Settings',        icon: 'mdi-cog',             to: '/settings' },
]
</script>
