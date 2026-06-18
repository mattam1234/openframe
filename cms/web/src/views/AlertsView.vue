<template>
  <div>
    <v-card border flat class="mb-4">
      <v-card-title class="text-subtitle-1">Active alerts</v-card-title>
      <v-table density="compact">
        <thead>
          <tr><th></th><th>Device</th><th>Type</th><th>Message</th><th>Raised</th></tr>
        </thead>
        <tbody>
          <tr v-for="a in activeSorted" :key="a.id">
            <td><span class="dot" :class="a.severity === 'critical' ? 'crit' : 'warn'" /></td>
            <td><router-link :to="{ name: 'device', params: { id: a.deviceId } }" class="devlink">{{ a.deviceId }}</router-link></td>
            <td>{{ a.type }}</td>
            <td>{{ a.message }}</td>
            <td>{{ fmtTs(a.raisedAt) }}</td>
          </tr>
          <tr v-if="!activeSorted.length"><td colspan="5" class="text-medium-emphasis">No active alerts.</td></tr>
        </tbody>
      </v-table>
    </v-card>

    <v-card border flat>
      <v-card-title class="text-subtitle-1">Recent</v-card-title>
      <v-table density="compact">
        <thead>
          <tr><th></th><th>Device</th><th>Type</th><th>Message</th><th>Raised</th><th>Resolved</th></tr>
        </thead>
        <tbody>
          <tr v-for="a in recent" :key="a.id + a.raisedAt" :class="{ 'text-medium-emphasis': a.resolvedAt }">
            <td><span class="dot" :class="a.severity === 'critical' ? 'crit' : 'warn'" /></td>
            <td><router-link :to="{ name: 'device', params: { id: a.deviceId } }" class="devlink">{{ a.deviceId }}</router-link></td>
            <td>{{ a.type }}</td>
            <td>{{ a.message }}</td>
            <td>{{ fmtTs(a.raisedAt) }}</td>
            <td>{{ a.resolvedAt ? fmtTs(a.resolvedAt) : '—' }}</td>
          </tr>
          <tr v-if="!recent.length"><td colspan="6" class="text-medium-emphasis">No recent alerts.</td></tr>
        </tbody>
      </v-table>
    </v-card>
  </div>
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import api from '../api'
import { useFleet } from '../store'

const { activeAlerts } = useFleet()
const recent = ref([])

const activeSorted = computed(() => [...activeAlerts.value].sort((a, b) => b.raisedAt - a.raisedAt))
const fmtTs = (t) => (t ? new Date(t).toLocaleString() : '—')

onMounted(async () => {
  try { recent.value = (await api.get('/api/alerts')).recent || [] } catch { /* optional */ }
})
</script>

<style scoped>
.dot { display: inline-block; width: 9px; height: 9px; border-radius: 50%; }
.dot.crit { background: #ef4444; }
.dot.warn { background: #f59e0b; }
.devlink { color: rgb(var(--v-theme-primary)); text-decoration: none; }
.devlink:hover { text-decoration: underline; }
</style>
