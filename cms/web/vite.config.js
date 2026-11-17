import { fileURLToPath, URL } from 'node:url'
import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import vuetify from 'vite-plugin-vuetify'

// The CMS Vue app is served by the Express server under /app, so assets resolve
// from that base. /api and /ws proxy to the running CMS (default :4000) in dev.
export default defineConfig({
  base: '/app/',
  plugins: [
    vue(),
    vuetify({ autoImport: true }),
  ],
  resolve: {
    // Shared component library, consumed as source by both the device UI and CMS (#45).
    alias: { '@shared': fileURLToPath(new URL('../../shared/ui', import.meta.url)) },
  },
  // Vitest: unit specs live under src/. The Playwright E2E specs in e2e/ (which
  // import @playwright/test) run separately via `npm run test:e2e`.
  test: {
    include: ['src/**/*.{test,spec}.js'],
  },
  // Vuetify components are auto-imported per view, so the dev server keeps
  // discovering new 'vuetify/components/…' entries as lazy routes load. Each
  // discovery re-optimizes dependencies and force-reloads the page, which made
  // the first (cold-cache) E2E run fail. Vuetify ships ESM, so skip pre-bundling.
  optimizeDeps: { exclude: ['vuetify'] },
  server: {
    // Allow reading the shared lib, which lives outside this app's root.
    fs: { allow: ['../..'] },
    proxy: {
      '/api': { target: process.env.CMS_URL || 'http://localhost:4000', changeOrigin: true },
      '/ws': { target: process.env.CMS_URL || 'http://localhost:4000', ws: true, changeOrigin: true },
    },
  },
  build: { outDir: 'dist', emptyOutDir: true },
})
