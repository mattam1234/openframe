// Pure helpers behind the fleet store's live-update path. A busy fleet sends many
// `device` WS frames per second; applying each one to the reactive devices array
// re-ran every computed that iterates the fleet once per frame. Instead frames are
// coalesced per deviceId and applied together once per animation frame, and
// devices are located through a deviceId → index map rather than a linear scan.

// deviceId → index for `list`.
export function buildIndex(list) {
  const index = new Map()
  for (let i = 0; i < list.length; i++) index.set(list[i].deviceId, i)
  return index
}

// Replace-or-append `device` in `list`, keeping `index` in step. If the array was
// changed behind the index's back (so the slot no longer holds that device, or a
// miss can't be trusted), the index is rebuilt in place before it is used.
export function upsertInto(list, index, device) {
  const id = device.deviceId
  let i = index.get(id)
  const stale = i === undefined ? index.size !== list.length : list[i]?.deviceId !== id
  if (stale) {
    index.clear()
    for (let k = 0; k < list.length; k++) index.set(list[k].deviceId, k)
    i = index.get(id)
  }
  if (i !== undefined) list[i] = device
  else {
    index.set(id, list.length)
    list.push(device)
  }
}

// How long a pending batch may wait when animation frames aren't being delivered
// (background tab) — and the delay used when requestAnimationFrame doesn't exist.
const HIDDEN_FLUSH_MS = 250
const NO_RAF_FLUSH_MS = 16

// Default scheduler: next animation frame, with a timer as a backstop because
// browsers pause rAF in hidden tabs. Without rAF (unit tests, SSR) it is just a
// short timer. Returns { schedule(fn) → handle, cancel(handle) }.
export function frameScheduler(env = globalThis) {
  const raf = typeof env.requestAnimationFrame === 'function' ? env.requestAnimationFrame.bind(env) : null
  const caf = typeof env.cancelAnimationFrame === 'function' ? env.cancelAnimationFrame.bind(env) : null
  const cancel = (h) => {
    if (!h) return
    if (h.frame != null && caf) caf(h.frame)
    clearTimeout(h.timer)
  }
  const schedule = (fn) => {
    const h = { frame: null, timer: null, done: false }
    const run = () => {
      if (h.done) return
      h.done = true
      cancel(h)
      fn()
    }
    if (raf) h.frame = raf(run)
    h.timer = setTimeout(run, raf ? HIDDEN_FLUSH_MS : NO_RAF_FLUSH_MS)
    return h
  }
  return { schedule, cancel }
}

// Coalesces device frames: `queue(device)` keeps only the latest frame per
// deviceId and schedules one `apply(devices[])` call for the whole batch.
export function createDeviceBatcher(apply, scheduler = frameScheduler()) {
  const pending = new Map()
  let handle = null

  const unschedule = () => {
    if (handle !== null) {
      scheduler.cancel(handle)
      handle = null
    }
  }

  // Apply whatever is pending right now (no-op when empty).
  function flush() {
    unschedule()
    if (!pending.size) return
    const batch = [...pending.values()]
    pending.clear()
    apply(batch)
  }

  function queue(device) {
    if (!device || device.deviceId == null) return
    pending.set(device.deviceId, device)
    if (handle === null) handle = scheduler.schedule(() => { handle = null; flush() })
  }

  // Drop a queued frame that has been superseded (e.g. by a synchronous upsert).
  function discard(deviceId) {
    pending.delete(deviceId)
    if (!pending.size) unschedule()
  }

  // Drop everything queued (a snapshot replaces the whole list).
  function clear() {
    pending.clear()
    unschedule()
  }

  return { queue, flush, discard, clear, get size() { return pending.size } }
}
