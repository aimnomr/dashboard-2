import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// In development Vite serves the UI and proxies the socket to the backend, so the app
// always connects to a same-origin "/ws". At launch FastAPI serves the built bundle and
// the same URL resolves directly. One code path, no environment branching.
export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      '/ws': { target: 'ws://127.0.0.1:8000', ws: true },
      '/api': { target: 'http://127.0.0.1:8000' },
    },
  },
  build: {
    outDir: 'dist',
    // No CDN, no remote fonts, no external anything: there is no internet at the
    // launch site. Everything must be in the bundle. See ISS-12.
    assetsInlineLimit: 0,
  },
})
