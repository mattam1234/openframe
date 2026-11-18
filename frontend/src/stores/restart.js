import { defineStore } from 'pinia'
import { ref } from 'vue'
import { waitForDevice } from '../lib/deviceRestart'

/**
 * Tracks a device reboot triggered by a save, so the whole app can show it and
 * views can hold off reloading until the device answers again.
 *
 * `awaitRestart()` resolves true once the device is back, false on timeout.
 * Concurrent callers share the one in-flight wait.
 */
export const useRestartStore = defineStore('restart', () => {
  const restarting = ref(false)
  let pending = null

  function awaitRestart(options) {
    if (pending) return pending
    restarting.value = true
    pending = waitForDevice(options).finally(() => {
      restarting.value = false
      pending = null
    })
    return pending
  }

  return { restarting, awaitRestart }
})
