import type { AiroInfo } from './airo-client';
import type { BenchState } from './pin-bridge';

export interface BenchGuardOptions {
  /** Allow the run while bench drive (breadboard LEDs) is on. Default false. */
  allowDrive?: boolean;
}

/**
 * The hardware-in-the-loop suite refuses to run unless the board says it is
 * in bench mode: outputs not driven (unless explicitly allowed), inputs from
 * the bench link, desk mode not even compiled in. Returns the reasons to
 * refuse; empty = safe to run.
 */
export function benchRefusals(info: (AiroInfo & { bench?: Partial<BenchState> }) | null, opts: BenchGuardOptions = {}): string[] {
  if (!info) return ['no answer from GET /api/v1/info'];
  const b = info.bench;
  if (!b) return [`the target reports no bench state (adapter "${info.adapter}"); is it running the bench firmware?`];
  const why: string[] = [];
  if (b.bench_mode !== true || b.mode !== 'bench') why.push(`not in bench mode (mode=${String(b.mode)})`);
  if (b.desk_mode_compiled !== false) why.push('firmware was built with desk mode (AIRO_ALLOW_DESK_MODE); use the bench build');
  if (b.drive_enabled !== false && !opts.allowDrive)
    why.push('bench drive is ON (pins are driven); turn it off at the serial console or set AIRO_HIL_ALLOW_DRIVE=1');
  if (b.inputs !== 'link') why.push(`inputs are "${String(b.inputs)}", the suite needs "link"`);
  return why;
}
