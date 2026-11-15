import { defineConfig } from 'vitest/config';

// Hardware-in-the-loop run against the ESP32-S3 (hil/). Not part of CI: it
// needs the board on the bench. scripts/hil.mjs sets the environment.
export default defineConfig({
  test: {
    include: ['hil/**/*.hil.test.ts'],
    environment: 'node',
    fileParallelism: false,
    testTimeout: 30000,
    hookTimeout: 30000,
    bail: 0,
  },
});
