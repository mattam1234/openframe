/**
 * HH:MM ↔ time-of-day converters shared by the Layout Designer (night-mode
 * minutes past midnight) and the Action Manager (daily-schedule seconds past
 * midnight). One core owns the string format; the seconds variants wrap it.
 */

/**
 * Format minutes past midnight as HH:MM for a native time input. Non-integer
 * input (unset config) falls back to `fallback` minutes; values wrap into a
 * day (0…1439).
 */
export function minutesToHhmm(min, fallback = 0) {
  const m = Number.isInteger(min) ? ((min % 1440) + 1440) % 1440 : fallback
  return `${String(Math.floor(m / 60)).padStart(2, '0')}:${String(m % 60).padStart(2, '0')}`
}

/**
 * Parse HH:MM to minutes past midnight (wrapped to 0…1439). A cleared/partial
 * field returns undefined, which JSON-serialises as an omitted key — the
 * firmware then falls back to its default.
 */
export function hhmmToMinutes(t) {
  const m = /^(\d{1,2}):(\d{2})$/.exec(String(t ?? '').trim())
  return m ? (Number(m[1]) * 60 + Number(m[2])) % 1440 : undefined
}

/**
 * Format seconds past midnight as HH:MM. Unset or negative (the firmware's
 * "no daily schedule" sentinel) returns the `fallback` string.
 */
export function secondsToHhmm(secs, fallback = '08:00') {
  if (!Number.isFinite(secs) || secs < 0) return fallback
  return minutesToHhmm(Math.floor(secs / 60))
}

/** Parse HH:MM to seconds past midnight; invalid input → -1 (firmware sentinel). */
export function hhmmToSeconds(hhmm) {
  const min = hhmmToMinutes(hhmm)
  return min === undefined ? -1 : min * 60
}
