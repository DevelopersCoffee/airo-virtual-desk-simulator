import { describe, expect, it } from 'vitest';
import { builtInProfiles, defaultProfile, validateProfile, withParam } from '.';

describe('desk profiles', () => {
  it('ships valid built-in profiles', () => {
    for (const p of builtInProfiles) expect(validateProfile(p)).toEqual({ ok: true, errors: [] });
  });

  it('marks every reference parameter as not yet measured', () => {
    const params = [...Object.values(defaultProfile.mechanical), ...Object.values(defaultProfile.motor)];
    for (const param of params) expect(param.status).not.toBe('measured');
  });

  it('rejects invalid limits and ratios', () => {
    expect(validateProfile(withParam(defaultProfile, 'mechanical', 'maxHeightMm', 500)).ok).toBe(false);
    expect(validateProfile(withParam(defaultProfile, 'mechanical', 'travelPerShaftRevMm', -1)).ok).toBe(false);
    expect(validateProfile(withParam(defaultProfile, 'motor', 'noLoadRpm', NaN)).ok).toBe(false);
  });
});
