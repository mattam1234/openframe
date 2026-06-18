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
})
