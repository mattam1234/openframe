// Rolling per-device log window. Entries get a client-side, monotonically
// increasing `_seq` when they enter the window so a list rendering them has a
// stable key: the buffer shifts as it fills, so an array index is not one.
// `_seq` is bookkeeping only — it is never shown and never sent anywhere.
let seq = 0

const stamp = (entry) => ({ ...entry, _seq: ++seq })

// Append one entry, dropping the oldest once the window exceeds `max`.
export function pushLog(buf, entry, max) {
  buf.push(stamp(entry))
  if (buf.length > max) buf.shift()
  return buf
}

// A fresh window holding the last `max` of `entries`.
export function seedLog(entries, max) {
  return (entries || []).slice(-max).map(stamp)
}
