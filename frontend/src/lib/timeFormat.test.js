import { describe, it, expect } from 'vitest'
import { minutesToHhmm, hhmmToMinutes, secondsToHhmm, hhmmToSeconds } from './timeFormat'

describe('minutesToHhmm', () => {
  it('formats minutes past midnight as HH:MM', () => {
    expect(minutesToHhmm(0)).toBe('00:00')
    expect(minutesToHhmm(420)).toBe('07:00')
    expect(minutesToHhmm(1320)).toBe('22:00')
    expect(minutesToHhmm(1439)).toBe('23:59')
  })
  it('wraps out-of-range values into a day', () => {
    expect(minutesToHhmm(1440)).toBe('00:00')
    expect(minutesToHhmm(-60)).toBe('23:00')
  })
  it('uses the fallback for unset (non-integer) config values', () => {
    expect(minutesToHhmm(undefined, 1320)).toBe('22:00')
    expect(minutesToHhmm(null, 420)).toBe('07:00')
    expect(minutesToHhmm('9')).toBe('00:00')
  })
})

describe('hhmmToMinutes', () => {
  it('parses HH:MM to minutes past midnight', () => {
    expect(hhmmToMinutes('00:00')).toBe(0)
    expect(hhmmToMinutes('07:30')).toBe(450)
    expect(hhmmToMinutes('23:59')).toBe(1439)
    expect(hhmmToMinutes('7:05')).toBe(425) // single-digit hour ok
  })
  it('returns undefined for cleared or partial fields', () => {
    expect(hhmmToMinutes('')).toBeUndefined()
    expect(hhmmToMinutes(undefined)).toBeUndefined()
    expect(hhmmToMinutes('12')).toBeUndefined()
    expect(hhmmToMinutes('12:3')).toBeUndefined()
    expect(hhmmToMinutes('junk')).toBeUndefined()
  })
})

describe('secondsToHhmm', () => {
  it('formats seconds past midnight as HH:MM (flooring within the minute)', () => {
    expect(secondsToHhmm(0)).toBe('00:00')
    expect(secondsToHhmm(8 * 3600)).toBe('08:00')
    expect(secondsToHhmm(13 * 3600 + 45 * 60 + 59)).toBe('13:45')
  })
  it('falls back for unset or sentinel (-1) values', () => {
    expect(secondsToHhmm(undefined)).toBe('08:00')
    expect(secondsToHhmm(null)).toBe('08:00')
    expect(secondsToHhmm(-1)).toBe('08:00')
    expect(secondsToHhmm(-1, '12:00')).toBe('12:00')
  })
})

describe('hhmmToSeconds', () => {
  it('parses HH:MM to seconds past midnight', () => {
    expect(hhmmToSeconds('00:00')).toBe(0)
    expect(hhmmToSeconds('08:00')).toBe(8 * 3600)
    expect(hhmmToSeconds('23:59')).toBe(23 * 3600 + 59 * 60)
  })
  it('returns the -1 sentinel for invalid input', () => {
    expect(hhmmToSeconds('')).toBe(-1)
    expect(hhmmToSeconds(undefined)).toBe(-1)
    expect(hhmmToSeconds('nope')).toBe(-1)
  })
})
