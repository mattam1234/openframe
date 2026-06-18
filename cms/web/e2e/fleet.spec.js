import { test, expect } from '@playwright/test'
import { stubCms } from './helpers.js'

test.describe('CMS fleet UI', () => {
  test.beforeEach(async ({ page }) => {
    await stubCms(page)
    await page.goto('/app/')
  })

  test('lists devices seeded from the API', async ({ page }) => {
    await expect(page.getByText('OpenFrame Fleet')).toBeVisible()
    const rows = page.locator('.v-data-table tbody tr')
    await expect(rows).toHaveCount(3)
    await expect(page.getByRole('link', { name: 'Lobby Frame' })).toBeVisible()
  })

  test('navigates to a device drill-in via the grid link', async ({ page }) => {
    await page.locator('a.devlink', { hasText: 'Lobby Frame' }).click()
    await expect(page).toHaveURL(/\/app\/device\/lobby-01$/)
    await expect(page.getByText('lobby-01')).toBeVisible()
    await expect(page.getByText('Tags & site')).toBeVisible()
  })

  test('site selector scopes the fleet', async ({ page }) => {
    await expect(page.locator('.v-data-table tbody tr')).toHaveCount(3)
    // Pick "Munich Store" from the app-bar site selector.
    await page.locator('.site-select').click()
    await page.getByRole('option', { name: 'Munich Store' }).click()
    await expect(page.locator('.v-data-table tbody tr')).toHaveCount(1)
    await expect(page.getByRole('link', { name: 'Shop Window' })).toBeVisible()
  })

  test('shows the login gate when auth is required', async ({ page }) => {
    await stubCms(page, { '/api/auth': { authRequired: true, authed: false } })
    await page.goto('/app/')
    await expect(page.getByText('OpenFrame CMS')).toBeVisible()
    await expect(page.getByLabel('Access token')).toBeVisible()
  })

  test('reaches the secondary pages via the nav drawer', async ({ page }) => {
    await page.locator('.v-navigation-drawer a', { hasText: 'Templates' }).click()
    await expect(page).toHaveURL(/\/app\/templates$/)
    await expect(page.getByText('New template')).toBeVisible()
  })
})
