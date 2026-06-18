import { defineConfig, devices } from '@playwright/test'

// E2E for the CMS Vue app. Playwright boots the Vite dev server (served under
// /app/) and drives Chromium; the CMS REST API + auth are stubbed per-test via
// page.route, so these run with no broker and no devices.
export default defineConfig({
  testDir: './e2e',
  fullyParallel: true,
  forbidOnly: !!process.env.CI,
  retries: process.env.CI ? 1 : 0,
  reporter: process.env.CI ? 'line' : 'list',
  use: {
    baseURL: 'http://localhost:5173',
    trace: 'on-first-retry',
  },
  projects: [{ name: 'chromium', use: { ...devices['Desktop Chrome'] } }],
  webServer: {
    command: 'npm run dev',
    url: 'http://localhost:5173/app/',
    reuseExistingServer: !process.env.CI,
    timeout: 120_000,
  },
})
