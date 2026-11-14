import { describe, expect, it } from 'vitest';
import {
  assertTransmission,
  liftSpeedMmPerS,
  motorRevsFromShaftRevs,
  motorRpmFromShaftRpm,
  shaftRevsFromMotorRevs,
  shaftRevsFromTravelMm,
  shaftRpmFromMotorRpm,
  travelMmFromShaftRevs,
} from './transmission';

const p = { gearRatio: 20, travelPerShaftRevMm: 12 };

describe('transmission equations', () => {
  it('reduces motor revolutions by the gear ratio', () => {
    expect(shaftRevsFromMotorRevs(200, p)).toBe(10);
    expect(motorRevsFromShaftRevs(10, p)).toBe(200);
  });

  it('converts shaft revolutions to column travel with the configured lead', () => {
    expect(travelMmFromShaftRevs(10, p)).toBe(120);
    expect(shaftRevsFromTravelMm(120, p)).toBe(10);
  });

  it('keeps the sign so DOWN is the exact reverse of UP', () => {
    expect(travelMmFromShaftRevs(shaftRevsFromMotorRevs(-200, p), p)).toBe(-120);
  });

  it('converts speeds consistently', () => {
    expect(shaftRpmFromMotorRpm(3200, p)).toBe(160);
    expect(motorRpmFromShaftRpm(160, p)).toBe(3200);
    // 160 shaft rpm = 2.667 rev/s, x 12 mm = 32 mm/s
    expect(liftSpeedMmPerS(3200, p)).toBeCloseTo(32, 10);
  });

  it('uses the configured values rather than any built-in constant', () => {
    const other = { gearRatio: 36.5, travelPerShaftRevMm: 4.2 };
    expect(liftSpeedMmPerS(3000, other)).toBeCloseTo((3000 / 36.5 / 60) * 4.2, 12);
  });

  it('round-trips motor revs to travel and back', () => {
    for (const revs of [0, 1, 17.3, -42, 1e5]) {
      const travel = travelMmFromShaftRevs(shaftRevsFromMotorRevs(revs, p), p);
      expect(motorRevsFromShaftRevs(shaftRevsFromTravelMm(travel, p), p)).toBeCloseTo(revs, 9);
    }
  });

  it('rejects non-physical parameters', () => {
    expect(() => assertTransmission({ gearRatio: 0, travelPerShaftRevMm: 1 })).toThrow(RangeError);
    expect(() => assertTransmission({ gearRatio: -5, travelPerShaftRevMm: 1 })).toThrow(RangeError);
    expect(() => assertTransmission({ gearRatio: 10, travelPerShaftRevMm: NaN })).toThrow(RangeError);
    expect(() => assertTransmission({ gearRatio: Infinity, travelPerShaftRevMm: 1 })).toThrow(RangeError);
  });
});
