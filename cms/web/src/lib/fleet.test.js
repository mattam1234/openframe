import { describe, it, expect } from 'vitest'
import { filterBySite, sitesOf, resolveTargetSpec, buildTopology } from './fleet'

const dev = (deviceId, extra = {}) => ({ deviceId, ...extra })

describe('filterBySite', () => {
  const devices = [dev('a', { site: 'HQ' }), dev('b', { site: 'Shop' }), dev('c')]
  it('returns all devices when no site is selected', () => {
    expect(filterBySite(devices, '')).toHaveLength(3)
    expect(filterBySite(devices, null)).toBe(devices)
  })
  it('keeps only devices in the selected site', () => {
    expect(filterBySite(devices, 'HQ').map((d) => d.deviceId)).toEqual(['a'])
    expect(filterBySite(devices, 'Shop').map((d) => d.deviceId)).toEqual(['b'])
    expect(filterBySite(devices, 'Nowhere')).toEqual([])
  })
})

describe('sitesOf', () => {
  it('returns distinct, sorted, non-empty sites', () => {
    const devices = [dev('a', { site: 'Shop' }), dev('b', { site: 'HQ' }), dev('c', { site: 'HQ' }), dev('d')]
    expect(sitesOf(devices)).toEqual(['HQ', 'Shop'])
  })
})

describe('resolveTargetSpec', () => {
  it('prefers an explicit selection over tags and fleet', () => {
    expect(resolveTargetSpec({ selectedIds: ['x', 'y'], tags: ['t'], onlineOnly: true }))
      .toEqual({ deviceIds: ['x', 'y'] })
  })
  it('falls back to tags with the online-only flag', () => {
    expect(resolveTargetSpec({ tags: ['lobby'], onlineOnly: true }))
      .toEqual({ tags: ['lobby'], onlineOnly: true })
  })
  it('falls back to the whole fleet', () => {
    expect(resolveTargetSpec({ onlineOnly: false })).toEqual({ onlineOnly: false })
    expect(resolveTargetSpec()).toEqual({ onlineOnly: false })
  })
})

describe('buildTopology', () => {
  it('groups direct nodes and their ESP-NOW leaves, sorted by id', () => {
    const list = [
      dev('gw1'),
      dev('leafB', { via: 'gw1' }),
      dev('leafA', { via: 'gw1' }),
      dev('solo'),
    ]
    const { nodes, orphans } = buildTopology(list)
    expect(nodes.map((n) => n.device.deviceId)).toEqual(['gw1', 'solo'])
    expect(nodes[0].leaves.map((l) => l.deviceId)).toEqual(['leafA', 'leafB'])
    expect(nodes[1].leaves).toEqual([])
    expect(orphans).toEqual([])
  })
  it('surfaces leaves whose gateway is not a known device as orphans', () => {
    const { nodes, orphans } = buildTopology([dev('x', { via: 'ghost-gw' })])
    expect(nodes).toEqual([])
    expect(orphans).toHaveLength(1)
    expect(orphans[0].via).toBe('ghost-gw')
    expect(orphans[0].leaves.map((l) => l.deviceId)).toEqual(['x'])
  })
})
