import { describe, it, expect } from 'vitest'
import {
  clamp, snap, clampWidgetPos, pointerToDevicePx, fitScale,
  clampWidgetSize, moveItem, variableGroupOf, sortPages,
} from './screenEditor'

describe('clamp', () => {
  it('bounds a value and falls back to min for non-finite input', () => {
    expect(clamp(5, 0, 10)).toBe(5)
    expect(clamp(-3, 0, 10)).toBe(0)
    expect(clamp(99, 0, 10)).toBe(10)
    expect(clamp(NaN, 2, 10)).toBe(2)
  })
})

describe('snap', () => {
  it('rounds to the nearest step', () => {
    expect(snap(13, 4)).toBe(12)
    expect(snap(14, 4)).toBe(16)
    expect(snap(7)).toBe(7)
    expect(snap(7.6)).toBe(8)
    expect(snap(5, 0)).toBe(5) // step ≤ 0 → whole pixels
  })
})

describe('clampWidgetPos', () => {
  const display = { width: 128, height: 64 }
  it('keeps the origin on the panel', () => {
    expect(clampWidgetPos(10, 20, display)).toEqual({ x: 10, y: 20 })
    expect(clampWidgetPos(-5, -5, display)).toEqual({ x: 0, y: 0 })
    expect(clampWidgetPos(999, 999, display)).toEqual({ x: 127, y: 63 })
  })
  it('rounds to whole pixels and defaults a missing display to 128×64', () => {
    expect(clampWidgetPos(3.7, 4.2, display)).toEqual({ x: 4, y: 4 })
    expect(clampWidgetPos(200, 200, undefined)).toEqual({ x: 127, y: 63 })
  })
})

describe('pointerToDevicePx', () => {
  const rect = { left: 100, top: 50 }
  const display = { width: 128, height: 64 }
  it('maps a pointer over a 4× canvas back to device pixels', () => {
    // 40px right, 20px down on a 4× canvas → (10, 5) device px.
    expect(pointerToDevicePx(140, 70, rect, 4, display)).toEqual({ x: 10, y: 5 })
  })
  it('subtracts the grab offset so a dragged widget tracks the cursor', () => {
    // Pointer at device (10,5) but grabbed 3px right / 2px down into the widget.
    expect(pointerToDevicePx(140, 70, rect, 4, display, 1, { x: 3, y: 2 }))
      .toEqual({ x: 7, y: 3 })
  })
  it('snaps to the grid step and clamps to bounds', () => {
    expect(pointerToDevicePx(145, 70, rect, 4, display, 4)).toEqual({ x: 12, y: 4 })
    expect(pointerToDevicePx(9999, 9999, rect, 4, display)).toEqual({ x: 127, y: 63 })
  })
})

describe('fitScale', () => {
  it('fits the device width into the target, bounded', () => {
    expect(fitScale(128)).toBe(4) // 512/128
    expect(fitScale(64)).toBe(8) // 512/64 = 8, at max
    expect(fitScale(320)).toBe(2) // 512/320 = 1.6 → floor 1, min 2
  })
})

describe('clampWidgetSize', () => {
  const display = { width: 128, height: 64 }
  it('keeps the widget on the panel from its origin', () => {
    expect(clampWidgetSize(40, 20, 10, 10, display)).toEqual({ w: 40, h: 20 })
    expect(clampWidgetSize(999, 999, 100, 50, display)).toEqual({ w: 28, h: 14 })
  })
  it('enforces the 1×1 minimum and handles junk input', () => {
    expect(clampWidgetSize(-5, 0, 0, 0, display)).toEqual({ w: 1, h: 1 })
    expect(clampWidgetSize(NaN, NaN, 0, 0, display)).toEqual({ w: 1, h: 1 })
  })
  it('allows zero-size deltas when min is 0 (line widgets)', () => {
    expect(clampWidgetSize(-3, 0, 5, 5, display, 1, { w: 0, h: 0 })).toEqual({ w: 0, h: 0 })
  })
  it('snaps to the step, then clamps to the panel edge', () => {
    expect(clampWidgetSize(13, 14, 0, 0, display, 4)).toEqual({ w: 12, h: 16 })
    expect(clampWidgetSize(127, 63, 120, 60, display, 4)).toEqual({ w: 8, h: 4 })
  })
  it('defaults a missing display to 128×64', () => {
    expect(clampWidgetSize(500, 500, 0, 0, undefined)).toEqual({ w: 128, h: 64 })
  })
})

describe('moveItem', () => {
  it('moves an element in place and reports success', () => {
    const arr = ['a', 'b', 'c']
    expect(moveItem(arr, 0, 2)).toBe(true)
    expect(arr).toEqual(['b', 'c', 'a'])
    expect(moveItem(arr, 2, 1)).toBe(true)
    expect(arr).toEqual(['b', 'a', 'c'])
  })
  it('rejects out-of-bounds or no-op moves', () => {
    const arr = ['a', 'b']
    expect(moveItem(arr, 0, 0)).toBe(false)
    expect(moveItem(arr, -1, 1)).toBe(false)
    expect(moveItem(arr, 0, 2)).toBe(false)
    expect(moveItem(arr, 1.5, 0)).toBe(false)
    expect(moveItem('nope', 0, 1)).toBe(false)
    expect(arr).toEqual(['a', 'b'])
  })
})

describe('variableGroupOf', () => {
  it('trusts a specific firmware source tag', () => {
    expect(variableGroupOf('foo', 'sensor')).toBe('sensor')
    expect(variableGroupOf('foo', 'output')).toBe('output')
  })
  it('falls back to id-prefix heuristics when source is local/absent', () => {
    expect(variableGroupOf('sensor.bme280.temp')).toBe('sensor')
    expect(variableGroupOf('input.button1', 'local')).toBe('input')
    expect(variableGroupOf('weather.outside_temp', 'local')).toBe('weather')
    expect(variableGroupOf('node/kitchen/temp')).toBe('node')
    expect(variableGroupOf('node.mesh1')).toBe('node')
    expect(variableGroupOf('counter', 'local')).toBe('local')
    expect(variableGroupOf('counter')).toBe('local')
  })
})

describe('sortPages', () => {
  const displays = [
    { id: 'd1', page_order: ['p2', 'p1'] },
    { id: 'd2', page_order: [] },
  ]
  it('groups by display and follows page_order within a display', () => {
    const pages = [
      { id: 'p1', displayId: 'd1' },
      { id: 'pz', displayId: 'd2' },
      { id: 'p2', displayId: 'd1' },
    ]
    expect(sortPages(pages, displays).map((p) => p.id)).toEqual(['p2', 'p1', 'pz'])
  })
  it('appends pages missing from page_order (and unknown displays) in stable order', () => {
    const pages = [
      { id: 'new2', displayId: 'd1' },
      { id: 'p1', displayId: 'd1' },
      { id: 'orphan', displayId: 'ghost' },
      { id: 'new1', displayId: 'd1' },
    ]
    expect(sortPages(pages, displays).map((p) => p.id)).toEqual(['p1', 'new2', 'new1', 'orphan'])
    expect(sortPages(pages, undefined).map((p) => p.id)).toEqual(['new2', 'p1', 'orphan', 'new1'])
  })
})
