import type { SimulationSnapshot, Simulation } from '../packages/simulation-engine';

interface Props {
  sim: Simulation;
  snapshot: SimulationSnapshot;
  refresh: () => void;
}

const STATUS_LABEL: Record<SimulationSnapshot['controllerStatus'], string> = {
  idle: 'Idle',
  'moving-up': 'Moving up',
  'moving-down': 'Moving down',
  'at-upper-limit': 'At upper limit',
  'at-lower-limit': 'At lower limit',
};

const SPEEDS = [0.02, 0.1, 0.5, 1, 2];

export function ControlPanel({ sim, snapshot, refresh }: Props) {
  const m = snapshot.mechanical;
  const act = (fn: () => void) => () => {
    fn();
    refresh();
  };
  const moving = snapshot.controllerStatus.startsWith('moving');

  return (
    <aside className="left">
      <div className="brand">
        <div className="logo">A</div>
        <div>
          <h1>Airo Virtual Desk</h1>
          <p className="muted small">Phase 1 · mechanical simulator</p>
        </div>
      </div>

      <div className="status-row">
        <span className={`dot ${moving ? 'live' : snapshot.paused ? 'paused' : ''}`} />
        <span className="muted">Status</span>
        <span className="right-align">{snapshot.paused ? 'Paused' : STATUS_LABEL[snapshot.controllerStatus]}</span>
      </div>

      <div className="height">
        <div className="big">{(m.heightMm / 10).toFixed(1)}<span className="unit">cm</span></div>
        <div className="muted caps">Tabletop height</div>
        <div className="muted small">{(m.heightMm / 25.4).toFixed(1)} in</div>
      </div>

      <div className="btn-grid">
        <button className={`btn ${snapshot.controllerStatus === 'moving-up' ? 'active' : ''}`} onClick={act(() => sim.command('up'))}>
          ▲ Up
        </button>
        <button className={`btn ${snapshot.controllerStatus === 'moving-down' ? 'active' : ''}`} onClick={act(() => sim.command('down'))}>
          ▼ Down
        </button>
        <button className="btn stop wide" onClick={act(() => sim.command('stop'))}>
          ■ Stop
        </button>
      </div>
      <p className="muted small center-text">Keys: ↑ up, ↓ down, Space stop</p>

      <h3 className="section">Simulation</h3>
      <div className="btn-grid">
        <button className="btn ghost" onClick={act(() => sim.setPaused(!snapshot.paused))}>
          {snapshot.paused ? 'Resume' : 'Pause'}
        </button>
        <button className="btn ghost" disabled={!snapshot.paused} onClick={act(() => sim.step())} title="Advance one fixed timestep while paused">
          Step {Math.round(snapshot.dtS * 1000)} ms
        </button>
        <button className="btn ghost wide" onClick={act(() => sim.reset())}>
          Reset simulation
        </button>
      </div>
      <label className="field">
        <span className="muted small">Speed</span>
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
    </aside>
  );
}
