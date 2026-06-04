<template>
  <v-app>
    <v-navigation-drawer
      v-model="drawer"
      :rail="isDesktop ? rail : false"
      :permanent="isDesktop"
      :temporary="!isDesktop"
    >
      <v-list-item
        prepend-icon="mdi-layers"
        title="OpenFrame"
        nav
      >
        <template v-if="isDesktop" #append>
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
          @click="handleNavClick"
        />
      </v-list>
    </v-navigation-drawer>

    <v-app-bar elevation="1">
      <template #prepend>
        <v-app-bar-nav-icon
          v-if="!isDesktop"
          @click.stop="drawer = !drawer"
        />
      </template>
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
import { computed, ref, watch } from 'vue'
import { useDisplay } from 'vuetify'
import { useWebSocketStore } from './stores/websocket'

const drawer = ref(true)
const rail = ref(false)
const { mdAndUp } = useDisplay()
const isDesktop = computed(() => mdAndUp.value)

watch(isDesktop, (desktop) => {
  drawer.value = desktop
}, { immediate: true })

function handleNavClick() {
  if (!isDesktop.value) {
    drawer.value = false
  }
}

const wsStore = useWebSocketStore()
wsStore.connect()

const navItems = [
  { title: 'Dashboard',       icon: 'mdi-view-dashboard',         to: '/' },
  { title: 'Layout Designer', icon: 'mdi-developer-board',        to: '/layout' },
  { title: 'Screen Designer', icon: 'mdi-monitor-edit',           to: '/screens' },
  { title: 'Sensors',         icon: 'mdi-thermometer',            to: '/sensors' },
  { title: 'Actions',         icon: 'mdi-lightning-bolt',         to: '/actions' },
  { title: 'Modules',         icon: 'mdi-expansion-card',         to: '/modules' },
  { title: 'Home Assistant',  icon: 'mdi-home-assistant',         to: '/ha' },
  { title: 'Profiles',        icon: 'mdi-account-box-multiple',   to: '/profiles' },
  { title: 'Logs',            icon: 'mdi-text-box-outline',       to: '/logs' },
  { title: 'Settings',        icon: 'mdi-cog',                    to: '/settings' },
]
</script>
