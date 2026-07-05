import { test, expect } from '@playwright/test'
import { stubDevice } from './helpers.js'

test.describe('Screen Designer', () => {
  test.beforeEach(async ({ page }) => {
    await stubDevice(page)
    await page.goto('/#/screens')
    // Open the stubbed "Home" page into the editor.
    await page.locator('.sd-pagebar .v-chip', { hasText: 'Home' }).click()
    await expect(page.locator('.sd-panel')).toBeVisible()
  })

  test('renders the page widgets on the device panel', async ({ page }) => {
    await expect(page.locator('.sd-widget')).toHaveCount(3)
    await expect(page.locator('.sd-widget', { hasText: 'OpenFrame' })).toBeVisible()
  })

  test('drags a widget from the content library onto the panel', async ({ page }) => {
    await expect(page.locator('.sd-widget')).toHaveCount(3)
    await page.locator('.sd-lib-item', { hasText: 'Static text' })
      .dragTo(page.locator('.sd-panel'))
    await expect(page.locator('.sd-widget')).toHaveCount(4)
  })

  test('keyboard: add from the library and nudge with arrow keys (a11y)', async ({ page }) => {
    await expect(page.locator('.sd-widget')).toHaveCount(3)
    // Enter on a focused library tile adds a widget (drag-free alternative).
    await page.locator('.sd-lib-item', { hasText: 'Static text' }).focus()
    await page.keyboard.press('Enter')
    await expect(page.locator('.sd-widget')).toHaveCount(4)
    // The new widget is focused at the origin; arrow keys move it (1px snap).
    await page.keyboard.press('ArrowRight')
    await page.keyboard.press('ArrowDown')
    await expect(page.locator('.sd-widget--selected')).toHaveAttribute('aria-label', /at 1, 1/)
    // Delete removes the focused widget.
    await page.keyboard.press('Delete')
    await expect(page.locator('.sd-widget')).toHaveCount(3)
  })

  test('selecting a widget reveals its inspector', async ({ page }) => {
    // No widget selected → only the Page block shows.
    await expect(page.locator('.sd-inspector')).not.toContainText('Widget')
    await page.locator('.sd-widget', { hasText: 'temp' }).click()
    await expect(page.locator('.sd-inspector')).toContainText('Widget')
    // The value widget's variable is bound in the inspector. Target the field by
    // its label: the Type select sits above it and would win a bare .first().
    await expect(page.locator('.sd-inspector').getByLabel('Variable', { exact: true })).toHaveValue('temp')
  })

  test('shows the empty state when no display exists', async ({ page }) => {
    await stubDevice(page, { '/api/displays': { displays: [] }, '/api/displays/pages': { pages: [] } })
    // Re-goto to the same hash won't remount the SPA; reload to get a fresh load
    // against the now-empty fixture.
    await page.reload()
    await expect(page.getByText('No page selected')).toBeVisible()
    // v-btn with a `to` prop renders as a link, not a button.
    await expect(page.getByRole('link', { name: 'Go to Layout Designer' })).toBeVisible()
  })
})
