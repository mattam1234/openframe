// Shared E2E helpers (#91): stub the device API so the UI can be exercised with
// no hardware. The Vue app talks to the device over relative /api/* (and a WS);
// we intercept every /api request at the browser and fulfil it from a fixture.

export const fixture = {
  '/api/hardware': { board: { board_type: 'ESP32' } },
  '/api/inputs': {
    digital: [
      { id: 'button', subtype: 'digital', pin: 0, pullup: true, touch: false },
      { id: 'touchpad', subtype: 'digital', pin: 4, touch: true },
    ],
    analog: [{ id: 'knob', subtype: 'analog', pin: 34 }],
  },
  '/api/outputs': {
    outputs: [
      { id: 'status', type: 'led', pin: 2, gamma: true },
      { id: 'strip', type: 'ws2812', pin: 5, led_count: 30 },
    ],
  },
  '/api/sensors': {
    sensors: [{ id: 'climate', type: 'bme280', address: '0x76', poll_interval_ms: 5000, enabled: true }],
  },
  '/api/displays': {
    displays: [{ id: 'oled', type: 'ssd1306', address: '0x3C', width: 128, height: 64, enabled: true }],
  },
  '/api/displays/pages': {
    pages: [
      {
        id: 'home', title: 'Home', displayId: 'oled', widgets: [
          { id: 'w1', type: 'text', x: 4, y: 2, textSize: 1, text: 'OpenFrame' },
          { id: 'w2', type: 'value', x: 4, y: 26, textSize: 2, variableId: 'temp', suffix: 'C', decimals: 1 },
          { id: 'w3', type: 'value', x: 4, y: 52, textSize: 1, variableId: 'humidity', suffix: '%', decimals: 0 },
        ],
      },
    ],
  },
  '/api/variables': { variables: [{ id: 'temp' }, { id: 'humidity' }, { id: 'pressure' }] },
}

// Route every /api/* request to the fixture; GETs not in the map return {} and
// writes (POST/PUT/DELETE) return { ok: true } so save buttons resolve cleanly.
export async function stubDevice(page, overrides = {}) {
  const data = { ...fixture, ...overrides }
  // Match only root-level device endpoints (/api/...). A glob like **/api/** also
  // catches the app's own modules (e.g. /src/api/client.js) and would serve them
  // JSON, breaking the module load — so match on the exact pathname prefix.
  await page.route(
    (url) => url.pathname.startsWith('/api/'),
    (route) => {
      const pathname = new URL(route.request().url()).pathname
      if (pathname in data) return route.fulfill({ json: data[pathname] })
      if (route.request().method() !== 'GET') return route.fulfill({ json: { ok: true } })
      return route.fulfill({ json: {} })
    },
  )
}
