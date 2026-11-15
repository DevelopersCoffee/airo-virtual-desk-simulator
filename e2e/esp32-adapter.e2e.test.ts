/**
 * The same scenario table against `airo-host --adapter esp32`: the ESP32-S3
 * build's Esp32HardwareAdapter, BenchIo (bench mode, in-memory pins) and the
 * pin-level /bench link, with the desk model on the other side through
 * PinBridge. This is the hardware-in-the-loop run with the board simulated,
 * so the HIL path is proven in CI before the board arrives.
 */
import { beforeAll } from 'vitest';
import { hostBuilt, HOST_BIN, targets } from './harness';
import { runScenarios } from './run-scenarios';

beforeAll(() => {
  if (!hostBuilt()) throw new Error(`${HOST_BIN} is missing: run "npm run firmware:build" first`);
});

runScenarios(targets['laptop-esp32']);
