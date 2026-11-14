/**
 * Desk profile types.
 *
 * Every physical quantity that we have not measured on real hardware is a
 * `ConfigParam` carrying an explicit verification status, so the UI and any
 * report can always show which numbers are assumptions.
 */

export type VerificationStatus = 'unverified' | 'estimated' | 'measured';

export interface ConfigParam {
  value: number;
  unit: string;
  status: VerificationStatus;
  /** Where the value came from, or why it is a placeholder. */
  note?: string;
}

export interface MechanicalConfig {
  /**
   * Gearbox reduction, motor revolutions per output (hex shaft) revolution.
   * Example: 20 means the motor turns 20 times for one shaft turn.
   */
  gearRatio: ConfigParam;
  /**
   * Vertical travel of each lifting column per hex-shaft revolution, in mm.
   * This lumps together the column's internal bevel stage and lead screw.
   */
  travelPerShaftRevMm: ConfigParam;
  /** Tabletop surface height (floor to top surface) at the lower travel limit. */
  minHeightMm: ConfigParam;
  /** Tabletop surface height at the upper travel limit. */
  maxHeightMm: ConfigParam;
}

export interface MotorConfig {
  /** Motor speed at full command with no load (stub motor model). */
  noLoadRpm: ConfigParam;
  /** First-order spin-up time constant of the stub motor model. */
  spinUpTimeConstantS: ConfigParam;
}

export interface DeskProfile {
  id: string;
  name: string;
  /** Profile schema version, bumped when the shape changes. */
  version: number;
  description: string;
  mechanical: MechanicalConfig;
  motor: MotorConfig;
}
