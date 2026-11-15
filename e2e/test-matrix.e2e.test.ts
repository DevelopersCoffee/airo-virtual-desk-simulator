/**
 * Spec section 12.1 matrix against the laptop host with VirtualHardwareAdapter:
 *   AiroClient (REST/WS) -> airo-host (C++ airo-core) -> /bridge -> InterfaceBoard
 *   -> OriginalDeskController -> mechanism.
 * The rows live in scenarios.ts and also run against the board adapter
 * (esp32-adapter.e2e.test.ts) and the real board (hil/).
 */
import { beforeAll } from 'vitest';
import { hostBuilt, HOST_BIN, targets } from './harness';
import { runScenarios } from './run-scenarios';

beforeAll(() => {
  if (!hostBuilt()) throw new Error(`${HOST_BIN} is missing: run "npm run firmware:build" first`);
});

runScenarios(targets.laptop);
