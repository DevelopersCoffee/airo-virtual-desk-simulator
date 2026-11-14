/**
 * Motor abstraction.
 *
 * The simulation only talks to a motor through this interface, so the stub
 * below can later be replaced by a DC motor model (Phase 2) or by an adapter
 * for real hardware, without touching the mechanical engine or controller.
 */

export interface MotorInput {
  /** Normalised command, -1 (full DOWN) .. +1 (full UP). 0 means brake. */
  dutyCycle: number;
  dtS: number;
}

export interface MotorState {
  /** Rotor speed the motor is trying to deliver, rpm (signed). */
  rpm: number;
  dutyCycle: number;
}

export interface MotorModel {
  readonly kind: string;
  step(input: MotorInput): MotorState;
  /** Tell the motor the mechanism is blocked so it does not keep spinning freely. */
  notifyBlocked(): void;
  reset(): void;
  readonly state: MotorState;
}
