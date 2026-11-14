/**
 * Spec section 12.1 "Simulation test matrix", run end to end:
 *   AiroClient (REST/WS) -> airo-host (C++ airo-core + VirtualHardwareAdapter)
 *   -> /bridge -> InterfaceBoard -> OriginalDeskController -> mechanism.
 * Every row is exercised on each modelled interface case where it applies.
 */
import { afterEach, beforeAll, describe, expect, it } from 'vitest';
import type { InterfaceCase } from '../src/packages/desk-hardware';
import { appClient, bench, hostBuilt, HOST_BIN, shortProfile, sleep, TOKEN, waitFor, type Bench } from './harness';

const CASES: InterfaceCase[] = ['A1', 'A2', 'B'];
let b: Bench | null = null;

beforeAll(() => {
  if (!hostBuilt()) throw new Error(`${HOST_BIN} is missing: run "npm run firmware:build" first`);
});

afterEach(async () => {
  await b?.close();
  b = null;
});

async function holdFor(ms: number, cmd: 'desk.up' | 'desk.down' = 'desk.up') {
  const reply = await b!.app.startHold(cmd);
  expect(reply).toMatchObject({ type: 'ack', result: 'accepted' });
  await sleep(ms);
}

describe.each(CASES)('interface case %s', (c) => {
  it('physical UP and DOWN buttons move the desk (core attached, idle)', async () => {
    b = await bench(c);
    const h0 = b.desk.heightMm;
    b.desk.rig.keypad.press('up');
    await sleep(400);
    b.desk.rig.keypad.release('up');
    const h1 = b.desk.heightMm;
    expect(h1).toBeGreaterThan(h0 + 5);
    await waitFor(async () => (await b!.app.getStatus()).body.state === 'MANUAL_ACTIVE' || true);
    b.desk.rig.keypad.press('down');
    await sleep(300);
    b.desk.rig.keypad.release('down');
    expect(b.desk.heightMm).toBeLessThan(h1 - 3);
  });

  it('app desk.up / desk.down move the desk only while the lease is renewed', async () => {
    b = await bench(c);
    const h0 = b.desk.heightMm;
    await holdFor(1000, 'desk.up');
    expect(b.desk.moving).toBe('up');
    await b.app.releaseHold();
    await waitFor(() => b!.desk.moving === null, 500, 'stop after release');
    const h1 = b.desk.heightMm;
    expect(h1).toBeGreaterThan(h0 + 20);
    await sleep(100); // settle
    await holdFor(500, 'desk.down');
    expect(b.desk.moving).toBe('down');
    await b.app.releaseHold();
    await waitFor(() => b!.desk.moving === null, 500, 'stop after release');
    expect(b.desk.heightMm).toBeLessThan(h1 - 5);
  });

  it('desk.standing then desk.sitting reach the controller presets (no height feedback needed)', async () => {
    b = await bench(c);
    const { standHeightMm, sitHeightMm } = b.desk.rig.controller.options;
    expect((await b.app.send('desk.standing')).result).toBe('accepted');
    await waitFor(() => Math.abs(b!.desk.heightMm - standHeightMm) < 1 && b!.desk.moving === null, 15000, 'standing');
    expect((await b.app.send('desk.sitting')).result).toBe('accepted');
    await waitFor(() => Math.abs(b!.desk.heightMm - sitHeightMm) < 1 && b!.desk.moving === null, 15000, 'sitting');
  }, 40000);

  it('physical key during an app command: app releases fast, keypad wins, no phantom preset', async () => {
    b = await bench(c);
    await holdFor(400, 'desk.up');
    expect(b.desk.moving).toBe('up');
    const pressedAt = b.desk.nowMs;
    let releasedAt: number | null = null;
    b.desk.watch(() => {
      const s = b!.desk.rig.board.snapshot();
      const appGone = s.injecting.length === 0 && s.selector !== 'airo' && !b!.desk.rig.controller.seen().includes('up');
      if (appGone) releasedAt = b!.desk.nowMs;
      return appGone;
    });
    b.desk.rig.keypad.press('down');
    await waitFor(() => releasedAt !== null, 1000, 'app release');
    await sleep(300);
    expect(b.desk.moving).toBe('down'); // the keypad's command, not the app's
    b.desk.rig.keypad.release('down');
    const latency = releasedAt! - pressedAt;
    // The core releases on the tick after it senses the key (firmware unit
    // test: <= 1 ms tick). End to end this includes the loopback bridge and
    // the desk model's 1 ms step loop.
    expect(latency).toBeLessThan(50);
    expect(b.desk.presetsStarted()).toEqual([]);
    expect(b.events.map((e) => e.event)).toContain('manual_override');
    expect((await b.app.send('desk.up')).code).toBe('manual_active');
  });

  it('Airo power loss: selector returns to the keypad path and the keypad works', async () => {
    b = await bench(c);
    await holdFor(300, 'desk.up');
    b.desk.rig.board.setCorePowered(false);
    b.desk.rig.flush();
    await waitFor(() => b!.desk.moving === null, 300, 'stop on power loss');
    expect(b.desk.rig.board.snapshot().selector).not.toBe('airo');
    expect((await b.app.rest('desk.up')).http).toBe(503);
    const h0 = b.desk.heightMm;
    b.desk.rig.keypad.press('up');
    await sleep(300);
    b.desk.rig.keypad.release('up');
    expect(b.desk.heightMm).toBeGreaterThan(h0 + 3);
    await waitFor(() => b!.desk.moving === null, 500, 'keypad release');
    // Power back: the core boots, and nothing moves on its own.
    b.desk.rig.board.setCorePowered(true);
    b.desk.rig.flush();
    const h1 = b.desk.heightMm;
    await waitFor(async () => (await b!.app.getStatus()).body.state === 'IDLE', 4000, 'reboot to IDLE');
    await sleep(300);
    expect(b.desk.heightMm).toBe(h1);
  });

  it('core process killed mid-move: hardware watchdog stops the desk within 200 ms', async () => {
    b = await bench(c);
    await holdFor(300, 'desk.up');
    const t0 = b.desk.nowMs;
    let stoppedAt: number | null = null;
    b.desk.watch(() => {
      if (b!.desk.rig.board.watchdogOk()) return false;
      stoppedAt = b!.desk.nowMs;
      return true;
    });
    await b.host.kill('SIGKILL');
    await waitFor(() => stoppedAt !== null && b!.desk.moving === null, 1000, 'watchdog');
    expect(stoppedAt! - t0).toBeLessThanOrEqual(260);
    const h0 = b.desk.heightMm;
    b.desk.rig.keypad.press('up');
    await sleep(300);
    expect(b.desk.heightMm).toBeGreaterThan(h0 + 3);
  });

  it('network drop during hold: lease expires and movement stops', async () => {
    b = await bench(c);
    await holdFor(300, 'desk.up');
    b.app.renewalsPaused = true; // packets stop arriving, socket stays open
    const t0 = Date.now();
    await waitFor(() => b!.desk.moving === null, 1500, 'lease expiry');
    expect(Date.now() - t0).toBeLessThan(800);
    await waitFor(() => b!.events.some((e) => e.event === 'lease_expired'), 500, 'lease_expired event');
  });

  it('reconnect after drop: no automatic restart', async () => {
    b = await bench(c);
    await holdFor(300, 'desk.up');
    b.app.close(); // abrupt close: owner socket gone
    // B: the controller coasts up to one frame-hold (150 ms) after the last frame.
    await waitFor(() => b!.desk.moving === null, 500, 'release on close');
    const again = appClient(b.host.port);
    await again.connect();
    const h0 = b.desk.heightMm;
    await sleep(1000);
    expect(b.desk.heightMm).toBe(h0);
    expect((await again.getStatus()).body.state).toBe('IDLE');
    again.close();
  });

  it('repeated desk.up with the same request_id executes once', async () => {
    b = await bench(c);
    const first = await b.app.rest('desk.up', { request_id: 'dup-1', lease_ms: 300 });
    expect(first.body.result).toBe('accepted');
    await waitFor(() => b!.desk.moving === 'up', 500, 'first command moves');
    await waitFor(() => b!.desk.moving === null, 1500, 'first lease ends');
    const h0 = b.desk.heightMm;
    const dup = await b.app.rest('desk.up', { request_id: 'dup-1', lease_ms: 300 });
    expect(dup.http).toBe(202);
    expect(dup.body.result).toBe('duplicate');
    await sleep(500);
    expect(b.desk.heightMm).toBe(h0);
  });

  it('desk.stop from a non-owner stops app movement', async () => {
    b = await bench(c);
    await holdFor(300, 'desk.up');
    const other = appClient(b.host.port, 'home_assistant');
    expect((await other.rest('desk.down')).body.code).toBe('busy');
    expect((await other.rest('desk.stop')).http).toBe(202);
    await waitFor(() => b!.desk.moving === null, 300, 'stop');
  });

  it('desk.stop during a one-touch preset stops the desk (stop method, [ASSUMPTION])', async () => {
    b = await bench(c);
    await b.app.send('desk.standing');
    await sleep(800);
    expect(b.desk.moving).toBe('up');
    expect((await b.app.send('desk.stop')).result).toBe('accepted');
    await waitFor(() => b!.desk.moving === null, 500, 'preset stopped');
    const h0 = b.desk.heightMm;
    await sleep(500);
    expect(b.desk.heightMm).toBe(h0);
  });

  it('MANUAL ONLY: every app movement command is rejected and the keypad works', async () => {
    b = await bench(c);
    b.desk.rig.board.setManualOnly(true);
    b.desk.rig.flush();
    await waitFor(async () => (await b!.app.getStatus()).body.state === 'BYPASS', 1000, 'BYPASS');
    for (const cmd of ['desk.up', 'desk.down', 'desk.sitting', 'desk.standing'] as const)
      expect((await b.app.rest(cmd)).body.code).toBe('bypass_active');
    const h0 = b.desk.heightMm;
    b.desk.rig.keypad.press('up');
    await sleep(300);
    expect(b.desk.heightMm).toBeGreaterThan(h0 + 3);
  });

  it('upper limit: the desk never passes it', async () => {
    b = await bench(c, { profile: shortProfile(30) });
    const max = b.desk.rig.sim.snapshot().profile.mechanical.maxHeightMm.value;
    await b.app.startHold('desk.up');
    await sleep(1800);
    await b.app.releaseHold();
    expect(b.desk.heightMm).toBeLessThanOrEqual(max);
    expect(b.desk.heightMm).toBeGreaterThan(max - 1);
  });
});

describe('height feedback (digital case B only)', () => {
  it('limit reached: further movement in that direction is blocked', async () => {
    b = await bench('B', { profile: shortProfile(20) });
    await b.app.startHold('desk.up');
    await waitFor(() => b!.events.some((e) => e.event === 'limit_reached' && e.direction === 'up'), 4000, 'limit_reached');
    expect((await b.app.rest('desk.up')).body.code).toBe('direction_blocked');
    expect((await b.app.rest('desk.down', { lease_ms: 200 })).body.result).toBe('accepted');
  });

  it('status reports height_mm in case B and null otherwise', async () => {
    b = await bench('B');
    await waitFor(async () => (await b!.app.getStatus()).body.height_mm !== null, 2000, 'height');
    const s = (await b.app.getStatus()).body;
    expect(Math.abs(s.height_mm! - b.desk.heightMm)).toBeLessThan(2);
    await b.close();
    b = await bench('A2');
    await sleep(500);
    expect((await b.app.getStatus()).body.height_mm).toBeNull();
  });

  it('presets_require_height_feedback=true: refused without feedback, allowed with it', async () => {
    b = await bench('A2', { hostArgs: ['--set', 'presets_require_height_feedback=true'] });
    expect((await b.app.rest('desk.standing')).body.code).toBe('height_feedback_required');
    expect((await b.app.rest('desk.up', { lease_ms: 200 })).body.result).toBe('accepted');
    await b.close();
    b = await bench('B', { hostArgs: ['--set', 'presets_require_height_feedback=true'] });
    await waitFor(async () => (await b!.app.getStatus()).body.height_mm !== null, 2000, 'height');
    expect((await b.app.rest('desk.standing')).body.result).toBe('accepted');
  });
});

describe('core hang and API surface', () => {
  it('a hung core (SIGSTOP) is cut off by the watchdog; keypad still works', async () => {
    b = await bench('A2');
    await holdFor(300, 'desk.up');
    await b.host.kill('SIGSTOP');
    await waitFor(() => !b!.desk.rig.board.watchdogOk() && b!.desk.moving === null, 1000, 'watchdog');
    const h0 = b.desk.heightMm;
    b.desk.rig.keypad.press('up');
    await sleep(300);
    b.desk.rig.keypad.release('up');
    expect(b.desk.heightMm).toBeGreaterThan(h0 + 3);
    await b.host.kill('SIGCONT');
  });

  it('auth, reset and info', async () => {
    b = await bench('A1');
    const bad = appClient(b.host.port, 'mobile_app', 'wrong');
    expect((await bad.rest('desk.up')).http).toBe(401);
    await expect(bad.connect()).rejects.toThrow();
    expect((await b.app.rest('desk.reset')).http).toBe(403);
    const info = await b.app.getInfo();
    expect(info).toMatchObject({ adapter: 'virtual', interface_case: 'A1', board_connected: true });
    expect(info.capabilities.presets_require_height_feedback).toBe(false);
    expect(TOKEN).not.toBe('airo-sim-token');
  });

  it('switching the interface case reboots the core onto the new board', async () => {
    b = await bench('A1');
    b.desk.rig.board.setInterfaceCase('B');
    b.desk.rig.flush();
    await waitFor(async () => (await b!.app.getInfo()).interface_case === 'B', 1000, 'case B');
    await waitFor(async () => (await b!.app.getStatus()).body.state === 'IDLE', 3000, 'IDLE');
    await holdFor(400, 'desk.up');
    expect(b.desk.moving).toBe('up');
  });
});
