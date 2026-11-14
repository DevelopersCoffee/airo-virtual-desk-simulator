import type { ConfigParam, DeskProfile } from './types';

export interface ValidationResult {
  ok: boolean;
  errors: string[];
}

function positiveFinite(p: ConfigParam, label: string, errors: string[]): void {
  if (!Number.isFinite(p.value) || p.value <= 0) {
    errors.push(`${label} must be a positive number (got ${p.value}).`);
  }
}

/** Rejects profiles that would make the simulation meaningless or unstable. */
export function validateProfile(profile: DeskProfile): ValidationResult {
  const errors: string[] = [];
  const m = profile.mechanical;
  positiveFinite(m.gearRatio, 'Gear ratio', errors);
  positiveFinite(m.travelPerShaftRevMm, 'Travel per shaft revolution', errors);
  positiveFinite(m.minHeightMm, 'Minimum height', errors);
  positiveFinite(m.maxHeightMm, 'Maximum height', errors);
  if (m.maxHeightMm.value <= m.minHeightMm.value) {
    errors.push('Maximum height must be greater than minimum height.');
  }
  positiveFinite(profile.motor.noLoadRpm, 'Motor no-load speed', errors);
  const tau = profile.motor.spinUpTimeConstantS.value;
  if (!Number.isFinite(tau) || tau < 0) {
    errors.push(`Motor spin-up time constant must be zero or positive (got ${tau}).`);
  }
  return { ok: errors.length === 0, errors };
}

export function assertValidProfile(profile: DeskProfile): void {
  const r = validateProfile(profile);
  if (!r.ok) throw new Error(`Invalid desk profile "${profile.id}": ${r.errors.join(' ')}`);
}

/** Returns a copy of the profile with one numeric parameter replaced. */
export function withParam(
  profile: DeskProfile,
  group: 'mechanical' | 'motor',
  key: string,
  value: number,
): DeskProfile {
  const groupObj = profile[group] as unknown as Record<string, ConfigParam>;
  const current = groupObj[key];
  if (!current) throw new Error(`Unknown parameter ${group}.${key}`);
  return {
    ...profile,
    [group]: { ...groupObj, [key]: { ...current, value } },
  };
}
