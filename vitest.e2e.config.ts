import { defineConfig } from 'vitest/config';

// End-to-end tests: the real C++ airo-host (firmware/build/airo-host) talking
// to the TypeScript desk model over the same REST/WebSocket APIs the dashboard uses.
export default defineConfig({
  test: {
    include: ['e2e/**/*.e2e.test.ts'],
    environment: 'node',
    fileParallelism: false,
    testTimeout: 20000,
    hookTimeout: 20000,
  },
});
