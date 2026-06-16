/**
 * Per-board GPIO pin lists for the Layout Designer pin selectors.
 *
 * `io`  — pins usable for general digital input/output and PWM.
 * `adc` — pins with an ADC channel, usable for analog inputs (ESP32 uses ADC1
 *         channels so they coexist with WiFi).
 *
 * These mirror the safe/usable sets for each chip; pins tied to flash/PSRAM,
 * strapping-sensitive pins, and (for outputs) input-only pins are excluded.
 */
const BOARDS = {
  'ESP32': {
    io:  [2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33],
    adc: [32, 33, 34, 35, 36, 39],
  },
  'ESP32-S3': {
    io:  [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 38, 39, 40, 41, 42, 45, 47, 48],
    adc: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10],
  },
  'ESP8266': {
    io:  [0, 2, 4, 5, 12, 13, 14, 15, 16],
    adc: [17], // A0
  },
}

const FALLBACK = BOARDS['ESP32']

function setFor(boardType) {
  if (!boardType) return FALLBACK
  // Match the most specific key first so "ESP32-S3" doesn't match "ESP32".
  const bt = boardType.toUpperCase()
  const key = Object.keys(BOARDS)
    .sort((a, b) => b.length - a.length)
    .find(k => bt.startsWith(k.toUpperCase()))
  return key ? BOARDS[key] : FALLBACK
}

// Build v-select items (label/value). Labels show A0 for the ESP8266 analog pin.
export function pinItems(boardType, kind = 'io') {
  const set = setFor(boardType)
  const pins = kind === 'adc' ? set.adc : set.io
  return pins.map(p => ({
    value: p,
    title: (boardType || '').toUpperCase().startsWith('ESP8266') && kind === 'adc' && p === 17
      ? 'A0 (17)'
      : `GPIO ${p}`,
  }))
}
