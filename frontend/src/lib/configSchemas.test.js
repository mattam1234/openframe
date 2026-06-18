import { describe, it, expect } from 'vitest'
import { validateConfigFile, hasSchema } from './configSchemas'

describe('hasSchema', () => {
  it('recognises known config files and pages', () => {
    expect(hasSchema('/outputs.json')).toBe(true)
    expect(hasSchema('/inputs.json')).toBe(true)
    expect(hasSchema('/config.json')).toBe(true)
    expect(hasSchema('/pages/home.json')).toBe(true)
  })
  it('returns false for unknown files', () => {
    expect(hasSchema('/notes.txt')).toBe(false)
    expect(hasSchema('/random.json')).toBe(false)
    expect(hasSchema('/www/index.html')).toBe(false)
  })
})

describe('validateConfigFile', () => {
  it('passes a well-formed collection file', () => {
    const r = validateConfigFile('/outputs.json', { outputs: [{ id: 'led', type: 'led' }] })
    expect(r.errors).toEqual([])
    expect(r.warnings).toEqual([])
  })

  it('errors when the root is not an object', () => {
    expect(validateConfigFile('/outputs.json', []).errors.length).toBe(1)
    expect(validateConfigFile('/sensors.json', 42).errors.length).toBe(1)
    expect(validateConfigFile('/actions.json', null).errors.length).toBe(1)
  })

  it('errors when the collection key is the wrong type', () => {
    const r = validateConfigFile('/sensors.json', { sensors: { not: 'an array' } })
    expect(r.errors).toEqual(['"sensors" must be an array.'])
  })

  it('errors on non-object array elements, reporting the index', () => {
    const r = validateConfigFile('/outputs.json', { outputs: [{ id: 'a' }, 'oops'] })
    expect(r.errors).toEqual(['"outputs[1]" must be an object.'])
  })

  it('warns (but does not error) when a required collection key is missing', () => {
    const r = validateConfigFile('/macros.json', {})
    expect(r.errors).toEqual([])
    expect(r.warnings.length).toBe(1)
    expect(r.warnings[0]).toContain('macros')
  })

  it('validates inputs.json optional arrays without requiring any', () => {
    expect(validateConfigFile('/inputs.json', {}).errors).toEqual([])
    expect(validateConfigFile('/inputs.json', { digital: [], keypads: [] }).errors).toEqual([])
    expect(validateConfigFile('/inputs.json', { encoders: {} }).errors)
      .toEqual(['"encoders" must be an array.'])
  })

  it('validates page widget arrays', () => {
    expect(validateConfigFile('/pages/home.json', { id: 'home', widgets: [] }).errors).toEqual([])
    expect(validateConfigFile('/pages/home.json', { widgets: 5 }).errors)
      .toEqual(['"widgets" must be an array.'])
  })

  it('accepts config.json as any object and applies no collection rules', () => {
    const r = validateConfigFile('/config.json', { device: { name: 'Frame' } })
    expect(r.errors).toEqual([])
    expect(r.warnings).toEqual([])
  })

  it('skips structural rules for unknown files', () => {
    const r = validateConfigFile('/anything.json', 'a bare string')
    expect(r.errors).toEqual([])
    expect(r.warnings).toEqual([])
  })
})
