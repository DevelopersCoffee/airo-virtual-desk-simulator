/**
 * Pure transmission equations.
 *
 *   motor --(gearbox, ratio G)--> hex shaft --(column drive, L mm/rev)--> column travel
 *
 *   shaftRev    = motorRev / G
 *   travelMm    = shaftRev * L
 *   shaftRpm    = motorRpm / G
 *   liftSpeed   = shaftRpm / 60 * L          [mm/s]
 *
 * G and L come from the desk profile and are placeholders until measured.
 */

export interface TransmissionParams {
  /** Motor revolutions per shaft revolution. Must be > 0. */
  gearRatio: number;
  /** Column travel per shaft revolution in mm. Must be > 0. */
  travelPerShaftRevMm: number;
}

export function assertTransmission(p: TransmissionParams): void {
  if (!(p.gearRatio > 0) || !Number.isFinite(p.gearRatio)) {
    throw new RangeError(`gearRatio must be a positive finite number, got ${p.gearRatio}`);
  }
  if (!(p.travelPerShaftRevMm > 0) || !Number.isFinite(p.travelPerShaftRevMm)) {
    throw new RangeError(
      `travelPerShaftRevMm must be a positive finite number, got ${p.travelPerShaftRevMm}`,
    );
  }
}

export const shaftRevsFromMotorRevs = (motorRevs: number, p: TransmissionParams): number =>
  motorRevs / p.gearRatio;

export const motorRevsFromShaftRevs = (shaftRevs: number, p: TransmissionParams): number =>
  shaftRevs * p.gearRatio;

export const travelMmFromShaftRevs = (shaftRevs: number, p: TransmissionParams): number =>
  shaftRevs * p.travelPerShaftRevMm;

export const shaftRevsFromTravelMm = (travelMm: number, p: TransmissionParams): number =>
  travelMm / p.travelPerShaftRevMm;

export const shaftRpmFromMotorRpm = (motorRpm: number, p: TransmissionParams): number =>
  motorRpm / p.gearRatio;

export const motorRpmFromShaftRpm = (shaftRpm: number, p: TransmissionParams): number =>
  shaftRpm * p.gearRatio;

/** Vertical speed of the tabletop in mm/s for a given motor speed. */
export const liftSpeedMmPerS = (motorRpm: number, p: TransmissionParams): number =>
  (shaftRpmFromMotorRpm(motorRpm, p) / 60) * p.travelPerShaftRevMm;
