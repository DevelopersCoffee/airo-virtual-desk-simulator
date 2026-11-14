import { afterEach, describe, it } from 'vitest';
import { selected, type ScenarioCtx } from './scenarios';
import type { Bench, Target } from './harness';

/** Registers the selected scenario table as vitest tests against one target. */
export function runScenarios(target: () => Target): void {
  const { scenarios, cases } = selected();
  const t = target();
  let opened: Bench[] = [];

  afterEach(async () => {
    for (const b of opened.reverse()) await b.close().catch(() => undefined);
    opened = [];
  });

  const ctx: ScenarioCtx = {
    target: t,
    slack: (ms) => ms + t.slackMs,
    open: async (c, opts) => {
      const b = await t.open(c, opts);
      opened.push(b);
      const close = b.close;
      let closed = false;
      b.close = async () => {
        if (closed) return;
        closed = true;
        await close();
      };
      return b;
    },
  };

  describe(`target ${t.name}`, () => {
    for (const s of scenarios) {
      const runOn = s.cases.filter((c) => cases.includes(c));
      for (const c of runOn) {
        it(`[${s.id}] ${c}: ${s.title}`, () => s.run(ctx, c), s.timeoutMs);
      }
    }
  });
}
