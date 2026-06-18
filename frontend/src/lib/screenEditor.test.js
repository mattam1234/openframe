import { describe, it, expect } from 'vitest'
import { clamp, snap, clampWidgetPos, pointerToDevicePx, fitScale } from './screenEditor'

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
