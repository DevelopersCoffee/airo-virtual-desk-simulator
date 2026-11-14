/// <reference types="vitest/config" />
import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

const AIRO_HOST = process.env.AIRO_HOST ?? 'http://127.0.0.1:8080';

export default defineConfig({
  plugins: [react()],
  server: {
    // The dashboard and the simulated interface board reach airo-host through
    // the dev server, so the page has one origin.
    proxy: {
      '/api': { target: AIRO_HOST, ws: true, changeOrigin: true },
      '/bridge': { target: AIRO_HOST, ws: true, changeOrigin: true },
    },
  },
  test: {
    include: ['src/**/*.test.ts'],
    environment: 'node',
  },
});
