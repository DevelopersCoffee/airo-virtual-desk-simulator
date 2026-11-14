import type { TravelLimit } from '../mechanical-engine';

export type DeskCommand = 'up' | 'down' | 'stop';

export type ControllerStatus = 'idle' | 'moving-up' | 'moving-down' | 'at-upper-limit' | 'at-lower-limit';

/** What the controller is allowed to observe. Mirrors sensors a real board would have. */
export interface ControllerInputs {
  heightMm: number;
  limit: TravelLimit;
}

export interface ControllerOutput {
  dutyCycle: number;
  status: ControllerStatus;
}

/**
 * Controller abstraction. The Phase 3 adaptive controller and later the
 * ESP32 firmware port implement this same interface.
 */
export interface Controller {
  readonly kind: string;
  command(cmd: DeskCommand): void;
  update(inputs: ControllerInputs, dtS: number): ControllerOutput;
  reset(): void;
}
