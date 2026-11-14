/**
 * Fixed-timestep accumulator.
 *
 * Wall-clock (render) time is fed in with `advance`, and the physics is
 * stepped in whole multiples of `dtS`. The result depends only on the total
 * simulated time and the inputs, not on the render frame rate.
 */
export class FixedStepClock {
  private accumulatorS = 0;

  constructor(
    readonly dtS: number,
    /** Upper bound on steps per advance call, so a long stall cannot freeze the tab. */
    readonly maxStepsPerAdvance: number,
  ) {
    if (!(dtS > 0)) throw new RangeError('dtS must be positive');
    if (!(maxStepsPerAdvance >= 1)) throw new RangeError('maxStepsPerAdvance must be >= 1');
  }

  /** Returns how many fixed steps to run for this much elapsed simulated time. */
  advance(elapsedS: number): number {
    if (!(elapsedS > 0)) return 0;
    this.accumulatorS += elapsedS;
    // Small epsilon so that e.g. 10 x 0.1 ms reliably yields one 1 ms step.
    let steps = Math.floor(this.accumulatorS / this.dtS + 1e-9);
    if (steps > this.maxStepsPerAdvance) {
      steps = this.maxStepsPerAdvance;
      this.accumulatorS = 0; // drop the backlog instead of spiralling
    } else {
      this.accumulatorS = Math.max(0, this.accumulatorS - steps * this.dtS);
    }
    return steps;
  }

  reset(): void {
    this.accumulatorS = 0;
  }
}
