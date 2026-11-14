import type { Simulation, SimulationSnapshot } from '../packages/simulation-engine';

const SPEEDS = [0.02, 0.1, 0.5, 1, 2];

export function SimControls({ sim, snapshot, refresh }: { sim: Simulation; snapshot: SimulationSnapshot; refresh: () => void }) {
  const act = (fn: () => void) => () => {
    fn();
    refresh();
  };
  return (
    <>
      <h3 className="section">Simulation</h3>
      <div className="btn-grid">
        <button className="btn ghost" onClick={act(() => sim.setPaused(!snapshot.paused))}>
          {snapshot.paused ? 'Resume' : 'Pause'}
        </button>
        <button className="btn ghost" disabled={!snapshot.paused} onClick={act(() => sim.step())} title="Advance one fixed timestep while paused">
          Step {Math.round(snapshot.dtS * 1000)} ms
        </button>
      </div>
      <label className="field">
        <span className="muted small">Speed (Airo leases run in real time; use 1× with the app)</span>
        <div className="seg">
          {SPEEDS.map((s) => (
            <button key={s} className={snapshot.timeScale === s ? 'on' : ''} onClick={act(() => sim.setTimeScale(s))}>
              {s}×
            </button>
          ))}
        </div>
      </label>
      <div className="footer mono small muted">
        motor: {snapshot.motorKind} · controller: {snapshot.controllerKind}
      </div>
    </>
  );
}
