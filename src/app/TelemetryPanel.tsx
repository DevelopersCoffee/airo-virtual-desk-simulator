import type { SimulationSnapshot } from '../packages/simulation-engine';

const f = (v: number, d = 0) => (Math.abs(v) < 0.5 * 10 ** -d ? (0).toFixed(d) : v.toFixed(d));

export function TelemetryPanel({ snapshot }: { snapshot: SimulationSnapshot }) {
  const m = snapshot.mechanical;
  const rows: [string, string, string?][] = [
    ['Motor speed', `${f(m.motorRpm)} rpm`],
    ['Shaft speed', `${f(m.shaftRpm, 1)} rpm`],
    ['Lift speed', `${f(m.liftSpeedMmPerS, 1)} mm/s`],
    ['Left column', `${f(m.leftColumnHeightMm, 1)} mm`],
    ['Right column', `${f(m.rightColumnHeightMm, 1)} mm`],
    ['Column sync error', `${f(snapshot.syncErrorMm, 2)} mm`, snapshot.syncErrorMm > 0.5 ? 'warn' : 'ok'],
    ['Motor revolutions', f(m.motorRevs, 1)],
    ['Shaft revolutions', f(m.shaftRevs, 2)],
    ['Travel limit', m.limit === 'none' ? 'clear' : m.limit],
    ['Sim time', `${snapshot.timeS.toFixed(2)} s`],
    ['Fixed timestep', `${(snapshot.dtS * 1000).toFixed(1)} ms · ${snapshot.steps} steps`],
  ];
  return (
    <section className="card">
      <header className="card-head">
        <h2>Live telemetry</h2>
      </header>
      <dl className="telemetry">
        {rows.map(([k, v, tone]) => (
          <div key={k} className={tone ?? ''}>
            <dt>{k}</dt>
            <dd className="mono">{v}</dd>
          </div>
        ))}
      </dl>
      <p className="muted small">Torque, current, load and temperature arrive with the Phase 2 motor model.</p>
    </section>
  );
}
