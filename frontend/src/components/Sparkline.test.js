// @vitest-environment happy-dom
import { describe, it, expect } from 'vitest'
import { mount } from '@vue/test-utils'
import { Sparkline } from '@shared'

describe('Sparkline', () => {
  it('renders a placeholder when fewer than 2 points', () => {
    const w = mount(Sparkline, { props: { values: [5] } })
    expect(w.find('svg').exists()).toBe(false)
    expect(w.text()).toBe('—')
  })

  it('renders an SVG polyline for >=2 numeric values', () => {
    const w = mount(Sparkline, { props: { values: [0, 5, 10], width: 80, height: 24 } })
    expect(w.find('svg').exists()).toBe(true)
    const pts = w.find('polyline').attributes('points').trim().split(/\s+/)
    expect(pts).toHaveLength(3)
  })

  it('filters out non-finite values before plotting', () => {
    const w = mount(Sparkline, { props: { values: [1, NaN, 2, Infinity, 3] } })
    const pts = w.find('polyline').attributes('points').trim().split(/\s+/)
    expect(pts).toHaveLength(3) // only 1, 2, 3 survive
  })

  it('maps the largest value to the top (inverted Y) within padding', () => {
    const w = mount(Sparkline, { props: { values: [0, 10], width: 80, height: 24 } })
    const [, p1] = w.find('polyline').attributes('points').trim().split(/\s+/)
    const yOfMax = Number(p1.split(',')[1])
    expect(yOfMax).toBeCloseTo(2, 1) // top, at the 2px padding
  })
})
