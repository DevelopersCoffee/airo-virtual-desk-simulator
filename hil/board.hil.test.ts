/**
 * Hardware-in-the-loop: the e2e scenario table (e2e/scenarios.ts) against the
 * ESP32-S3 on the bench, over Wi-Fi. The laptop runs the desk model (keypad,
 * Interface Board, original controller, mechanism) and talks to the board's
 * Esp32HardwareAdapter at pin level over /bench. Nothing is wired to the desk.
 *
 * Refuses to run unless the board reports bench mode (see benchRefusals).
 * Run with scripts/hil.mjs, see docs/BENCH_TEST.md.
 */
import { beforeAll } from 'vitest';
import { appClient, assertBenchSafe, targets } from '../e2e/harness';
import { runScenarios } from '../e2e/run-scenarios';

beforeAll(async () => {
  const url = process.env.AIRO_BOARD_URL;
  const token = process.env.AIRO_TOKEN;
  if (!url || !token) throw new Error('set AIRO_BOARD_URL (e.g. http://airo-desk-001.local) and AIRO_TOKEN (serial console: token)');
  const info = await assertBenchSafe(appClient(url.replace(/\/$/, ''), 'test', token));
  console.log(`HIL target ${info.device_id} fw ${info.fw} (${info.hardware}), bench mode confirmed`);
});

runScenarios(targets.board);
