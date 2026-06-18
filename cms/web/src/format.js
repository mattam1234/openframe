// Display formatters shared by the fleet grid and device views. Device-supplied
// numbers come from (semi-trusted) heartbeats, so coerce to finite first.
export const asNum = (v) => { const n = Number(v); return Number.isFinite(n) ? n : null }

export function fmtUptime(ms) {
  const n = asNum(ms)
  if (n == null) return '—'
  const s = Math.floor(n / 1000)
  const d = Math.floor(s / 86400)
  const h = Math.floor((s % 86400) / 3600)
  const m = Math.floor((s % 3600) / 60)
  return d ? `${d}d ${h}h` : h ? `${h}h ${m}m` : `${m}m`
}

export function fmtHeap(b) {
  const n = asNum(b)
  return n == null ? '—' : `${(n / 1024).toFixed(0)} KB`
}

export function fmtAgo(ts) {
  const n = asNum(ts)
  if (!n) return 'never'
  const s = Math.floor((Date.now() - n) / 1000)
  return s < 60 ? `${s}s ago` : s < 3600 ? `${Math.floor(s / 60)}m ago` : `${Math.floor(s / 3600)}h ago`
}

export function rssiLabel(r) {
  const n = asNum(r)
  return n == null || n === 0 ? '—' : `${n} dBm`
}
