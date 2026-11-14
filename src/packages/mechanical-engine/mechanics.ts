import {
  assertTransmission,
  motorRevsFromShaftRevs,
  shaftRevsFromTravelMm,
  travelMmFromShaftRevs,
  type TransmissionParams,
} from './transmission';

export interface MechanicalParams extends TransmissionParams {
  minHeightMm: number;
  maxHeightMm: number;
}

export type TravelLimit = 'none' | 'upper' | 'lower';

export interface MechanicalState {
  /** Cumulative motor rotor revolutions (signed, + is the UP direction). */
  motorRevs: number;
  /** Cumulative hex-shaft revolutions. Primary and secondary shafts are rigidly coupled. */
  shaftRevs: number;
  /** Tabletop surface height above the floor, set by the left column. */
  leftColumnHeightMm: number;
  /** Tabletop surface height above the floor, set by the right column. */
  rightColumnHeightMm: number;
  /** Mean of the two columns. */
  heightMm: number;
  /** Actual motor speed after mechanical constraints (0 when blocked at a limit). */
  motorRpm: number;
  shaftRpm: number;
  /** Signed vertical speed, mm/s. */
  liftSpeedMmPerS: number;
  limit: TravelLimit;
  /** True when this step's requested travel was cut short by a travel limit. */
  blocked: boolean;
}

export function assertMechanical(p: MechanicalParams): void {
  assertTransmission(p);
  if (!(p.maxHeightMm > p.minHeightMm)) {
    throw new RangeError('maxHeightMm must be greater than minHeightMm');
  }
}

export function initialMechanicalState(p: MechanicalParams, heightMm = p.minHeightMm): MechanicalState {
  assertMechanical(p);
  const h = clamp(heightMm, p.minHeightMm, p.maxHeightMm);
  return {
    motorRevs: 0,
    shaftRevs: 0,
    leftColumnHeightMm: h,
    rightColumnHeightMm: h,
    heightMm: h,
    motorRpm: 0,
    shaftRpm: 0,
    liftSpeedMmPerS: 0,
    limit: limitAt(h, p),
    blocked: false,
  };
}

/**
 * Advance the mechanism by one fixed timestep.
 *
 * The commanded motor speed is converted to shaft rotation and then column
 * travel. Travel is clamped to the mechanical limits, and the shaft and motor
 * rotation are recomputed from the travel that actually happened, so the
 * angle of every part always agrees with the tabletop height.
 *
 * Phase 1 assumes a rigid coupling and identical column drives, so both
 * columns receive exactly the same travel.
 */
export function stepMechanics(
  state: MechanicalState,
  commandedMotorRpm: number,
  dtS: number,
  p: MechanicalParams,
): MechanicalState {
  if (!(dtS > 0)) throw new RangeError(`dtS must be positive, got ${dtS}`);

  const requestedMotorRevs = (commandedMotorRpm / 60) * dtS;
  const requestedShaftRevs = requestedMotorRevs / p.gearRatio;
  const requestedTravel = travelMmFromShaftRevs(requestedShaftRevs, p);

  const target = state.heightMm + requestedTravel;
  const newHeight = clamp(target, p.minHeightMm, p.maxHeightMm);
  const blocked = newHeight !== target;
  // Only re-derive travel when clamped, so free motion stays exact.
  const actualTravel = blocked ? newHeight - state.heightMm : requestedTravel;

  const actualShaftRevs = shaftRevsFromTravelMm(actualTravel, p);
  const actualMotorRevs = motorRevsFromShaftRevs(actualShaftRevs, p);

  const limit = limitAt(newHeight, p);

  const motorRpm = (actualMotorRevs / dtS) * 60;
  const shaftRpm = (actualShaftRevs / dtS) * 60;

  return {
    motorRevs: state.motorRevs + actualMotorRevs,
    shaftRevs: state.shaftRevs + actualShaftRevs,
    leftColumnHeightMm: state.leftColumnHeightMm + actualTravel,
    rightColumnHeightMm: state.rightColumnHeightMm + actualTravel,
    heightMm: newHeight,
    motorRpm,
    shaftRpm,
    liftSpeedMmPerS: actualTravel / dtS,
    limit,
    blocked,
  };
}

export const syncErrorMm = (s: MechanicalState): number =>
  Math.abs(s.leftColumnHeightMm - s.rightColumnHeightMm);

function limitAt(h: number, p: MechanicalParams): TravelLimit {
  if (h >= p.maxHeightMm) return 'upper';
  if (h <= p.minHeightMm) return 'lower';
  return 'none';
}

function clamp(v: number, lo: number, hi: number): number {
  return Math.min(hi, Math.max(lo, v));
}
