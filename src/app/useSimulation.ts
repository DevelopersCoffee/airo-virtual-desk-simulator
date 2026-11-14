import { useEffect, useMemo, useRef, useState } from 'react';
import { defaultProfile } from '../packages/desk-profiles';
import { Simulation, type SimulationSnapshot } from '../packages/simulation-engine';

/**
 * Owns the Simulation instance and runs it on its own requestAnimationFrame
 * loop. The loop only reports elapsed wall time; the simulation turns that
 * into whole fixed timesteps, so physics does not depend on render FPS.
 * React state is refreshed at a modest rate for the panels; the 3D scene
 * reads snapshots directly every frame.
 */
export function useSimulation(uiHz = 15) {
  const sim = useMemo(() => new Simulation(defaultProfile), []);
  const [snapshot, setSnapshot] = useState<SimulationSnapshot>(() => sim.snapshot());
  const latest = useRef(snapshot);

  useEffect(() => {
    let raf = 0;
    let last = performance.now();
    const loop = (now: number) => {
      const elapsed = Math.min(0.25, (now - last) / 1000);
      last = now;
      sim.advance(elapsed);
      latest.current = sim.snapshot();
      raf = requestAnimationFrame(loop);
    };
    raf = requestAnimationFrame(loop);
    const ui = setInterval(() => setSnapshot(sim.snapshot()), 1000 / uiHz);
    return () => {
      cancelAnimationFrame(raf);
      clearInterval(ui);
    };
  }, [sim, uiHz]);

  const getSnapshot = useMemo(() => () => latest.current, []);
  const refresh = () => {
    latest.current = sim.snapshot();
    setSnapshot(latest.current);
  };

  return { sim, snapshot, getSnapshot, refresh };
}
