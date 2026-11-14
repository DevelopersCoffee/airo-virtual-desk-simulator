import { describe, expect, it } from 'vitest';
import { defaultProfile, withParam } from '../desk-profiles';
import { FixedStepClock } from './fixed-step-clock';
import { Simulation } from './simulation';

const instantMotor = withParam(defaultProfile, 'motor', 'spinUpTimeConstantS', 0);

describe('FixedStepClock', () => {
  it('produces the same number of steps regardless of frame size', () => {
    for (const fps of [24, 30, 60, 144, 240]) {
      const c = new FixedStepClock(0.001, 10_000);
      let steps = 0;
      for (let i = 0; i < fps * 2; i++) steps += c.advance(1 / fps);
      expect(steps).toBe(2000);
    }
  });

  it('caps the backlog after a long stall', () => {
    const c = new FixedStepClock(0.001, 100);
    expect(c.advance(5)).toBe(100);
    expect(c.advance(0.001)).toBe(1);
  });
});

describe('Simulation (Phase 1 scenarios)', () => {
  it('UP rotates the shaft and raises the tabletop', () => {
    const sim = new Simulation(instantMotor);
    const h0 = sim.snapshot().mechanical.heightMm;
    sim.command('up');
    sim.advance(1);
    const s = sim.snapshot();
    expect(s.mechanical.heightMm).toBeGreaterThan(h0);
    expect(s.mechanical.shaftRevs).toBeGreaterThan(0);
    expect(s.mechanical.shaftRpm).toBeGreaterThan(0);
    expect(s.controllerStatus).toBe('moving-up');
    // 3200 rpm / 20 / 60 * 12 = 32 mm/s placeholder values
    expect(s.mechanical.heightMm - h0).toBeCloseTo(32, 3);
  });

  it('reaches full placeholder speed with the default spin-up lag', () => {
    const sim = new Simulation(defaultProfile);
    sim.command('up');
    sim.advance(1);
    const m = sim.snapshot().mechanical;
    expect(m.motorRpm).toBeCloseTo(3200, 0);
    expect(m.liftSpeedMmPerS).toBeCloseTo(32, 2);
  });

  it('STOP immediately stops movement', () => {
    const sim = new Simulation(instantMotor);
    sim.command('up');
    sim.advance(1);
    sim.command('stop');
    const atStop = sim.snapshot().mechanical;
    expect(atStop.shaftRpm).toBe(0);
    sim.advance(1);
    expect(sim.snapshot().mechanical.heightMm).toBe(atStop.heightMm);
    expect(sim.snapshot().mechanical.shaftRevs).toBe(atStop.shaftRevs);
  });

  it('DOWN reverses the mechanism', () => {
    const sim = new Simulation(instantMotor);
    sim.command('up');
    sim.advance(2);
    sim.command('stop');
    const top = sim.snapshot().mechanical;
    sim.command('down');
    sim.advance(1);
    const s = sim.snapshot().mechanical;
    expect(s.heightMm).toBeLessThan(top.heightMm);
    expect(s.shaftRevs).toBeLessThan(top.shaftRevs);
    expect(s.shaftRpm).toBeLessThan(0);
  });

  it('keeps both columns synchronised through a whole run', () => {
    const sim = new Simulation(defaultProfile);
    sim.command('up');
    for (let i = 0; i < 300; i++) {
      sim.advance(1 / 60);
      expect(sim.snapshot().syncErrorMm).toBe(0);
    }
  });

  it('stops by itself at the upper limit', () => {
    const sim = new Simulation(instantMotor);
    sim.command('up');
    for (let i = 0; i < 40; i++) sim.advance(1); // 480 mm range at 32 mm/s = 15 s
    const s = sim.snapshot();
    expect(s.mechanical.heightMm).toBe(defaultProfile.mechanical.maxHeightMm.value);
    expect(s.controllerStatus).toBe('at-upper-limit');
    expect(s.mechanical.shaftRpm).toBe(0);
  });

  it('is deterministic and independent of render frame rate', () => {
    const runAt = (fps: number) => {
      const sim = new Simulation(defaultProfile);
      sim.command('up');
      for (let i = 0; i < fps * 3; i++) sim.advance(1 / fps);
      return sim.snapshot();
    };
    const a = runAt(30);
    const b = runAt(144);
    expect(a.steps).toBe(b.steps);
    expect(a.mechanical).toEqual(b.mechanical);
  });

  it('time scale slows the simulation, not the math', () => {
    const sim = new Simulation(instantMotor);
    sim.setTimeScale(0.1);
    sim.command('up');
    sim.advance(1);
    expect(sim.snapshot().timeS).toBeCloseTo(0.1, 9);
  });

  it('pause freezes and single step advances one timestep', () => {
    const sim = new Simulation(instantMotor);
    sim.command('up');
    sim.setPaused(true);
    expect(sim.advance(1)).toBe(0);
    sim.step();
    expect(sim.snapshot().steps).toBe(1);
  });

  it('reset returns to the start with no movement', () => {
    const sim = new Simulation(instantMotor);
    sim.command('up');
    sim.advance(2);
    sim.reset();
    sim.advance(1);
    const s = sim.snapshot();
    expect(s.mechanical.heightMm).toBe(defaultProfile.mechanical.minHeightMm.value);
    expect(s.mechanical.shaftRevs).toBe(0);
  });

  it('accepts changed placeholder values at runtime', () => {
    const sim = new Simulation(instantMotor);
    sim.setProfile(withParam(instantMotor, 'mechanical', 'gearRatio', 40));
    sim.command('up');
    sim.advance(1);
    expect(sim.snapshot().mechanical.liftSpeedMmPerS).toBeCloseTo(16, 6);
  });

  it('rejects invalid profiles', () => {
    expect(() => new Simulation(withParam(defaultProfile, 'mechanical', 'gearRatio', 0))).toThrow();
  });
});
