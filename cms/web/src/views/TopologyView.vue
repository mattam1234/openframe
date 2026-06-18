<template>
  <v-card border flat>
    <v-card-title class="text-subtitle-1">Topology</v-card-title>
    <v-card-text>
      <p v-if="!devices.length" class="text-medium-emphasis">
        No devices yet. Direct WiFi nodes and their ESP-NOW leaves appear here.
      </p>
      <div v-else class="topo">
        <div class="topo-root">◉ MQTT broker / CMS</div>
        <template v-for="n in topology.nodes" :key="n.device.deviceId">
          <div class="topo-node">
            {{ n.leaves.length ? '◆' : '●' }}
            <span class="dot" :class="n.device.online ? 'online' : 'offline'" />
            <router-link :to="{ name: 'device', params: { id: n.device.deviceId } }" class="devlink">
              {{ n.device.name || n.device.deviceId }}
            </router-link>
            <span class="meta">{{ meta(n.device) }}</span>
            <v-chip v-if="n.leaves.length" size="x-small" label class="ml-1">gateway</v-chip>
          </div>
          <div v-for="leaf in n.leaves" :key="leaf.deviceId" class="topo-leaf">
            <span class="dot" :class="leaf.online ? 'online' : 'offline'" />
            <router-link :to="{ name: 'device', params: { id: leaf.deviceId } }" class="devlink">
              {{ leaf.name || leaf.deviceId }}
            </router-link>
            <v-chip size="x-small" label class="ml-1">esp-now</v-chip>
          </div>
        </template>
        <template v-for="o in topology.orphans" :key="o.via">
          <div class="topo-node">◆ <span class="meta">unknown gateway {{ o.via }}</span></div>
          <div v-for="leaf in o.leaves" :key="leaf.deviceId" class="topo-leaf">
            <span class="dot" :class="leaf.online ? 'online' : 'offline'" />
            <router-link :to="{ name: 'device', params: { id: leaf.deviceId } }" class="devlink">
              {{ leaf.name || leaf.deviceId }}
            </router-link>
            <v-chip size="x-small" label class="ml-1">esp-now</v-chip>
          </div>
        </template>
      </div>
    </v-card-text>
  </v-card>
</template>

<script setup>
import { computed } from 'vue'
import { useFleet } from '../store'

const { devicesInSite: devices } = useFleet()

const meta = (d) => [d.board, d.ip].filter(Boolean).join(', ')

// Group devices into broker → {direct nodes, gateways→leaves}, plus leaves whose
// gateway isn't a known device. (Ported from the vanilla topology view.)
const topology = computed(() => {
  const list = devices.value
  const byGateway = new Map()
  const direct = []
  for (const d of list) {
    if (d.via) {
      if (!byGateway.has(d.via)) byGateway.set(d.via, [])
      byGateway.get(d.via).push(d)
    } else { direct.push(d) }
  }
  const known = new Set(list.map((d) => d.deviceId))
  const byId = (a, b) => a.deviceId.localeCompare(b.deviceId)
  const nodes = direct.sort(byId).map((d) => ({ device: d, leaves: (byGateway.get(d.deviceId) || []).sort(byId) }))
  const orphans = [...byGateway.entries()]
    .filter(([gw]) => !known.has(gw))
    .map(([via, leaves]) => ({ via, leaves: leaves.sort(byId) }))
  return { nodes, orphans }
})
</script>

<style scoped>
.topo { font-size: 0.95rem; line-height: 2; font-family: 'Roboto Mono', monospace; }
.topo-node { margin-left: 1rem; }
.topo-leaf { margin-left: 3rem; opacity: 0.92; }
.dot { display: inline-block; width: 9px; height: 9px; border-radius: 50%; margin: 0 4px; }
.dot.online { background: #22c55e; }
.dot.offline { background: #6b7280; }
.devlink { color: rgb(var(--v-theme-primary)); text-decoration: none; }
.devlink:hover { text-decoration: underline; }
.meta { color: rgba(var(--v-theme-on-surface), 0.6); font-size: 0.8rem; }
</style>
