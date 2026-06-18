// Pure fleet helpers, extracted from the views so they can be unit-tested.

// Scope a device list to a selected site ('' / falsy = all sites). (#66)
export function filterBySite(devices, site) {
  if (!site) return devices
  return devices.filter((d) => d.site === site)
}

// Distinct, sorted site labels present in the fleet.
export function sitesOf(devices) {
  return [...new Set(devices.map((d) => d.site).filter(Boolean))].sort()
}

// Bulk-target precedence (mirrors the server's resolveTargets):
// explicit selection → tag filter → whole fleet. The online-only flag applies
// to the tag/fleet cases, not to an explicit id selection.
export function resolveTargetSpec({ selectedIds = [], tags = [], onlineOnly = false } = {}) {
  if (selectedIds.length) return { deviceIds: [...selectedIds] }
  if (tags.length) return { tags: [...tags], onlineOnly }
  return { onlineOnly }
}

// Group devices into the broker → {direct nodes, gateways→leaves} topology, plus
// leaves whose gateway isn't (yet) a known device. (ports topology.js)
export function buildTopology(list) {
  const byGateway = new Map()
  const direct = []
  for (const d of list) {
    if (d.via) {
      if (!byGateway.has(d.via)) byGateway.set(d.via, [])
      byGateway.get(d.via).push(d)
    } else {
      direct.push(d)
    }
  }
  const known = new Set(list.map((d) => d.deviceId))
  const byId = (a, b) => a.deviceId.localeCompare(b.deviceId)
  const nodes = direct.sort(byId).map((d) => ({ device: d, leaves: (byGateway.get(d.deviceId) || []).sort(byId) }))
  const orphans = [...byGateway.entries()]
    .filter(([gw]) => !known.has(gw))
    .map(([via, leaves]) => ({ via, leaves: leaves.sort(byId) }))
  return { nodes, orphans }
}
