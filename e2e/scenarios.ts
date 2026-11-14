/**
 * The scenario table (spec section 12.1 "Simulation test matrix") as data, so
 * the same rows run against every target in e2e/harness.ts:
 *   laptop        AiroClient -> airo-host (VirtualHardwareAdapter) -> /bridge -> desk model
 *   laptop-esp32  AiroClient -> airo-host --adapter esp32 -> /bench (pin level) -> desk model
 *   board         AiroClient -> ESP32-S3 over Wi-Fi -> /bench (pin level) -> desk model
 * Each row has a stable id so it can be selected (AIRO_SCENARIOS=id,id).
 */
import { expect } from 'vitest';
import type { InterfaceCase } from '../src/packages/desk-hardware';
import { appClient, shortProfile, sleep, waitFor, type Bench, type BenchOptions, type Target } from './harness';

export const CASES: InterfaceCase[] = ['A1', 'A2', 'B'];

export interface ScenarioCtx {
  target: Target;
  /** Open a bench on this target; closed automatically after the scenario. */
  open(c: InterfaceCase, opts?: BenchOptions): Promise<Bench>;
  /** Time budget with the target's link slack added. */
  slack(ms: number): number;
}

export interface Scenario {
  id: string;
  title: string;
  /** Spec 12.1 row it covers. */
  row: string;
  /** Interface cases it runs on: each case gets its own test. */
  cases: InterfaceCase[];
  timeoutMs?: number;
  run(ctx: ScenarioCtx, c: InterfaceCase): Promise<void>;
}

async function holdFor(b: Bench, ms: number, cmd: 'desk.up' | 'desk.down' = 'desk.up') {
  const reply = await b.app.startHold(cmd);
  expect(reply).toMatchObject({ type: 'ack', result: 'accepted' });
  await sleep(ms);
}

export const SCENARIOS: Scenario[] = [
  {
    id: 'physical-keys',
    row: 'Physical UP / DOWN',
    title: 'physical UP and DOWN buttons move the desk (core attached, idle)',
    cases: CASES,
    async run({ open }, c) {
      const b = await open(c);
      const h0 = b.desk.heightMm;
      b.desk.rig.keypad.press('up');
      await sleep(400);
      b.desk.rig.keypad.release('up');
      const h1 = b.desk.heightMm;
      expect(h1).toBeGreaterThan(h0 + 5);
      b.desk.rig.keypad.press('down');
      await sleep(300);
      b.desk.rig.keypad.release('down');
      expect(b.desk.heightMm).toBeLessThan(h1 - 3);
    },
  },
  {
    id: 'app-lease',
    row: 'App desk.up / desk.down',
    title: 'app desk.up / desk.down move the desk only while the lease is renewed',
    cases: CASES,
    async run({ open, slack }, c) {
      const b = await open(c);
      const h0 = b.desk.heightMm;
      await holdFor(b, 1000, 'desk.up');
      expect(b.desk.moving).toBe('up');
      await b.app.releaseHold();
      await waitFor(() => b.desk.moving === null, slack(500), 'stop after release');
      const h1 = b.desk.heightMm;
      expect(h1).toBeGreaterThan(h0 + 20);
      await sleep(100); // settle
      await holdFor(b, 500, 'desk.down');
      expect(b.desk.moving).toBe('down');
      await b.app.releaseHold();
      await waitFor(() => b.desk.moving === null, slack(500), 'stop after release');
      expect(b.desk.heightMm).toBeLessThan(h1 - 5);
    },
  },
  {
    id: 'presets',
    row: 'desk.sitting / desk.standing',
    title: 'desk.standing then desk.sitting reach the controller presets (no height feedback needed)',
    cases: CASES,
    timeoutMs: 40000,
    async run({ open }, c) {
      const b = await open(c);
      const { standHeightMm, sitHeightMm } = b.desk.rig.controller.options;
      expect((await b.app.send('desk.standing')).result).toBe('accepted');
      await waitFor(() => Math.abs(b.desk.heightMm - standHeightMm) < 1 && b.desk.moving === null, 15000, 'standing');
      expect((await b.app.send('desk.sitting')).result).toBe('accepted');
      await waitFor(() => Math.abs(b.desk.heightMm - sitHeightMm) < 1 && b.desk.moving === null, 15000, 'sitting');
    },
  },
  {
    id: 'keypad-wins',
    row: 'Physical and app together',
    title: 'physical key during an app command: app releases fast, keypad wins, no phantom preset',
    cases: CASES,
    async run({ open, slack }, c) {
      const b = await open(c);
      await holdFor(b, 400, 'desk.up');
      expect(b.desk.moving).toBe('up');
      const pressedAt = b.desk.nowMs;
      let releasedAt: number | null = null;
      b.desk.watch(() => {
        const s = b.desk.rig.board.snapshot();
        const appGone = s.injecting.length === 0 && s.selector !== 'airo' && !b.desk.rig.controller.seen().includes('up');
        if (appGone) releasedAt = b.desk.nowMs;
        return appGone;
      });
      b.desk.rig.keypad.press('down');
      await waitFor(() => releasedAt !== null, slack(1000), 'app release');
      await sleep(300);
      expect(b.desk.moving).toBe('down'); // the keypad's command, not the app's
      b.desk.rig.keypad.release('down');
      const latency = releasedAt! - pressedAt;
      // The core releases on the tick after it senses the key (firmware unit
      // test: <= 1 ms tick). End to end this includes the link and the desk
      // model's 1 ms step loop; on the board it includes two Wi-Fi hops, so
      // the real 20 ms budget is measured on the pins (docs/BENCH_TEST.md B6).
      expect(latency).toBeLessThan(slack(50));
      expect(b.desk.presetsStarted()).toEqual([]);
      expect(b.events.map((e) => e.event)).toContain('manual_override');
      expect((await b.app.send('desk.up')).code).toBe('manual_active');
    },
  },
  {
    id: 'power-loss',
    row: 'Airo power loss',
    title: 'Airo power loss: selector returns to the keypad path and the keypad works',
    cases: CASES,
    async run({ open, slack }, c) {
      const b = await open(c);
      await holdFor(b, 300, 'desk.up');
      b.desk.rig.board.setCorePowered(false);
      b.desk.rig.flush();
      await waitFor(() => b.desk.moving === null, 300, 'stop on power loss');
      expect(b.desk.rig.board.snapshot().selector).not.toBe('airo');
      await sleep(slack(0));
      expect((await b.app.rest('desk.up')).http).toBe(503);
      const h0 = b.desk.heightMm;
      b.desk.rig.keypad.press('up');
      await sleep(300);
      b.desk.rig.keypad.release('up');
      expect(b.desk.heightMm).toBeGreaterThan(h0 + 3);
      await waitFor(() => b.desk.moving === null, 500, 'keypad release');
      // Power back: the core boots, and nothing moves on its own.
      b.desk.rig.board.setCorePowered(true);
      b.desk.rig.flush();
      const h1 = b.desk.heightMm;
      await waitFor(async () => (await b.app.getStatus()).body.state === 'IDLE', 4000, 'reboot to IDLE');
      await sleep(300);
      expect(b.desk.heightMm).toBe(h1);
    },
  },
  {
    id: 'core-crash',
    row: 'Airo power loss (process killed)',
    title: 'core killed mid-move: hardware watchdog stops the desk within 200 ms',
    cases: CASES,
    async run({ open, slack }, c) {
      const b = await open(c);
      await holdFor(b, 300, 'desk.up');
      const t0 = b.desk.nowMs;
      let stoppedAt: number | null = null;
      b.desk.watch(() => {
        if (b.desk.rig.board.watchdogOk()) return false;
        stoppedAt = b.desk.nowMs;
        return true;
      });
      await b.crash();
      await waitFor(() => stoppedAt !== null && b.desk.moving === null, slack(1000), 'watchdog');
      expect(stoppedAt! - t0).toBeLessThanOrEqual(slack(260));
      const h0 = b.desk.heightMm;
      b.desk.rig.keypad.press('up');
      await sleep(300);
      expect(b.desk.heightMm).toBeGreaterThan(h0 + 3);
    },
  },
  {
    id: 'lease-expiry',
    row: 'Network disconnect during hold',
    title: 'network drop during hold: lease expires and movement stops',
    cases: CASES,
    async run({ open, slack }, c) {
      const b = await open(c);
      await holdFor(b, 300, 'desk.up');
      b.app.renewalsPaused = true; // packets stop arriving, socket stays open
      const t0 = Date.now();
      await waitFor(() => b.desk.moving === null, slack(1500), 'lease expiry');
      expect(Date.now() - t0).toBeLessThan(slack(800));
      await waitFor(() => b.events.some((e) => e.event === 'lease_expired'), slack(500), 'lease_expired event');
    },
  },
  {
    id: 'reconnect',
    row: 'Reconnect after drop',
    title: 'reconnect after drop: no automatic restart',
    cases: CASES,
    async run({ open, slack, target }, c) {
      const b = await open(c);
      await holdFor(b, 300, 'desk.up');
      b.app.close(); // abrupt close: owner socket gone
      // B: the controller coasts up to one frame-hold (150 ms) after the last frame.
      await waitFor(() => b.desk.moving === null, slack(500), 'release on close');
      const again = appClient(b.baseUrl, 'mobile_app', target.name === 'board' ? process.env.AIRO_TOKEN : undefined);
      await again.connect();
      const h0 = b.desk.heightMm;
      await sleep(1000);
      expect(b.desk.heightMm).toBe(h0);
      expect((await again.getStatus()).body.state).toBe('IDLE');
      again.close();
    },
  },
  {
    id: 'dedupe',
    row: 'Repeated desk.up with same request_id',
    title: 'repeated desk.up with the same request_id executes once',
    cases: CASES,
    async run({ open, slack }, c) {
      const b = await open(c);
      const id = `dup-${Date.now()}`; // unique per run: the board keeps ids for 60 s
      const first = await b.app.rest('desk.up', { request_id: id, lease_ms: 300 });
      expect(first.body.result).toBe('accepted');
      await waitFor(() => b.desk.moving === 'up', slack(500), 'first command moves');
      await waitFor(() => b.desk.moving === null, slack(1500), 'first lease ends');
      const h0 = b.desk.heightMm;
      const dup = await b.app.rest('desk.up', { request_id: id, lease_ms: 300 });
      expect(dup.http).toBe(202);
      expect(dup.body.result).toBe('duplicate');
      await sleep(500);
      expect(b.desk.heightMm).toBe(h0);
    },
  },
  {
    id: 'stop-non-owner',
    row: 'desk.stop',
    title: 'desk.stop from a non-owner stops app movement',
    cases: CASES,
    async run({ open, slack, target }, c) {
      const b = await open(c);
      await holdFor(b, 300, 'desk.up');
      const other = appClient(b.baseUrl, 'home_assistant', target.name === 'board' ? process.env.AIRO_TOKEN : undefined);
      expect((await other.rest('desk.down')).body.code).toBe('busy');
      expect((await other.rest('desk.stop')).http).toBe(202);
      await waitFor(() => b.desk.moving === null, slack(300), 'stop');
    },
  },
  {
    id: 'stop-preset',
    row: 'desk.stop',
    title: 'desk.stop during a one-touch preset stops the desk (stop method, [ASSUMPTION])',
    cases: CASES,
    async run({ open, slack }, c) {
      const b = await open(c);
      await b.app.send('desk.standing');
      await sleep(800);
      expect(b.desk.moving).toBe('up');
      expect((await b.app.send('desk.stop')).result).toBe('accepted');
      await waitFor(() => b.desk.moving === null, slack(500), 'preset stopped');
      const h0 = b.desk.heightMm;
      await sleep(500);
      expect(b.desk.heightMm).toBe(h0);
    },
  },
  {
    id: 'manual-only',
    row: 'MANUAL ONLY',
    title: 'MANUAL ONLY: every app movement command is rejected and the keypad works',
    cases: CASES,
    async run({ open, slack }, c) {
      const b = await open(c);
      b.desk.rig.board.setManualOnly(true);
      b.desk.rig.flush();
      await waitFor(async () => (await b.app.getStatus()).body.state === 'BYPASS', slack(1000), 'BYPASS');
      for (const cmd of ['desk.up', 'desk.down', 'desk.sitting', 'desk.standing'] as const)
        expect((await b.app.rest(cmd)).body.code).toBe('bypass_active');
      const h0 = b.desk.heightMm;
      b.desk.rig.keypad.press('up');
      await sleep(300);
      expect(b.desk.heightMm).toBeGreaterThan(h0 + 3);
    },
  },
  {
    id: 'upper-limit',
    row: 'Upper/lower limit',
    title: 'upper limit: the desk never passes it',
    cases: CASES,
    async run({ open }, c) {
      const b = await open(c, { profile: shortProfile(30) });
      const max = b.desk.rig.sim.snapshot().profile.mechanical.maxHeightMm.value;
      await b.app.startHold('desk.up');
      await sleep(1800);
      await b.app.releaseHold();
      expect(b.desk.heightMm).toBeLessThanOrEqual(max);
      expect(b.desk.heightMm).toBeGreaterThan(max - 1);
    },
  },
  {
    id: 'limit-blocks',
    row: 'Upper/lower limit (height feedback)',
    title: 'limit reached: further movement in that direction is blocked',
    cases: ['B'],
    async run({ open }, c) {
      const b = await open(c, { profile: shortProfile(20) });
      await b.app.startHold('desk.up');
      await waitFor(() => b.events.some((e) => e.event === 'limit_reached' && e.direction === 'up'), 4000, 'limit_reached');
      expect((await b.app.rest('desk.up')).body.code).toBe('direction_blocked');
      expect((await b.app.rest('desk.down', { lease_ms: 200 })).body.result).toBe('accepted');
    },
  },
  {
    id: 'height-report',
    row: 'Height feedback',
    title: 'status reports height_mm in case B and null otherwise',
    cases: ['B'],
    async run({ open }) {
      let b = await open('B');
      await waitFor(async () => (await b.app.getStatus()).body.height_mm !== null, 2000, 'height');
      const s = (await b.app.getStatus()).body;
      expect(Math.abs(s.height_mm! - b.desk.heightMm)).toBeLessThan(2);
      await b.close();
      b = await open('A2');
      await sleep(500);
      expect((await b.app.getStatus()).body.height_mm).toBeNull();
    },
  },
  {
    id: 'presets-need-feedback',
    row: 'Presets and height feedback (Q14)',
    title: 'presets_require_height_feedback=true: refused without feedback, allowed with it',
    cases: ['A2'],
    async run({ open }) {
      let b = await open('A2', { config: { presets_require_height_feedback: true } });
      expect((await b.app.rest('desk.standing')).body.code).toBe('height_feedback_required');
      expect((await b.app.rest('desk.up', { lease_ms: 200 })).body.result).toBe('accepted');
      await b.close();
      b = await open('B', { config: { presets_require_height_feedback: true } });
      await waitFor(async () => (await b.app.getStatus()).body.height_mm !== null, 2000, 'height');
      expect((await b.app.rest('desk.standing')).body.result).toBe('accepted');
    },
  },
  {
    id: 'core-hang',
    row: 'Watchdog',
    title: 'a hung core is cut off by the watchdog; keypad still works',
    cases: ['A2'],
    async run({ open, slack }, c) {
      const b = await open(c);
      await holdFor(b, 300, 'desk.up');
      await b.hang();
      await waitFor(() => !b.desk.rig.board.watchdogOk() && b.desk.moving === null, slack(1000), 'watchdog');
      const h0 = b.desk.heightMm;
      b.desk.rig.keypad.press('up');
      await sleep(300);
      b.desk.rig.keypad.release('up');
      expect(b.desk.heightMm).toBeGreaterThan(h0 + 3);
      await b.resume();
    },
  },
  {
    id: 'api-surface',
    row: 'Auth, reset, info',
    title: 'auth, reset and info',
    cases: ['A1'],
    async run({ open, target }, c) {
      const b = await open(c);
      const bad = appClient(b.baseUrl, 'mobile_app', 'wrong');
      expect((await bad.rest('desk.up')).http).toBe(401);
      await expect(bad.connect()).rejects.toThrow();
      expect((await b.app.rest('desk.reset')).http).toBe(403);
      const info = await b.app.getInfo();
      expect(info).toMatchObject({ adapter: target.adapter, interface_case: 'A1', board_connected: true });
      expect(info.capabilities.presets_require_height_feedback).toBe(false);
      expect(info.capabilities.stop_method_verified).toBe(false);
    },
  },
  {
    id: 'case-switch',
    row: 'Interface case',
    title: 'switching the interface case reboots the core onto the new board',
    cases: ['A1'],
    async run({ open, slack }, c) {
      const b = await open(c);
      b.desk.rig.board.setInterfaceCase('B');
      b.desk.rig.flush();
      await waitFor(async () => (await b.app.getInfo()).interface_case === 'B', slack(1000), 'case B');
      await waitFor(async () => (await b.app.getStatus()).body.state === 'IDLE', 3000, 'IDLE');
      await holdFor(b, 400, 'desk.up');
      expect(b.desk.moving).toBe('up');
    },
  },
];

/** Comma-separated filters from the environment (empty = everything). */
export function selected(): { scenarios: Scenario[]; cases: InterfaceCase[] } {
  const ids = (process.env.AIRO_SCENARIOS ?? '').split(',').map((s) => s.trim()).filter(Boolean);
  const cases = (process.env.AIRO_CASES ?? '').split(',').map((s) => s.trim()).filter(Boolean) as InterfaceCase[];
  const unknown = ids.filter((id) => !SCENARIOS.some((s) => s.id === id));
  if (unknown.length) throw new Error(`unknown scenario id(s): ${unknown.join(', ')}. Known: ${SCENARIOS.map((s) => s.id).join(', ')}`);
  return {
    scenarios: ids.length ? SCENARIOS.filter((s) => ids.includes(s.id)) : SCENARIOS,
    cases: cases.length ? cases : CASES,
  };
}
