import { describe, expect, it } from 'vitest';
import { initialMechanicalState, stepMechanics, syncErrorMm, type MechanicalParams } from './mechanics';

const p: MechanicalParams = { gearRatio: 20, travelPerShaftRevMm: 12, minHeightMm: 700, maxHeightMm: 1200 };
const dt = 0.001;

function run(state = initialMechanicalState(p), rpm: number, seconds: number) {
  let s = state;
  const n = Math.round(seconds / dt);
  for (let i = 0; i < n; i++) s = stepMechanics(s, rpm, dt, p);
  return s;
}

describe('stepMechanics', () => {
  it('starts at the lower limit with both columns level', () => {
    const s = initialMechanicalState(p);
    expect(s.heightMm).toBe(700);
    expect(s.limit).toBe('lower');
    expect(syncErrorMm(s)).toBe(0);
  });

  it('raises the tabletop by shaft revolutions x travel per revolution', () => {
    const s = run(undefined, 3200, 2); // 2 s at 32 mm/s
    expect(s.heightMm).toBeCloseTo(764, 6);
    expect(s.shaftRevs).toBeCloseTo(64 / 12, 9);
    expect(s.motorRevs).toBeCloseTo((64 / 12) * 20, 9);
    expect(s.liftSpeedMmPerS).toBeCloseTo(32, 6);
    expect(s.shaftRpm).toBeCloseTo(160, 6);
  });

  it('keeps height, shaft angle and motor angle mutually consistent at every step', () => {
    let s = initialMechanicalState(p, 900);
    const rpms = [3200, 3200, -1500, 0, 800, -3200];
    for (let i = 0; i < 3000; i++) {
      s = stepMechanics(s, rpms[i % rpms.length]!, dt, p);
      expect(s.heightMm - 900).toBeCloseTo(s.shaftRevs * p.travelPerShaftRevMm, 9);
      expect(s.motorRevs).toBeCloseTo(s.shaftRevs * p.gearRatio, 9);
    }
  });

  it('keeps both columns synchronised', () => {
    let s = initialMechanicalState(p);
    for (let i = 0; i < 5000; i++) {
      s = stepMechanics(s, i < 3000 ? 3200 : -2000, dt, p);
      expect(syncErrorMm(s)).toBe(0);
      expect(s.leftColumnHeightMm).toBe(s.rightColumnHeightMm);
    }
  });

  it('reverses exactly: DOWN for the same time undoes UP', () => {
    const up = run(initialMechanicalState(p, 900), 3200, 1.5);
    const back = run(up, -3200, 1.5);
    expect(back.heightMm).toBeCloseTo(900, 6);
    expect(back.shaftRevs).toBeCloseTo(0, 9);
  });

  it('stops at the upper travel limit and stops the shaft with it', () => {
    const s = run(initialMechanicalState(p, 1190), 3200, 2);
    expect(s.heightMm).toBe(1200);
    expect(s.limit).toBe('upper');
    expect(s.shaftRpm).toBe(0);
    expect(s.motorRpm).toBe(0);
    expect(s.shaftRevs).toBeCloseTo(10 / 12, 9);
    expect(s.blocked).toBe(true);
  });

  it('is not reported as blocked during free travel', () => {
    let s = initialMechanicalState(p, 900);
    for (let i = 0; i < 2000; i++) {
      s = stepMechanics(s, 3200, dt, p);
      expect(s.blocked).toBe(false);
      expect(s.motorRpm).toBeCloseTo(3200, 6);
    }
  });

  it('stops at the lower travel limit', () => {
    const s = run(initialMechanicalState(p, 710), -3200, 2);
    expect(s.heightMm).toBe(700);
    expect(s.limit).toBe('lower');
    expect(s.motorRpm).toBe(0);
  });

  it('can leave a limit in the opposite direction', () => {
    const s = run(initialMechanicalState(p), 3200, 0.1);
    expect(s.heightMm).toBeGreaterThan(700);
    expect(s.limit).toBe('none');
  });

  it('zero command means no motion', () => {
    const s0 = initialMechanicalState(p, 900);
    const s = run(s0, 0, 1);
    expect(s.heightMm).toBe(900);
    expect(s.shaftRevs).toBe(0);
  });

  it('follows a different configured relationship without code changes', () => {
    const q: MechanicalParams = { gearRatio: 40, travelPerShaftRevMm: 3, minHeightMm: 600, maxHeightMm: 1300 };
    let s = initialMechanicalState(q);
    for (let i = 0; i < 1000; i++) s = stepMechanics(s, 2400, dt, q);
    // 2400 rpm / 40 = 60 shaft rpm = 1 rev/s, x 3 mm = 3 mm in 1 s
    expect(s.heightMm).toBeCloseTo(603, 6);
  });

  it('rejects invalid parameters and timesteps', () => {
    expect(() => initialMechanicalState({ ...p, maxHeightMm: 600 })).toThrow(RangeError);
    expect(() => stepMechanics(initialMechanicalState(p), 100, 0, p)).toThrow(RangeError);
  });
});
