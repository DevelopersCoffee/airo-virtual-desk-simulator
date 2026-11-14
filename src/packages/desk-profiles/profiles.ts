import type { DeskProfile } from './types';

const PLACEHOLDER = 'Placeholder for simulation only. Not measured on real hardware.';

/**
 * Green Soul Elevate reference configuration.
 *
 * None of these values come from the manufacturer or from measurement. They
 * are plausible placeholders so the mechanism can be visualised. Replace them
 * as real measurements become available and change `status` accordingly.
 */
export const greenSoulElevateReference: DeskProfile = {
  id: 'green-soul-elevate-reference',
  name: 'Green Soul Elevate (reference, unverified)',
  version: 1,
  description:
    'Single central motor, gearbox driving a split hexagonal shaft that synchronises two lifting columns.',
  mechanical: {
    gearRatio: { value: 20, unit: 'motor rev / shaft rev', status: 'unverified', note: PLACEHOLDER },
    travelPerShaftRevMm: { value: 12, unit: 'mm / shaft rev', status: 'unverified', note: PLACEHOLDER },
    minHeightMm: { value: 720, unit: 'mm', status: 'unverified', note: PLACEHOLDER },
    maxHeightMm: { value: 1200, unit: 'mm', status: 'unverified', note: PLACEHOLDER },
  },
  motor: {
    noLoadRpm: { value: 3200, unit: 'rpm', status: 'unverified', note: PLACEHOLDER },
    spinUpTimeConstantS: { value: 0.08, unit: 's', status: 'unverified', note: PLACEHOLDER },
  },
};

export const builtInProfiles: readonly DeskProfile[] = [greenSoulElevateReference];

export const defaultProfile = greenSoulElevateReference;
