import { describe, it, expect } from 'vitest'
import { pinItems } from './boardPins'

describe('pinItems', () => {
  it('returns IO pins for ESP32 by default', () => {
    const items = pinItems('ESP32')
    expect(items.length).toBeGreaterThan(0)
    expect(items[0]).toHaveProperty('value')
    expect(items[0]).toHaveProperty('title')
    expect(items.some(i => i.value === 2)).toBe(true)
  })

  it('matches ESP32-S3 before ESP32 (most-specific key wins)', () => {
    const s3 = pinItems('ESP32-S3')
    // GPIO 48 exists on S3 but not in the plain ESP32 set.
    expect(s3.some(i => i.value === 48)).toBe(true)
    expect(pinItems('ESP32').some(i => i.value === 48)).toBe(false)
  })

  it('labels the ESP8266 A0 analog pin', () => {
    const adc = pinItems('ESP8266', 'adc')
    expect(adc).toEqual([{ value: 17, title: 'A0 (17)' }])
  })

  it('falls back to ESP32 for an unknown board', () => {
    expect(pinItems('Mystery')).toEqual(pinItems('ESP32'))
    expect(pinItems('')).toEqual(pinItems('ESP32'))
  })

  it('adc kind returns ADC channels, not IO pins', () => {
    const adc = pinItems('ESP32', 'adc')
    expect(adc.some(i => i.value === 36)).toBe(true)   // ADC-only pin
    expect(pinItems('ESP32', 'io').some(i => i.value === 36)).toBe(false)
  })
})
