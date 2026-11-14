import { useEffect, useState } from 'react';
import { defaultProfile, validateProfile, withParam, type ConfigParam } from '../packages/desk-profiles';
import type { Simulation, SimulationSnapshot } from '../packages/simulation-engine';

interface Props {
  sim: Simulation;
  snapshot: SimulationSnapshot;
  refresh: () => void;
}

type Key = { group: 'mechanical' | 'motor'; key: string; label: string };

const FIELDS: Key[] = [
  { group: 'mechanical', key: 'gearRatio', label: 'Gear ratio' },
  { group: 'mechanical', key: 'travelPerShaftRevMm', label: 'Travel per shaft rev' },
  { group: 'mechanical', key: 'minHeightMm', label: 'Minimum height' },
  { group: 'mechanical', key: 'maxHeightMm', label: 'Maximum height' },
  { group: 'motor', key: 'noLoadRpm', label: 'Motor no-load speed' },
  { group: 'motor', key: 'spinUpTimeConstantS', label: 'Motor spin-up time' },
];

const param = (s: SimulationSnapshot, k: Key) =>
  (s.profile[k.group] as unknown as Record<string, ConfigParam>)[k.key]!;

export function ConfigPanel({ sim, snapshot, refresh }: Props) {
  const [draft, setDraft] = useState<Record<string, string>>({});
  const [errors, setErrors] = useState<string[]>([]);
  const profile = snapshot.profile;

  useEffect(() => setDraft({}), [profile]);

  const apply = () => {
    let next = profile;
    for (const k of FIELDS) {
      const raw = draft[k.key];
      if (raw !== undefined) next = withParam(next, k.group, k.key, Number(raw));
    }
    const r = validateProfile(next);
    setErrors(r.errors);
    if (r.ok) {
      sim.setProfile(next);
      refresh();
    }
  };

  const restore = () => {
    sim.setProfile(defaultProfile);
    setErrors([]);
    refresh();
  };

  const dirty = Object.keys(draft).length > 0;

  return (
    <section className="card">
      <header className="card-head">
        <h2>Desk profile</h2>
      </header>
      <p className="small">{profile.name}</p>
      <div className="config">
        {FIELDS.map((k) => {
          const p = param(snapshot, k);
          return (
            <label key={k.key} className="cfg-row" title={p.note}>
              <span>
                {k.label}
                <span className={`badge ${p.status}`}>{p.status}</span>
              </span>
              <span className="cfg-input">
                <input
                  type="number"
                  step="any"
                  value={draft[k.key] ?? String(p.value)}
                  onChange={(e) => setDraft((d) => ({ ...d, [k.key]: e.target.value }))}
                  onKeyDown={(e) => e.key === 'Enter' && apply()}
                />
                <span className="muted small unit-label">{p.unit}</span>
              </span>
            </label>
          );
        })}
      </div>
      {errors.length > 0 && (
        <ul className="errors">
          {errors.map((e) => (
            <li key={e}>{e}</li>
          ))}
        </ul>
      )}
      <div className="btn-grid">
        <button className="btn primary" disabled={!dirty} onClick={apply}>
          Apply
        </button>
        <button className="btn ghost" onClick={restore}>
          Restore defaults
        </button>
      </div>
      <p className="muted small">
        All values are placeholders, not measurements of the real desk. Applying stops the desk and keeps its current height.
      </p>
    </section>
  );
}
