import { DirectController, type Controller, type ControllerStatus, type DeskCommand } from '../controller';
import { assertValidProfile, type DeskProfile } from '../desk-profiles';
import {
  initialMechanicalState,
  stepMechanics,
  syncErrorMm,
  type MechanicalParams,
  type MechanicalState,
} from '../mechanical-engine';
import { IdealMotor, type MotorModel } from '../motor-engine';
import { FixedStepClock } from './fixed-step-clock';

export interface SimulationOptions {
  /** Fixed physics timestep in seconds. Default 1 ms. */
  dtS?: number;
  maxStepsPerAdvance?: number;
  /** Swap in another motor implementation (future DC model or hardware adapter). */
  createMotor?: (profile: DeskProfile) => MotorModel;
  /** Swap in another controller (future adaptive controller). */
  createController?: (profile: DeskProfile) => Controller;
}

export interface SimulationSnapshot {
  timeS: number;
  steps: number;
  dtS: number;
  paused: boolean;
  timeScale: number;
  profile: DeskProfile;
  mechanical: MechanicalState;
  syncErrorMm: number;
  motorCommandRpm: number;
  dutyCycle: number;
  controllerStatus: ControllerStatus;
  motorKind: string;
  controllerKind: string;
}

export function mechanicalParamsFromProfile(profile: DeskProfile): MechanicalParams {
  const m = profile.mechanical;
  return {
    gearRatio: m.gearRatio.value,
    travelPerShaftRevMm: m.travelPerShaftRevMm.value,
    minHeightMm: m.minHeightMm.value,
    maxHeightMm: m.maxHeightMm.value,
  };
}

const defaultMotor = (p: DeskProfile): MotorModel =>
  new IdealMotor({
    noLoadRpm: p.motor.noLoadRpm.value,
    spinUpTimeConstantS: p.motor.spinUpTimeConstantS.value,
  });

const defaultController = (): Controller => new DirectController();

/**
 * Wires controller -> motor -> mechanical engine together on a fixed clock.
 * Contains no rendering code; the visualization only reads snapshots.
 */
export class Simulation {
  readonly dtS: number;
  private clock: FixedStepClock;
  private profile: DeskProfile;
  private params: MechanicalParams;
  private motor: MotorModel;
  private controller: Controller;
  private mech: MechanicalState;
  private steps = 0;
  private paused = false;
  private timeScale = 1;
  private lastDuty = 0;
  private lastStatus: ControllerStatus = 'idle';
  private readonly createMotor: (p: DeskProfile) => MotorModel;
  private readonly createController: (p: DeskProfile) => Controller;

  constructor(profile: DeskProfile, opts: SimulationOptions = {}) {
    assertValidProfile(profile);
    this.dtS = opts.dtS ?? 0.001;
    this.clock = new FixedStepClock(this.dtS, opts.maxStepsPerAdvance ?? 2000);
    this.createMotor = opts.createMotor ?? defaultMotor;
    this.createController = opts.createController ?? defaultController;
    this.profile = profile;
    this.params = mechanicalParamsFromProfile(profile);
    this.motor = this.createMotor(profile);
    this.controller = this.createController(profile);
    this.mech = initialMechanicalState(this.params);
    this.lastStatus = this.controller.update({ heightMm: this.mech.heightMm, limit: this.mech.limit }, this.dtS).status;
  }

  command(cmd: DeskCommand): void {
    this.controller.command(cmd);
    if (cmd === 'stop') {
      // STOP takes effect now, not at the next fixed step.
      this.motor.step({ dutyCycle: 0, dtS: this.dtS });
      this.lastDuty = 0;
      this.mech = { ...this.mech, motorRpm: 0, shaftRpm: 0, liftSpeedMmPerS: 0 };
    }
  }

  /** Feed elapsed wall-clock seconds. Returns the number of fixed steps run. */
  advance(realElapsedS: number): number {
    if (this.paused) return 0;
    const n = this.clock.advance(realElapsedS * this.timeScale);
    for (let i = 0; i < n; i++) this.step();
    return n;
  }

  /** Run exactly one fixed timestep, regardless of pause state. */
  step(): void {
    const dt = this.dtS;
    const out = this.controller.update({ heightMm: this.mech.heightMm, limit: this.mech.limit }, dt);
    const motor = this.motor.step({ dutyCycle: out.dutyCycle, dtS: dt });
    const next = stepMechanics(this.mech, motor.rpm, dt, this.params);
    if (next.blocked) this.motor.notifyBlocked();
    this.mech = next;
    this.lastDuty = out.dutyCycle;
    this.lastStatus = out.status;
    this.steps++;
  }

  /** Apply a changed profile. Height is kept (clamped to the new limits). */
  setProfile(profile: DeskProfile): void {
    assertValidProfile(profile);
    this.profile = profile;
    this.params = mechanicalParamsFromProfile(profile);
    this.motor = this.createMotor(profile);
    this.controller.command('stop');
    const h = Math.min(this.params.maxHeightMm, Math.max(this.params.minHeightMm, this.mech.heightMm));
    this.mech = { ...initialMechanicalState(this.params, h), motorRevs: this.mech.motorRevs, shaftRevs: this.mech.shaftRevs };
    this.lastDuty = 0;
  }

  reset(): void {
    this.clock.reset();
    this.motor.reset();
    this.controller.reset();
    this.mech = initialMechanicalState(this.params);
    this.steps = 0;
    this.lastDuty = 0;
    this.lastStatus = this.controller.update({ heightMm: this.mech.heightMm, limit: this.mech.limit }, this.dtS).status;
  }

  setPaused(paused: boolean): void {
    this.paused = paused;
    if (paused) this.clock.reset();
  }

  setTimeScale(scale: number): void {
    if (!(scale > 0) || !Number.isFinite(scale)) throw new RangeError('timeScale must be positive');
    this.timeScale = scale;
  }

  snapshot(): SimulationSnapshot {
    return {
      timeS: this.steps * this.dtS,
      steps: this.steps,
      dtS: this.dtS,
      paused: this.paused,
      timeScale: this.timeScale,
      profile: this.profile,
      mechanical: this.mech,
      syncErrorMm: syncErrorMm(this.mech),
      motorCommandRpm: this.motor.state.rpm,
      dutyCycle: this.lastDuty,
      controllerStatus: this.lastStatus,
      motorKind: this.motor.kind,
      controllerKind: this.controller.kind,
    };
  }
}
