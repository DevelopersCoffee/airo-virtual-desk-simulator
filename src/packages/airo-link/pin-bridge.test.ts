import { describe, expect, it } from 'vitest';
import { benchRefusals } from './bench-guard';
import { PinTranslator } from './pin-bridge';
import type { AiroInfo } from './airo-client';

describe('PinTranslator (desk model <-> pin-level bench link)', () => {
  it('A1: keypad sense lines become sense_* inputs and injectors become inject lines', () => {
    const t = new PinTranslator();
    t.toBench({ t: 'hello', case: 'A1', manual_only: false, powered: true });
    expect(t.toBench({ t: 'sense', lines: ['down'] })).toEqual([
      { t: 'in', s: { sense_up: false, sense_down: true, sense_sit: false, sense_stand: false, sense_reset: false } },
    ]);
    expect(t.fromBench({ t: 'out', s: { inj_up: true, inj_down: false, inj_sit: false, inj_stand: false } })).toEqual([
      { t: 'inject', lines: ['up'] },
    ]);
    expect(t.fromBench({ t: 'out', s: { inj_up: false } })).toEqual([{ t: 'inject', lines: [] }]);
  });

  it('A2: encoded bits both ways', () => {
    const t = new PinTranslator();
    t.toBench({ t: 'hello', case: 'A2', manual_only: false, powered: true });
    expect(t.toBench({ t: 'sense', hs: 3 })).toEqual([
      { t: 'in', s: { sense_hs0: true, sense_hs1: true, sense_hs2: false, sense_hs3: false } },
    ]);
    expect(t.fromBench({ t: 'out', s: { inj_hs0: false, inj_hs1: true, inj_hs2: false, inj_hs3: false } })).toEqual([
      { t: 'inject', hs: 2 },
    ]);
  });

  it('heartbeat edges become hb, arm changes become arm, UART passes through', () => {
    const t = new PinTranslator();
    t.toBench({ t: 'hello', case: 'B', manual_only: false, powered: true });
    expect(t.fromBench({ t: 'out', s: { heartbeat: true } })).toEqual([{ t: 'hb' }]);
    expect(t.fromBench({ t: 'out', s: { heartbeat: false, arm: true } })).toEqual([{ t: 'hb' }, { t: 'arm', on: true }]);
    expect(t.fromBench({ t: 'tx', line: 'htx', bytes: [0xf1] })).toEqual([{ t: 'uart', line: 'htx', bytes: [0xf1] }]);
    expect(t.toBench({ t: 'uart', line: 'dtx', bytes: [1, 2] })).toEqual([{ t: 'rx', line: 'dtx', bytes: [1, 2] }]);
  });

  it('bypass, power and case changes', () => {
    const t = new PinTranslator();
    t.toBench({ t: 'hello', case: 'A1', manual_only: false, powered: true });
    expect(t.toBench({ t: 'bypass', on: true })).toEqual([{ t: 'in', s: { bypass_state: true } }]);
    expect(t.toBench({ t: 'sim.power', on: false })).toEqual([{ t: 'power', on: false }]);
    expect(t.toBench({ t: 'case', case: 'B' })).toEqual([{ t: 'hello', case: 'B', manual_only: true, powered: false }]);
  });
});

describe('benchRefusals (the HIL suite only runs on a board in bench mode)', () => {
  const base = { adapter: 'esp32' } as AiroInfo;
  const safe = { bench_mode: true, mode: 'bench' as const, drive_enabled: false, inputs: 'link' as const, desk_mode_compiled: false };

  it('accepts a board in bench mode', () => {
    expect(benchRefusals({ ...base, bench: safe })).toEqual([]);
  });

  it('refuses no info, no bench state, desk mode, desk-mode builds, drive on and physical inputs', () => {
    expect(benchRefusals(null)).toHaveLength(1);
    expect(benchRefusals({ ...base, adapter: 'virtual' })[0]).toMatch(/no bench state/);
    expect(benchRefusals({ ...base, bench: { ...safe, bench_mode: false, mode: 'desk' } })[0]).toMatch(/not in bench mode/);
    expect(benchRefusals({ ...base, bench: { ...safe, desk_mode_compiled: true } })[0]).toMatch(/desk mode/);
    expect(benchRefusals({ ...base, bench: { ...safe, drive_enabled: true } })[0]).toMatch(/drive is ON/);
    expect(benchRefusals({ ...base, bench: { ...safe, drive_enabled: true } }, { allowDrive: true })).toEqual([]);
    expect(benchRefusals({ ...base, bench: { ...safe, inputs: 'physical' } })[0]).toMatch(/inputs/);
  });
});
