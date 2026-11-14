import type { MotorInput, MotorModel, MotorState } from './types';

export interface IdealMotorParams {
  noLoadRpm: number;
  /** First-order spin-up time constant; 0 means instant. */
  spinUpTimeConstantS: number;
}

/**
 * Phase 1 stub motor.
 *
 * Speed follows dutyCycle * noLoadRpm with a first-order lag while driving.
 * A zero command brakes instantly, which is what makes STOP immediate.
 * There is no load, current, torque or temperature yet: that is Phase 2.
 */
export class IdealMotor implements MotorModel {
  readonly kind = 'ideal-stub';
  private _state: MotorState = { rpm: 0, dutyCycle: 0 };

  constructor(private params: IdealMotorParams) {
    if (!(params.noLoadRpm > 0)) throw new RangeError('noLoadRpm must be positive');
    if (!(params.spinUpTimeConstantS >= 0)) throw new RangeError('spinUpTimeConstantS must be >= 0');
  }

  get state(): MotorState {
    return this._state;
  }

  setParams(params: IdealMotorParams): void {
    this.params = params;
  }

  step({ dutyCycle, dtS }: MotorInput): MotorState {
    const duty = Math.max(-1, Math.min(1, dutyCycle));
    if (duty === 0) {
      this._state = { rpm: 0, dutyCycle: 0 };
      return this._state;
    }
    const target = duty * this.params.noLoadRpm;
    let rpm = this._state.rpm;
    // Reversing without passing through a stop: drop to zero first.
    if (rpm !== 0 && Math.sign(rpm) !== Math.sign(target)) rpm = 0;
    const tau = this.params.spinUpTimeConstantS;
    rpm = tau === 0 ? target : rpm + (target - rpm) * (1 - Math.exp(-dtS / tau));
    this._state = { rpm, dutyCycle: duty };
    return this._state;
  }

  notifyBlocked(): void {
    this._state = { ...this._state, rpm: 0 };
  }

  reset(): void {
    this._state = { rpm: 0, dutyCycle: 0 };
  }
}
