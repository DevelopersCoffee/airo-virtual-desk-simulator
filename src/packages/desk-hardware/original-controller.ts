import type { Controller, ControllerInputs, ControllerOutput, DeskCommand } from '../controller';
import { CMD, FrameDecoder, HANDSET, heightReport } from './frames';
import { decodeEncoded, KEY_BUTTONS, type KeyButton } from './types';

export interface OriginalControllerOptions {
  /** Heights stored in the controller's memory slots. Placeholders, not measured. */
  sitHeightMm: number;
  standHeightMm: number;
  dutyCycle?: number;
  /** A held UP/DOWN frame keeps the motor running this long (digital case) [ASSUMPTION]. */
  frameHoldMs?: number;
  /** How often the controller reports height on DTX (digital case) [ASSUMPTION]. */
  heightReportMs?: number;
  /** Stop a preset within this distance of its target. */
  presetToleranceMm?: number;
}

export type ControllerLogKind =
  | 'move-up'
  | 'move-down'
  | 'preset-sit'
  | 'preset-stand'
  | 'preset-done'
  | 'preset-stopped'
  | 'stop-frame'
  | 'conflict'
  | 'limit'
  | 'reset-ignored';

export interface ControllerLogEntry {
  tMs: number;
  kind: ControllerLogKind;
}

type Motion = 'stop' | 'up' | 'down';

/**
 * Model of the desk's ORIGINAL controller (conflict C8: this package models
 * the desk, never Airo's logic, which lives only in firmware/airo-core).
 *
 * Behaviour, all [ASSUMPTION] until discovery (spec section 5):
 * - UP / DOWN are hold-to-run.
 * - Sit / stand are one-touch presets to the stored heights (Q5).
 * - Any new key press during a preset stops it; keys are then ignored until
 *   all are released (Q6).
 * - Two direction keys at once (one-line-per-button case) stop the motor (Q9).
 * - The controller stops itself at its travel limits.
 * - The circular-arrow button does nothing here (Q4).
 * - In the digital case it reports height every heightReportMs on DTX (Q7).
 *
 * It accepts button lines (A1), encoded HS lines (A2) and UART frames (B);
 * the interface board feeds whichever the chosen case uses.
 */
export class OriginalDeskController implements Controller {
  readonly kind = 'original-desk-controller';
  readonly log: ControllerLogEntry[] = [];
  private opts: Required<OriginalControllerOptions>;
  private tMs = 0;
  private lines = new Set<KeyButton>();
  private encoded: KeyButton | null = null;
  private frameHeld: { button: KeyButton; untilMs: number } | null = null;
  private decoder = new FrameDecoder();
  private prev = new Set<KeyButton>();
  private preset: { target: number; button: 'sit' | 'stand' } | null = null;
  private latched = false;
  private motion: Motion = 'stop';
  private legacy: Motion = 'stop';
  private lastReportMs = -Infinity;
  private tx: number[][] = [];
  private reportHeight = false;
  private lastHeightMm = 0;

  constructor(opts: OriginalControllerOptions) {
    this.opts = {
      dutyCycle: 1,
      frameHoldMs: 150,
      heightReportMs: 200,
      presetToleranceMm: 0.5,
      ...opts,
    };
  }

  get options(): Readonly<Required<OriginalControllerOptions>> {
    return this.opts;
  }

  setPresetHeights(sitHeightMm: number, standHeightMm: number): void {
    this.opts = { ...this.opts, sitHeightMm, standHeightMm };
  }

  /** Case A1: which button lines are pulled low. */
  setLines(lines: Iterable<KeyButton>): void {
    this.lines = new Set(lines);
  }

  /** Case A2: the code on the HS lines (0 = idle). */
  setEncoded(code: number): void {
    this.encoded = code === 0 ? null : decodeEncoded(code);
  }

  /** Case B: bytes arriving on the controller's HTX input. */
  receiveBytes(bytes: readonly number[]): void {
    for (const f of this.decoder.feed(bytes)) {
      if (f.addr !== HANDSET) continue;
      if (f.cmd === CMD.stop) {
        this.log.push({ tMs: this.tMs, kind: 'stop-frame' });
        this.frameHeld = null;
        if (this.preset) this.endPreset('preset-stopped');
        this.motion = 'stop';
        continue;
      }
      const button: KeyButton | null =
        f.cmd === CMD.up ? 'up' : f.cmd === CMD.down ? 'down' : f.cmd === CMD.memory1 ? 'sit' : f.cmd === CMD.memory2 ? 'stand' : null;
      if (button) this.frameHeld = { button, untilMs: this.tMs + this.opts.frameHoldMs };
    }
  }

  /** Turn DTX height reports on (digital case) or off. */
  setReportsHeight(on: boolean): void {
    this.reportHeight = on;
    if (!on) this.tx = [];
  }

  /** Frames the controller sent on DTX since the last call. */
  takeTx(): number[][] {
    const out = this.tx;
    this.tx = [];
    return out;
  }

  /** Buttons the controller currently sees, from every input. */
  seen(): KeyButton[] {
    const s = new Set<KeyButton>(this.lines);
    if (this.encoded) s.add(this.encoded);
    if (this.frameHeld && this.tMs < this.frameHeld.untilMs) s.add(this.frameHeld.button);
    return KEY_BUTTONS.filter((b) => s.has(b));
  }

  get activePreset(): 'sit' | 'stand' | null {
    return this.preset?.button ?? null;
  }

  get currentMotion(): Motion {
    return this.motion;
  }

  /** Legacy UP/DOWN/STOP from PR #1's dashboard and keyboard. */
  command(cmd: DeskCommand): void {
    this.legacy = cmd;
    if (cmd === 'stop' && this.preset) this.endPreset('preset-stopped');
  }

  update(inputs: ControllerInputs, dtS: number): ControllerOutput {
    this.tMs += dtS * 1000;
    this.lastHeightMm = inputs.heightMm;
    if (this.reportHeight && this.tMs - this.lastReportMs >= this.opts.heightReportMs) {
      this.lastReportMs = this.tMs;
      this.tx.push(heightReport(inputs.heightMm));
    }

    const seen = this.seen();
    const now = new Set(seen);
    const fresh = seen.filter((b) => !this.prev.has(b));
    this.prev = now;

    let next: Motion = 'stop';
    if (this.latched) {
      if (now.size === 0) this.latched = false;
    } else if (this.preset) {
      if (fresh.length > 0) {
        this.endPreset('preset-stopped');
        this.latched = true;
      } else {
        next = this.presetMotion(inputs.heightMm);
      }
    } else if (now.has('up') && now.has('down')) {
      if (this.motion !== 'stop' || fresh.length) this.log.push({ tMs: this.tMs, kind: 'conflict' });
      next = 'stop';
    } else if (now.has('up')) {
      next = 'up';
    } else if (now.has('down')) {
      next = 'down';
    } else if (fresh.includes('sit') || fresh.includes('stand')) {
      const button = fresh.includes('sit') ? 'sit' : 'stand';
      this.preset = { button, target: button === 'sit' ? this.opts.sitHeightMm : this.opts.standHeightMm };
      this.log.push({ tMs: this.tMs, kind: button === 'sit' ? 'preset-sit' : 'preset-stand' });
      next = this.presetMotion(inputs.heightMm);
    } else if (fresh.includes('reset')) {
      this.log.push({ tMs: this.tMs, kind: 'reset-ignored' });
    } else if (this.legacy !== 'stop') {
      next = this.legacy;
    }

    if (next === 'up' && inputs.limit === 'upper') next = this.atLimit();
    if (next === 'down' && inputs.limit === 'lower') next = this.atLimit();
    if (next !== this.motion && (next === 'up' || next === 'down') && !this.preset)
      this.log.push({ tMs: this.tMs, kind: next === 'up' ? 'move-up' : 'move-down' });
    this.motion = next;

    const d = this.opts.dutyCycle;
    switch (next) {
      case 'up':
        return { dutyCycle: d, status: 'moving-up' };
      case 'down':
        return { dutyCycle: -d, status: 'moving-down' };
      case 'stop':
        return {
          dutyCycle: 0,
          status: inputs.limit === 'upper' ? 'at-upper-limit' : inputs.limit === 'lower' ? 'at-lower-limit' : 'idle',
        };
    }
  }

  reset(): void {
    this.lines.clear();
    this.encoded = null;
    this.frameHeld = null;
    this.prev.clear();
    this.preset = null;
    this.latched = false;
    this.motion = 'stop';
    this.legacy = 'stop';
    this.tx = [];
  }

  /** Simulated time seen by the controller, ms. */
  get timeMs(): number {
    return this.tMs;
  }

  get heightMm(): number {
    return this.lastHeightMm;
  }

  private presetMotion(h: number): Motion {
    const p = this.preset!;
    const err = p.target - h;
    if (Math.abs(err) <= this.opts.presetToleranceMm) {
      this.endPreset('preset-done');
      return 'stop';
    }
    return err > 0 ? 'up' : 'down';
  }

  private endPreset(kind: 'preset-done' | 'preset-stopped'): void {
    this.preset = null;
    this.log.push({ tMs: this.tMs, kind });
  }

  private atLimit(): Motion {
    if (this.motion !== 'stop') this.log.push({ tMs: this.tMs, kind: 'limit' });
    if (this.preset) this.endPreset('preset-done');
    return 'stop';
  }
}
