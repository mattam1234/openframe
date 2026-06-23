<template>
  <v-table density="compact">
    <thead>
      <tr><th>Name</th><th>Value</th><th>Type</th></tr>
    </thead>
    <tbody>
      <tr v-for="v in rows" :key="v.id">
        <td class="mono">
          {{ v.name ?? v.id }}
          <v-tooltip v-if="v.readOnly" text="Read-only — mirrors live hardware state" location="top">
            <template #activator="{ props }">
              <v-icon v-bind="props" size="x-small" class="ml-1 text-medium-emphasis">mdi-lock-outline</v-icon>
            </template>
          </v-tooltip>
        </td>
        <td>{{ fmtValue(v) }}</td>
        <td class="text-medium-emphasis">{{ v.type ?? '' }}</td>
      </tr>
      <tr v-if="!rows.length">
        <td colspan="3" class="text-medium-emphasis">{{ empty || 'No variables.' }}</td>
      </tr>
    </tbody>
  </v-table>
</template>

<script setup>
defineProps({
  rows: { type: Array, default: () => [] },
  empty: { type: String, default: '' },
})

function fmtValue(v) {
  if (v == null) return '—'
  const val = typeof v.value === 'boolean' ? (v.value ? 'true' : 'false') : String(v.value ?? '')
  return v.unit ? `${val} ${v.unit}` : val
}
</script>

<style scoped>
.mono { font-family: 'Roboto Mono', 'Consolas', monospace; }
</style>
