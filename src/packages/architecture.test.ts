import { readdirSync, readFileSync, statSync } from 'node:fs';
import { join } from 'node:path';
import { describe, expect, it } from 'vitest';

/**
 * The engine packages must stay free of rendering and UI code so they can be
 * reused by firmware simulation, tests and a future hardware adapter.
 */
const ENGINE_PACKAGES = ['desk-profiles', 'mechanical-engine', 'motor-engine', 'controller', 'simulation-engine'];
const FORBIDDEN = /from\s+['"](react|react-dom|three|@react-three\/[^'"]+)['"]|from\s+['"]\.\.\/(visualization)/;

function files(dir: string): string[] {
  return readdirSync(dir).flatMap((f: string) => {
    const p = join(dir, f);
    return statSync(p).isDirectory() ? files(p) : p.endsWith('.ts') ? [p] : [];
  });
}

describe('package boundaries', () => {
  for (const pkg of ENGINE_PACKAGES) {
    it(`${pkg} does not import React, three.js or the visualization`, () => {
      for (const f of files(join(__dirname, pkg))) {
        expect(readFileSync(f, 'utf8'), f).not.toMatch(FORBIDDEN);
      }
    });
  }

  it('the mechanical engine does not depend on the motor or controller', () => {
    for (const f of files(join(__dirname, 'mechanical-engine'))) {
      expect(readFileSync(f, 'utf8'), f).not.toMatch(/\.\.\/(motor-engine|controller|simulation-engine)/);
    }
  });
});
