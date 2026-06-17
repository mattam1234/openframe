<template>
  <v-app>
    <a href="#main-content" class="skip-link">Skip to main content</a>
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
          :title="item.key ? $t(item.key) : item.title"
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
        <router-link to="/" class="text-decoration-none text-high-emphasis">
          OpenFrame
        </router-link>
      </v-app-bar-title>
      <template #append>
        <v-btn
          :icon="isDark ? 'mdi-weather-sunny' : 'mdi-weather-night'"
          variant="text"
          :title="isDark ? 'Switch to light theme' : 'Switch to dark theme'"
          @click="toggleTheme"
        />
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
      <v-container id="main-content" fluid tabindex="-1">
        <router-view />
      </v-container>
    </v-main>

    <CommandPalette />
  </v-app>
</template>

<script setup>
import { computed, ref, watch } from 'vue'
import { useDisplay, useTheme } from 'vuetify'
import { useWebSocketStore } from './stores/websocket'
import CommandPalette from './components/CommandPalette.vue'

// Dark/light theme toggle (#46), persisted to localStorage.
const theme = useTheme()
const isDark = computed(() => theme.global.current.value.dark)
function toggleTheme() {
  const next = isDark.value ? 'light' : 'dark'
  theme.global.name.value = next
  try { localStorage.setItem('of-theme', next) } catch { /* private mode */ }
}

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
  { key: 'nav.dashboard', icon: 'mdi-view-dashboard',         to: '/' },
  { key: 'nav.layout',    icon: 'mdi-developer-board',        to: '/layout' },
  { key: 'nav.screens',   icon: 'mdi-monitor-edit',           to: '/screens' },
  { key: 'nav.sensors',   icon: 'mdi-thermometer',            to: '/sensors' },
  { key: 'nav.outputs',   icon: 'mdi-led-strip-variant',      to: '/outputs' },
  { key: 'nav.actions',   icon: 'mdi-lightning-bolt',         to: '/actions' },
  { key: 'nav.modules',   icon: 'mdi-expansion-card',         to: '/modules' },
  { key: 'nav.ha',        icon: 'mdi-home-assistant',         to: '/ha' },
  { key: 'nav.profiles',  icon: 'mdi-account-box-multiple',   to: '/profiles' },
  { key: 'nav.logs',      icon: 'mdi-text-box-outline',       to: '/logs' },
  { key: 'nav.events',    icon: 'mdi-pulse',                  to: '/events' },
  { key: 'nav.files',     icon: 'mdi-folder-cog-outline',     to: '/files' },
  { key: 'nav.settings',  icon: 'mdi-cog',                    to: '/settings' },
  { key: 'nav.setup',     icon: 'mdi-rocket-launch',          to: '/setup' },
]
</script>

<style>
/* Accessibility (#55): keyboard skip-link, visible only when focused. */
.skip-link {
  position: absolute;
  left: 8px;
  top: -48px;
  z-index: 3000;
  padding: 8px 14px;
  border-radius: 4px;
  background: rgb(var(--v-theme-primary));
  color: rgb(var(--v-theme-on-primary));
  transition: top 0.15s ease;
}
.skip-link:focus { top: 8px; }
#main-content:focus { outline: none; }

/* A clearly visible focus ring for keyboard users (not on mouse click). */
*:focus-visible {
  outline: 2px solid rgb(var(--v-theme-primary));
  outline-offset: 2px;
}
</style>
