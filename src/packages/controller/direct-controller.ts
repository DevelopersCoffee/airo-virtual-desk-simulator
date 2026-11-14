import type { Controller, ControllerInputs, ControllerOutput, DeskCommand } from './types';

/**
 * Phase 1 stub: maps UP/DOWN/STOP straight to a fixed duty cycle and stops
 * itself at the travel limits. No closed-loop control yet.
 */
export class DirectController implements Controller {
  readonly kind = 'direct-stub';
  private active: DeskCommand = 'stop';

  constructor(private dutyCycle = 1) {}

  command(cmd: DeskCommand): void {
    this.active = cmd;
  }

  update(inputs: ControllerInputs): ControllerOutput {
    if (this.active === 'up' && inputs.limit === 'upper') this.active = 'stop';
    if (this.active === 'down' && inputs.limit === 'lower') this.active = 'stop';

    switch (this.active) {
      case 'up':
        return { dutyCycle: this.dutyCycle, status: 'moving-up' };
      case 'down':
        return { dutyCycle: -this.dutyCycle, status: 'moving-down' };
      case 'stop':
        return {
          dutyCycle: 0,
          status:
            inputs.limit === 'upper' ? 'at-upper-limit' : inputs.limit === 'lower' ? 'at-lower-limit' : 'idle',
        };
    }
  }

  reset(): void {
    this.active = 'stop';
  }
}
