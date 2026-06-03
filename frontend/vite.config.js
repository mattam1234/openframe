import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import vuetify from 'vite-plugin-vuetify'

export default defineConfig({
  plugins: [
    vue(),
    vuetify({ autoImport: true }),
  ],
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
  },
})
