import { describe, it, expect } from 'vitest'
import { fmtUptime, fmtHeap, fmtAgo, rssiLabel, asNum } from './format'

describe('asNum', () => {
  it('coerces finite numbers and rejects the rest', () => {
    expect(asNum('42')).toBe(42)
    expect(asNum(3.14)).toBe(3.14)
    expect(asNum('nope')).toBe(null)
    expect(asNum(undefined)).toBe(null)
    expect(asNum(Infinity)).toBe(null)
  })
})

describe('fmtUptime', () => {
  it('formats days/hours/minutes and handles bad input', () => {
    expect(fmtUptime(90_061_000)).toBe('1d 1h')
    expect(fmtUptime(3_600_000)).toBe('1h 0m')
    expect(fmtUptime(120_000)).toBe('2m')
    expect(fmtUptime('x')).toBe('—')
  })
})

describe('fmtHeap', () => {
  it('shows KB, or — for non-numbers', () => {
    expect(fmtHeap(205000)).toBe('200 KB')
    expect(fmtHeap('nope')).toBe('—')
  })
})

describe('rssiLabel', () => {
  it('labels dBm, treating 0/non-numeric as unknown', () => {
    expect(rssiLabel(-48)).toBe('-48 dBm')
    expect(rssiLabel(0)).toBe('—')
    expect(rssiLabel(undefined)).toBe('—')
  })
})

describe('fmtAgo', () => {
  it('returns never for missing timestamps', () => {
    expect(fmtAgo(null)).toBe('never')
    expect(fmtAgo(0)).toBe('never')
  })
  it('formats a recent timestamp in seconds', () => {
    expect(fmtAgo(Date.now() - 5000)).toMatch(/^\d+s ago$/)
  })
})
