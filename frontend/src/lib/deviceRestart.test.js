import { describe, it, expect } from 'vitest'
import { waitForDevice } from './deviceRestart'

// Virtual clock: sleep() advances time instantly, probes answer from a script.
function harness(script) {
  let t = 0
  const calls = []
  return {
    calls,
    opts: {
      now: () => t,
      sleep: async (ms) => { t += ms },
      probe: async () => {
        calls.push(t)
        return typeof script === 'function' ? script(t) : script.shift() ?? false
      },
    },
  }
}

describe('waitForDevice', () => {
  it('waits for the device to go down, then come back', async () => {
    // up (not yet restarted), down, down, up
    const h = harness([true, false, false, true])
    expect(await waitForDevice(h.opts)).toBe(true)
    expect(h.calls.length).toBe(4)
  })

  it('does not report "back" while the device has not gone down yet', async () => {
    // Still answering for 3 s before the reboot actually starts, down until 10 s.
    const h = harness((t) => t < 3000 || t >= 10000)
    expect(await waitForDevice(h.opts)).toBe(true)
    expect(h.calls[h.calls.length - 1]).toBeGreaterThanOrEqual(10000)
  })

  it('assumes a fast restart when the device never appears down', async () => {
    const h = harness(() => true)
    expect(await waitForDevice({ ...h.opts, downTimeoutMs: 5000 })).toBe(true)
    // Gave up waiting for "down" around downTimeoutMs, then succeeded at once.
    expect(h.calls[h.calls.length - 1]).toBeLessThanOrEqual(6000)
  })

  it('returns false when the device never comes back', async () => {
    const h = harness(() => false)
    expect(await waitForDevice({ ...h.opts, timeoutMs: 20000 })).toBe(false)
  })
})
