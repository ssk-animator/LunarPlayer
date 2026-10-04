import tailwindcss from '@tailwindcss/vite'
import react from '@vitejs/plugin-react'
import path from 'node:path'
import { defineConfig } from 'vite'

// https://vite.dev/config/
export default defineConfig({
  plugins: [react(), tailwindcss()],
  resolve: {
    alias: {
      '@': path.resolve(__dirname, './src'),
    },
  },
  build: {
    // Single-file output for the Burn BA payload (one Payload entry;
    // virtual-host serving still applies for correctness).
    outDir: 'dist',
    assetsInlineLimit: 100 * 1024 * 1024,
    cssCodeSplit: false,
  },
  base: './',
})
