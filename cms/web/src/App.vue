<template>
  <v-app>
    <v-app-bar flat density="comfortable" border>
      <v-app-bar-nav-icon v-if="!needLogin" @click="drawer = !drawer" />
      <v-app-bar-title>
        <router-link to="/" class="brand">
          <v-icon class="mr-2" color="primary">mdi-view-grid</v-icon>
          OpenFrame Fleet
        </router-link>
      </v-app-bar-title>
      <template #append>
        <v-chip v-if="!needLogin" :color="connected ? 'success' : 'error'" size="small" variant="flat" class="mr-2">
          <v-icon start size="14">{{ connected ? 'mdi-lan-connect' : 'mdi-lan-disconnect' }}</v-icon>
          {{ connected ? 'live' : 'reconnecting…' }}
        </v-chip>
        <v-btn v-if="canLogout" size="small" variant="text" prepend-icon="mdi-logout" @click="logout">Sign out</v-btn>
      </template>
    </v-app-bar>

    <v-navigation-drawer v-if="!needLogin" v-model="drawer">
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
        <router-view v-if="!needLogin" />
      </v-container>
    </v-main>

    <!-- Login gate: shown when the CMS requires a token and we have no session. -->
    <v-dialog :model-value="needLogin" persistent max-width="380">
      <v-card>
        <v-card-title class="text-h6">OpenFrame CMS</v-card-title>
        <v-card-text>
          <v-form @submit.prevent="login">
            <v-text-field
              v-model="token"
              type="password"
              label="Access token"
              autocomplete="current-password"
              variant="outlined"
              autofocus
              :error="loginErr"
              :error-messages="loginErr ? 'Invalid token' : ''"
            />
            <v-btn type="submit" color="primary" block :loading="signingIn">Sign in</v-btn>
          </v-form>
        </v-card-text>
      </v-card>
    </v-dialog>
  </v-app>
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import api from './api'
import { useFleet } from './store'

const drawer = ref(true)
const { connected, activeAlerts, init } = useFleet()
const alertCount = computed(() => activeAlerts.value.length)

const needLogin = ref(false)
const canLogout = ref(false)
const token = ref('')
const loginErr = ref(false)
const signingIn = ref(false)

const links = [
  { to: '/', title: 'Fleet', icon: 'mdi-view-grid' },
  { to: '/topology', title: 'Topology', icon: 'mdi-lan' },
  { to: '/templates', title: 'Templates', icon: 'mdi-file-document-multiple-outline' },
  { to: '/jobs', title: 'Jobs', icon: 'mdi-clock-outline' },
  { to: '/firmware', title: 'Firmware', icon: 'mdi-chip' },
  { to: '/alerts', title: 'Alerts', icon: 'mdi-bell-outline' },
  { to: '/provision', title: 'Provision', icon: 'mdi-qrcode' },
]

// Gate on auth before opening the WebSocket — an unauthorized WS upgrade is
// rejected by the server. If the API is unreachable, proceed and let the views
// surface their own errors (matches the vanilla behaviour).
async function checkAuth() {
  let state
  try { state = await api.get('/api/auth') } catch { init(); return }
  canLogout.value = !!state.authRequired
  if (state.authRequired && !state.authed) { needLogin.value = true; return }
  init()
}

async function login() {
  signingIn.value = true
  loginErr.value = false
  try {
    await api.post('/api/login', { token: token.value })
    location.reload() // simplest: re-run with the session cookie set
  } catch {
    loginErr.value = true
  } finally {
    signingIn.value = false
  }
}

async function logout() {
  try { await api.post('/api/logout') } catch { /* ignore */ }
  location.reload()
}

onMounted(checkAuth)
</script>

<style scoped>
.brand { color: inherit; text-decoration: none; display: inline-flex; align-items: center; }
</style>
