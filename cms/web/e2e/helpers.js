// Stub the CMS REST API + auth so the Vue app can be driven with no broker.
// The store seeds the fleet from GET /api/devices, then goes live over the WS
// (which simply never connects in tests — handled gracefully).

export const sampleDevices = [
  { deviceId: 'lobby-01', name: 'Lobby Frame', board: 'ESP32', version: '0.4.0', ip: '10.0.0.21', rssi: -48, freeHeap: 205000, uptimeMs: 90061000, activeProfileId: 'day', tags: ['lobby'], site: 'Berlin Office', online: true, lastSeen: Date.now() - 4000 },
  { deviceId: 'kitchen-02', name: 'Kitchen', board: 'ESP32-S3', version: '0.4.0', ip: '10.0.0.22', rssi: -67, freeHeap: 188000, uptimeMs: 3600000, activeProfileId: 'default', tags: ['kitchen'], site: 'Berlin Office', online: true, lastSeen: Date.now() - 9000 },
  { deviceId: 'shop-09', name: 'Shop Window', board: 'ESP32', version: '0.4.0', ip: '10.1.0.5', rssi: -59, freeHeap: 196000, uptimeMs: 7200000, activeProfileId: 'day', tags: ['window'], site: 'Munich Store', online: true, lastSeen: Date.now() - 6000 },
]

export async function stubCms(page, overrides = {}) {
  const data = {
    '/api/auth': { authRequired: false, authed: true, role: 'admin' },
    '/api/devices': { devices: sampleDevices },
    '/api/templates': { templates: [] },
    '/api/alerts': { active: [], recent: [] },
    ...overrides,
  }
  // Match only root device endpoints; a glob like **/api/** also catches the app's
  // own /src/api... modules and would serve them JSON, breaking the load.
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
