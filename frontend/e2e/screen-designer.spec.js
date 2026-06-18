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

  test('selecting a widget reveals its inspector', async ({ page }) => {
    // No widget selected → only the Page block shows.
    await expect(page.locator('.sd-inspector')).not.toContainText('Widget')
    await page.locator('.sd-widget', { hasText: 'temp' }).click()
    await expect(page.locator('.sd-inspector')).toContainText('Widget')
    // The value widget's variable is bound in the inspector.
    await expect(page.locator('.sd-inspector input').first()).toHaveValue('temp')
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
