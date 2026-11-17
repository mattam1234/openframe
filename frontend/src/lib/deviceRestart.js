// Wait for the device to come back after a save that reboots it.
//
// Some saves (outputs, and any /api/config change outside the hot-appliable
// sections) make the firmware restart ~1.5 s after it replies. Until it is back
// every request fails, so callers use this to hold off reloading data until the
// device answers again instead of firing requests into the reboot.
//
// Two phases, because the reply arrives *before* the device goes down:
//   1. wait for a probe to fail (the device actually went away), up to
//      `downTimeoutMs` — if it never fails we assume the restart was quicker
//      than our polling and carry on;
//   2. wait for a probe to succeed, up to `timeoutMs` overall.
//
// Resolves true when the device is reachable again, false on timeout (e.g. a
// WiFi change moved it to a different address).

const defaultSleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms))

// "Up" means the device itself answered. A 401/403 still proves that; a 5xx is
// what the Vite dev proxy returns while the target is unreachable.
export async function probeDevice(timeoutMs = 2000) {
  const controller = new AbortController()
  const timer = setTimeout(() => controller.abort(), timeoutMs)
  try {
    const res = await fetch('/api/status', { cache: 'no-store', signal: controller.signal })
    return res.status < 500
  } catch {
    return false
  } finally {
    clearTimeout(timer)
  }
}

export async function waitForDevice({
  probe = probeDevice,
  sleep = defaultSleep,
  now = () => Date.now(),
  initialDelayMs = 1000,
  intervalMs = 1000,
  downTimeoutMs = 8000,
  timeoutMs = 60000,
} = {}) {
  const start = now()
  await sleep(initialDelayMs)

  // Phase 1: see it go down.
  while (now() - start < downTimeoutMs) {
    if (!(await probe())) break
    await sleep(intervalMs)
  }

  // Phase 2: see it come back.
  while (now() - start < timeoutMs) {
    if (await probe()) return true
    await sleep(intervalMs)
  }
  return false
}
