import react from '@vitejs/plugin-react'
import { defineConfig } from 'vite'

// Relative base so the build works on GitHub Pages project sites (/<repo>/) and locally.
export default defineConfig({
  base: './',
  plugins: [react()],
})
