import { test, expect } from '@playwright/test'
import { stubDevice } from './helpers.js'

test.describe('Layout Designer', () => {
  test.beforeEach(async ({ page }) => {
    await stubDevice(page)
    await page.goto('/#/layout')
  })

  test('loads the stubbed inputs as cards with the board chip', async ({ page }) => {
    await expect(page.getByRole('heading', { name: 'Layout Designer' })).toBeVisible()
    await expect(page.getByText('ESP32')).toBeVisible()
    // 3 input cards (button, touchpad, knob) + the "Add input" ghost tile.
    await expect(page.locator('.ld-item--input')).toHaveCount(3)
    // The id is an editable field — assert its value, not the card's text.
    await expect(page.locator('.ld-item--input').first().locator('.ld-id input')).toHaveValue('button')
  })

  test('adds an input via the ghost tile', async ({ page }) => {
    await expect(page.locator('.ld-item--input')).toHaveCount(3)
    await page.getByRole('button', { name: 'Add input' }).click()
    await expect(page.locator('.ld-item--input')).toHaveCount(4)
  })

  test('switches tabs to show outputs', async ({ page }) => {
    await page.getByRole('tab', { name: /Outputs/ }).click()
    await expect(page.locator('.ld-item--output')).toHaveCount(2)
    await expect(page.locator('.ld-item--output').first().locator('.ld-id input')).toHaveValue('status')
  })

  test('Save inputs is enabled once data has loaded', async ({ page }) => {
    await expect(page.getByRole('button', { name: 'Save inputs' })).toBeEnabled()
  })

  test('saving outputs waits for the device to restart, then confirms', async ({ page }) => {
    // The device replies, then reboots: /api/status fails twice before answering.
    let probes = 0
    await page.route((url) => url.pathname === '/api/outputs', (route) => {
      if (route.request().method() === 'POST') {
        return route.fulfill({ json: { ok: true, restartRequired: true, message: 'Outputs saved. Restarting to apply.' } })
      }
      return route.fallback()
    })
    await page.route((url) => url.pathname === '/api/status', (route) => {
      probes += 1
      return probes <= 2 ? route.abort() : route.fulfill({ json: { ok: true } })
    })

    await page.getByRole('tab', { name: /Outputs/ }).click()
    await page.getByRole('button', { name: 'Save outputs' }).click()

    await expect(page.getByText(/device is restarting to apply/i)).toBeVisible()
    await expect(page.getByText('Restarting…')).toBeVisible()
    await expect(page.getByText(/device has restarted and the change is applied/i)).toBeVisible({ timeout: 15_000 })
    expect(probes).toBeGreaterThanOrEqual(3)
    // The form still holds what was saved — nothing was reloaded or lost.
    await expect(page.locator('.ld-item--output')).toHaveCount(2)
  })
})
