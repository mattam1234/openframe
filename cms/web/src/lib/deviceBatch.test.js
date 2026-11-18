import { describe, it, expect, vi, afterEach } from 'vitest'
import { buildIndex, upsertInto, createDeviceBatcher, frameScheduler } from './deviceBatch'
import { pushLog, seedLog } from './logWindow'

const dev = (deviceId, extra = {}) => ({ deviceId, ...extra })

// A scheduler the test fires by hand.
function manualScheduler() {
  let next = 1
  const jobs = new Map()
  return {
    schedule: vi.fn((fn) => { const h = next++; jobs.set(h, fn); return h }),
    cancel: vi.fn((h) => { jobs.delete(h) }),
    get pending() { return jobs.size },
    run() { const fns = [...jobs.values()]; jobs.clear(); for (const fn of fns) fn() },
  }
}

describe('buildIndex / upsertInto', () => {
  it('maps each deviceId to its position', () => {
    const index = buildIndex([dev('a'), dev('b'), dev('c')])
    expect([...index]).toEqual([['a', 0], ['b', 1], ['c', 2]])
  })

  it('replaces a known device in place and appends an unknown one', () => {
    const list = [dev('a', { v: 1 }), dev('b', { v: 1 })]
    const index = buildIndex(list)
    upsertInto(list, index, dev('b', { v: 2 }))
    upsertInto(list, index, dev('c', { v: 1 }))
    expect(list).toEqual([dev('a', { v: 1 }), dev('b', { v: 2 }), dev('c', { v: 1 })])
    expect(index.get('c')).toBe(2)
    // The appended device is found through the index next time.
    upsertInto(list, index, dev('c', { v: 2 }))
    expect(list).toHaveLength(3)
    expect(list[2].v).toBe(2)
  })

  it('recovers when the list was reordered or trimmed behind the index', () => {
    const list = [dev('a'), dev('b'), dev('c')]
    const index = buildIndex(list)
    list.splice(0, 1) // 'a' removed: every index is now off by one
    upsertInto(list, index, dev('c', { v: 2 }))
    expect(list).toEqual([dev('b'), dev('c', { v: 2 })])
    upsertInto(list, index, dev('a', { v: 3 })) // must append, not overwrite slot 0
    expect(list).toEqual([dev('b'), dev('c', { v: 2 }), dev('a', { v: 3 })])
  })

  it('does not duplicate a device that was pushed behind the index', () => {
    const list = [dev('a')]
    const index = buildIndex(list)
    list.push(dev('b', { v: 1 }))
    upsertInto(list, index, dev('b', { v: 2 }))
    expect(list).toEqual([dev('a'), dev('b', { v: 2 })])
  })
})

describe('createDeviceBatcher', () => {
  it('applies many frames in one batch, keeping only the latest per device', () => {
    const sched = manualScheduler()
    const apply = vi.fn()
    const b = createDeviceBatcher(apply, sched)
    b.queue(dev('a', { v: 1 }))
    b.queue(dev('b', { v: 1 }))
    b.queue(dev('a', { v: 2 }))
    b.queue(dev('a', { v: 3 }))
    expect(apply).not.toHaveBeenCalled()
    expect(sched.schedule).toHaveBeenCalledTimes(1)
    expect(b.size).toBe(2)
    sched.run()
    expect(apply).toHaveBeenCalledTimes(1)
    expect(apply).toHaveBeenCalledWith([dev('a', { v: 3 }), dev('b', { v: 1 })])
    expect(b.size).toBe(0)
  })

  it('schedules again for frames that arrive after a flush', () => {
    const sched = manualScheduler()
    const apply = vi.fn()
    const b = createDeviceBatcher(apply, sched)
    b.queue(dev('a'))
    sched.run()
    b.queue(dev('b'))
    expect(sched.schedule).toHaveBeenCalledTimes(2)
    sched.run()
    expect(apply.mock.calls.map(([batch]) => batch.map((d) => d.deviceId))).toEqual([['a'], ['b']])
  })

  it('accepts frames queued re-entrantly from apply()', () => {
    const sched = manualScheduler()
    const seen = []
    const b = createDeviceBatcher((batch) => {
      seen.push(batch.map((d) => d.deviceId))
      if (seen.length === 1) b.queue(dev('late'))
    }, sched)
    b.queue(dev('a'))
    sched.run()
    sched.run()
    expect(seen).toEqual([['a'], ['late']])
  })

  it('flush() applies immediately and cancels the scheduled run', () => {
    const sched = manualScheduler()
    const apply = vi.fn()
    const b = createDeviceBatcher(apply, sched)
    b.queue(dev('a'))
    b.flush()
    expect(apply).toHaveBeenCalledTimes(1)
    expect(sched.pending).toBe(0)
    b.flush() // nothing pending → no-op
    expect(apply).toHaveBeenCalledTimes(1)
  })

  it('discard() drops a superseded frame; clear() drops them all', () => {
    const sched = manualScheduler()
    const apply = vi.fn()
    const b = createDeviceBatcher(apply, sched)
    b.queue(dev('a'))
    b.queue(dev('b'))
    b.discard('a')
    sched.run()
    expect(apply).toHaveBeenCalledWith([dev('b')])

    b.queue(dev('c'))
    b.clear()
    expect(sched.pending).toBe(0)
    sched.run()
    expect(apply).toHaveBeenCalledTimes(1)

    b.queue(dev('d'))
    b.discard('d') // last pending frame gone → nothing left to run
    expect(sched.pending).toBe(0)
  })

  it('ignores frames without a deviceId', () => {
    const sched = manualScheduler()
    const b = createDeviceBatcher(vi.fn(), sched)
    b.queue(null)
    b.queue({})
    expect(b.size).toBe(0)
    expect(sched.schedule).not.toHaveBeenCalled()
  })
})

describe('frameScheduler', () => {
  afterEach(() => { vi.useRealTimers() })

  it('falls back to a short timer when requestAnimationFrame is unavailable', () => {
    vi.useFakeTimers()
    const { schedule } = frameScheduler({})
    const fn = vi.fn()
    schedule(fn)
    expect(fn).not.toHaveBeenCalled()
    vi.advanceTimersByTime(20)
    expect(fn).toHaveBeenCalledTimes(1)
  })

  it('runs once on the animation frame and drops the backstop timer', () => {
    vi.useFakeTimers()
    let frame = null
    const env = { requestAnimationFrame: (cb) => { frame = cb; return 7 }, cancelAnimationFrame: vi.fn() }
    const fn = vi.fn()
    frameScheduler(env).schedule(fn)
    frame()
    vi.advanceTimersByTime(1000)
    expect(fn).toHaveBeenCalledTimes(1)
  })

  it('still runs when animation frames are paused (hidden tab)', () => {
    vi.useFakeTimers()
    const env = { requestAnimationFrame: () => 7, cancelAnimationFrame: vi.fn() }
    const fn = vi.fn()
    frameScheduler(env).schedule(fn)
    vi.advanceTimersByTime(1000)
    expect(fn).toHaveBeenCalledTimes(1)
    expect(env.cancelAnimationFrame).toHaveBeenCalledWith(7)
  })

  it('cancel() prevents the run', () => {
    vi.useFakeTimers()
    const { schedule, cancel } = frameScheduler({})
    const fn = vi.fn()
    cancel(schedule(fn))
    vi.advanceTimersByTime(1000)
    expect(fn).not.toHaveBeenCalled()
  })

  it('drives a batcher end to end with the timer fallback', () => {
    vi.useFakeTimers()
    const list = [dev('a', { v: 0 })]
    const index = buildIndex(list)
    const apply = vi.fn((batch) => { for (const d of batch) upsertInto(list, index, d) })
    const b = createDeviceBatcher(apply, frameScheduler({}))
    for (let v = 1; v <= 50; v++) b.queue(dev('a', { v }))
    b.queue(dev('b', { v: 1 }))
    vi.advanceTimersByTime(20)
    expect(apply).toHaveBeenCalledTimes(1)
    expect(list).toEqual([dev('a', { v: 50 }), dev('b', { v: 1 })])
  })
})

describe('log window', () => {
  it('stamps strictly increasing _seq values and keeps the original fields', () => {
    const buf = []
    pushLog(buf, { t: 1, msg: 'one' }, 10)
    pushLog(buf, { t: 2, msg: 'two' }, 10)
    expect(buf.map((e) => e.msg)).toEqual(['one', 'two'])
    expect(buf[1]._seq).toBeGreaterThan(buf[0]._seq)
  })

  it('keeps keys stable for surviving entries when the window shifts', () => {
    const buf = []
    for (let i = 0; i < 3; i++) pushLog(buf, { msg: `m${i}` }, 3)
    const before = buf.map((e) => e._seq)
    pushLog(buf, { msg: 'm3' }, 3)
    expect(buf.map((e) => e.msg)).toEqual(['m1', 'm2', 'm3'])
    expect(buf.slice(0, 2).map((e) => e._seq)).toEqual(before.slice(1))
    expect(new Set(buf.map((e) => e._seq)).size).toBe(3)
  })

  it('seeds the last `max` entries with unique keys and does not mutate its input', () => {
    const src = [{ msg: 'a' }, { msg: 'b' }, { msg: 'c' }]
    const win = seedLog(src, 2)
    expect(win.map((e) => e.msg)).toEqual(['b', 'c'])
    expect(win[1]._seq).toBeGreaterThan(win[0]._seq)
    expect(src.every((e) => !('_seq' in e))).toBe(true)
    expect(seedLog(null, 5)).toEqual([])
  })

  it('keeps keys unique across a seed followed by live entries', () => {
    const win = seedLog([{ msg: 'a' }, { msg: 'b' }], 10)
    pushLog(win, { msg: 'c' }, 10)
    expect(new Set(win.map((e) => e._seq)).size).toBe(3)
  })
})
