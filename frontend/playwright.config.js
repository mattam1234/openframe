import { defineConfig, devices } from '@playwright/test'

// E2E config (#91). Playwright boots the Vite dev server itself and drives a real
// Chromium against it. The device REST/WS API is stubbed per-test via page.route
// (see e2e/helpers.js), so these run with no hardware and no broker — the same
// "stub the device, exercise the UI" approach used to verify the designer views.
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
  projects: [
    { name: 'chromium', use: { ...devices['Desktop Chrome'] } },
  ],
  webServer: {
    command: 'npm run dev',
    url: 'http://localhost:5173',
    reuseExistingServer: !process.env.CI,
    timeout: 120_000,
  },
})
