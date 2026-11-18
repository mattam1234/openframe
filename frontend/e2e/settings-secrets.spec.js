import { test, expect } from '@playwright/test'
import { stubDevice } from './helpers.js'

// The device masks secrets in GET /api/config. Saving must send the mask back
// (the device then keeps the stored value) and must not overwrite the API token
// this browser has stored with the mask — that would lock the user out.
const MASK = '********'

const maskedConfig = {
  device: { name: 'Frame', board: 'ESP32', id: 'of-abc123', api_token: MASK },
  wifi: { ssid: 'home', password: MASK, ap_mode: false, networks: [{ ssid: 'home', password: MASK }] },
  mqtt: { enabled: true, host: 'broker.lan', port: 1883, user: 'frame', password: MASK, base_topic: 'openframe' },
}

test.describe('Settings with masked secrets', () => {
  test('saving keeps the stored browser token and posts the mask back', async ({ page }) => {
    await stubDevice(page, { '/api/config': maskedConfig })
    await page.addInitScript(() => localStorage.setItem('of_api_token', 'real-token'))

    let posted = null
    await page.route((url) => url.pathname === '/api/config', (route) => {
      if (route.request().method() !== 'POST') return route.fallback()
      posted = route.request().postDataJSON()
      return route.fulfill({ json: { ...maskedConfig, restartRequired: false, message: 'Settings saved and applied.' } })
    })

    await page.goto('/#/settings')
    await expect(page.getByLabel('API token (optional)')).toHaveValue(MASK)

    await page.getByRole('button', { name: /save/i }).first().click()
    await expect(page.getByText('Settings saved and applied.')).toBeVisible()

    expect(posted.device.api_token).toBe(MASK)
    expect(posted.mqtt.password).toBe(MASK)
    expect(posted.wifi.networks[0]).toEqual({ ssid: 'home', password: MASK })
    expect(await page.evaluate(() => localStorage.getItem('of_api_token'))).toBe('real-token')
  })

  test('typing in a masked field replaces the mask instead of appending to it', async ({ page }) => {
    await stubDevice(page, { '/api/config': maskedConfig })
    await page.goto('/#/settings')
    const token = page.getByLabel('API token (optional)')
    await expect(token).toHaveValue(MASK)
    await token.focus()
    await page.keyboard.type('new-secret')
    await expect(token).toHaveValue('new-secret')
  })
})
