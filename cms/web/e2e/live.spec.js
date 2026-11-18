import { test, expect } from '@playwright/test'
import { stubCms, sampleDevices } from './helpers.js'

// Live updates: the store buffers `device` WS frames and applies them in one
// batch per animation frame. Drive a fake CMS socket and check the UI converges
// on the latest frame per device, and that log lines keep rendering as the
// window fills.
test.describe('CMS live updates', () => {
  let socket

  test.beforeEach(async ({ page }) => {
    socket = new Promise((resolve) => {
      page.routeWebSocket(/\/ws$/, (ws) => resolve(ws))
    })
    await stubCms(page)
  })

  test('a burst of device frames lands as the latest state per device', async ({ page }) => {
    await page.goto('/app/')
    const rows = page.locator('.v-data-table tbody tr')
    await expect(rows).toHaveCount(3)

    const ws = await socket
    const lobby = sampleDevices[0]
    for (let i = 1; i <= 40; i++) {
      ws.send(JSON.stringify({ type: 'device', device: { ...lobby, name: `Lobby rev ${i}`, freeHeap: 200000 + i } }))
    }
    ws.send(JSON.stringify({
      type: 'device',
      device: { ...lobby, deviceId: 'annex-07', name: 'Annex', ip: '10.0.0.30', lastSeen: Date.now() },
    }))

    await expect(page.getByRole('link', { name: 'Lobby rev 40' })).toBeVisible()
    await expect(page.getByRole('link', { name: 'Annex' })).toBeVisible()
    await expect(rows).toHaveCount(4)
    await expect(page.getByRole('link', { name: 'Lobby Frame' })).toHaveCount(0)

    // A snapshot replaces the list wholesale.
    ws.send(JSON.stringify({ type: 'snapshot', devices: sampleDevices.slice(0, 2) }))
    await expect(rows).toHaveCount(2)
    await expect(page.getByRole('link', { name: 'Lobby Frame' })).toBeVisible()
  })

  test('streams device log lines past the window size', async ({ page }) => {
    // The view seeds the window from REST on mount, replacing whatever is there;
    // let that finish so it can't land in the middle of the stream.
    const seeded = page.waitForResponse((r) => new URL(r.url()).pathname === '/api/devices/lobby-01/logs')
    await page.goto('/app/device/lobby-01')
    await seeded
    await expect(page.getByText('No log lines received yet.')).toBeVisible()

    const ws = await socket
    // 260 lines into a 250-line window: the oldest ten scroll out.
    for (let i = 1; i <= 260; i++) {
      ws.send(JSON.stringify({ type: 'log', deviceId: 'lobby-01', entry: { t: Date.now(), level: 'W', tag: 'net', msg: `line ${i}` } }))
    }
    const lines = page.locator('.logbox .logmsg')
    await expect(lines).toHaveCount(250)
    await expect(lines.first()).toHaveText('line 11')
    await expect(lines.last()).toHaveText('line 260')
  })
})
