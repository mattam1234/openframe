import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import vuetify from 'vite-plugin-vuetify'

export default defineConfig({
  plugins: [
    vue(),
    vuetify({ autoImport: true }),
  ],
  // Vitest: unit/component specs live under src/. The Playwright E2E specs in
  // e2e/ (which import @playwright/test) run separately via `npm run test:e2e`.
  test: {
    include: ['src/**/*.{test,spec}.js'],
  },
  server: {
    proxy: {
      '/api': {
        target: process.env.VITE_DEVICE_IP || 'http://192.168.4.1',
        changeOrigin: true,
      },
      '/ws': {
        target: process.env.VITE_DEVICE_IP || 'ws://192.168.4.1',
        ws: true,
        changeOrigin: true,
      },
    },
  },
  build: {
    outDir: '../firmware/data/www',
    emptyOutDir: true,
    rollupOptions: {
      // mklittlefs (used to pack data/www into the LittleFS image) caps filenames
      // at 31 chars. Vite's default `[name]-[hash]` names — especially the
      // `_plugin-vue_export-helper-<hash>.js` helper chunk — exceed that and fail
      // `uploadfs`. Emit short hash-only names so no asset can ever cross the limit.
      output: {
        entryFileNames: 'assets/[hash].js',
        chunkFileNames: 'assets/[hash].js',
        assetFileNames: 'assets/[hash][extname]',
      },
    },
  },
})
