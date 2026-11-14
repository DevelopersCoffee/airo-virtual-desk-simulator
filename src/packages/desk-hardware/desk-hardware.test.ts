import { describe, expect, it } from 'vitest';
import { DeskRig } from '../airo-link';
import { FrameDecoder, buttonFrame, encode, heightReport, HANDSET } from './frames';
import type { CoreToBoard } from './interface-board';

/** A rig with no core attached, stepped 1 ms at a time in simulated time. */
function rig(c: 'A1' | 'A2' | 'B' = 'A2') {
  const r = new DeskRig({ interfaceCase: c });
  const run = (ms: number) => {
    for (let i = 0; i < ms; i++) r.step();
  };
  const core = (m: CoreToBoard) => r.board.receive(m);
  const h = () => r.sim.snapshot().mechanical.heightMm;
  return { r, run, core, h };
}

describe('frames', () => {
  it('matches the reported handset frames and round-trips height reports', () => {
    expect(buttonFrame('up')).toEqual([0xf1, 0xf1, 0x01, 0x00, 0x01, 0x7e]);
    const d = new FrameDecoder();
    const [f] = d.feed(heightReport(1286));
    expect(f).toMatchObject({ cmd: 1, params: [5, 6, 0] });
    expect(d.feed(encode(HANDSET, 1).map((b, i) => (i === 4 ? b + 1 : b)))).toEqual([]);
    expect(d.bad).toBe(1);
  });
});

describe('original desk controller (desk model, not Airo logic)', () => {
  it('keypad UP/DOWN are hold-to-run', () => {
    const { r, run, h } = rig('A1');
    const h0 = h();
    r.keypad.press('up');
    run(500);
    expect(h()).toBeGreaterThan(h0 + 10);
    r.keypad.release('up');
    const h1 = h();
    run(300);
    expect(h()).toBe(h1);
    r.keypad.press('down');
    run(300);
    expect(h()).toBeLessThan(h1);
  });

  it('sit/stand are one-touch presets to the stored heights', () => {
    const { r, run, h } = rig('A2');
    r.keypad.press('stand');
    run(50);
    r.keypad.release('stand');
    run(20000);
    expect(Math.abs(h() - r.controller.options.standHeightMm)).toBeLessThan(1);
    r.keypad.press('sit');
    run(50);
    r.keypad.release('sit');
    run(20000);
    expect(Math.abs(h() - r.controller.options.sitHeightMm)).toBeLessThan(1);
  });

  it('any key stops a preset and nothing restarts until keys are released', () => {
    const { r, run, h } = rig('A2');
    r.keypad.press('stand');
    run(30);
    r.keypad.release('stand');
    run(1000);
    r.keypad.press('down');
    run(10);
    const stopped = h();
    run(500);
    expect(h()).toBe(stopped);
    expect(r.controller.log.map((e) => e.kind)).toContain('preset-stopped');
  });

  it('stops itself at the upper limit', () => {
    const { r, run, h } = rig('A1');
    r.keypad.press('up');
    run(25000);
    expect(h()).toBe(r.sim.snapshot().profile.mechanical.maxHeightMm.value);
    expect(r.sim.snapshot().controllerStatus).toBe('at-upper-limit');
  });

  it('encoded lines: a person pressing UP+DOWN together reads as Preset 1 (the hazard)', () => {
    const { r, run } = rig('A2');
    r.keypad.press('up');
    r.keypad.press('down');
    run(10);
    expect(r.controller.log.map((e) => e.kind)).toContain('preset-sit');
  });
});

describe('interface board fail-safe (spec 6.1)', () => {
  it('with no core, the keypad drives the desk in every case (R5)', () => {
    for (const c of ['A1', 'A2', 'B'] as const) {
      const { r, run, h } = rig(c);
      const h0 = h();
      r.keypad.press('up');
      run(400);
      expect(h(), c).toBeGreaterThan(h0 + 5);
      expect(r.board.snapshot().selector).not.toBe('airo');
    }
  });

  it('A2 selector: Airo drives only while armed and heartbeating; keypad is cut off meanwhile', () => {
    const { r, run, core, h } = rig('A2');
    core({ t: 'hb' });
    core({ t: 'arm', on: true });
    core({ t: 'inject', hs: 2 });
    expect(r.board.snapshot().selector).toBe('airo');
    const h0 = h();
    for (let i = 0; i < 10; i++) {
      run(50);
      core({ t: 'hb' });
    }
    expect(h()).toBeGreaterThan(h0 + 5);
    // Keypad DOWN while K1 is energised never merges with Airo's UP (no code 3).
    r.keypad.press('down');
    run(10);
    expect(r.controller.log.map((e) => e.kind)).not.toContain('preset-sit');
    expect(r.board.snapshot().encodedAtController).toBe(2);
  });

  it('watchdog drops the Airo path within 200 ms of the last heartbeat (R7)', () => {
    const { r, run, core } = rig('A2');
    core({ t: 'hb' });
    core({ t: 'arm', on: true });
    core({ t: 'inject', hs: 2 });
    run(199);
    expect(r.board.airoPath()).toBe(true);
    run(3);
    expect(r.board.airoPath()).toBe(false);
    expect(r.board.snapshot().selector).toBe('keypad');
  });

  it('MANUAL ONLY cuts injectors in hardware even if the core keeps driving (R6)', () => {
    const { r, run, core, h } = rig('A1');
    r.board.setManualOnly(true);
    const h0 = h();
    for (let i = 0; i < 10; i++) {
      core({ t: 'hb' });
      core({ t: 'inject', lines: ['up'] });
      run(50);
    }
    expect(h()).toBe(h0);
    r.keypad.press('up');
    run(300);
    expect(h()).toBeGreaterThan(h0);
  });

  it('cutting Airo power returns the controller to the keypad immediately', () => {
    const { r, run, core } = rig('B');
    core({ t: 'hb' });
    core({ t: 'arm', on: true });
    expect(r.board.airoPath()).toBe(true);
    r.board.setCorePowered(false);
    run(1);
    expect(r.board.airoPath()).toBe(false);
    core({ t: 'hb' }); // an unpowered core drives nothing
    core({ t: 'arm', on: true });
    expect(r.board.airoPath()).toBe(false);
    expect(r.board.takeOutbox()).toContainEqual({ t: 'sim.power', on: false });
  });

  it('digital case: the controller reports height on DTX for the core', () => {
    const { r, run } = rig('B');
    run(450);
    const dtx = r.board.takeOutbox().filter((m) => m.t === 'uart' && m.line === 'dtx');
    expect(dtx.length).toBeGreaterThanOrEqual(2);
  });
});
