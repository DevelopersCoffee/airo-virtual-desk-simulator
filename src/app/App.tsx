import { useEffect, useState } from 'react';
import { DeskScene } from '../packages/visualization';
import { ConfigPanel } from './ConfigPanel';
import { ControlPanel } from './ControlPanel';
import { TelemetryPanel } from './TelemetryPanel';
import { useSimulation } from './useSimulation';

export function App() {
  const { sim, snapshot, getSnapshot, refresh } = useSimulation();
  const [xray, setXray] = useState(false);
  const [view, setView] = useState<'overview' | 'drive'>('overview');
  // Remount the scene when transmission geometry changes (gear teeth, screw lead).
  const sceneKey = `${snapshot.profile.mechanical.gearRatio.value}-${snapshot.profile.mechanical.travelPerShaftRevMm.value}`;

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.target instanceof HTMLInputElement) return;
      if (e.key === 'ArrowUp') sim.command('up');
      else if (e.key === 'ArrowDown') sim.command('down');
      else if (e.key === ' ' || e.key === 'Escape') sim.command('stop');
      else return;
      e.preventDefault();
      refresh();
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  });

  return (
    <div className="layout">
      <ControlPanel sim={sim} snapshot={snapshot} refresh={refresh} />
      <main className="center">
        <section className="card scene-card">
          <header className="card-head">
            <h2>Mechanism</h2>
            <div className="seg">
              <button className={view === 'overview' ? 'on' : ''} onClick={() => setView('overview')}>
                Overview
              </button>
              <button className={view === 'drive' ? 'on' : ''} onClick={() => setView('drive')}>
                Drive close-up
              </button>
              <button className={xray ? 'on' : ''} onClick={() => setXray((x) => !x)} title="See the lead screws inside the columns">
                X-ray columns
              </button>
            </div>
          </header>
          <div className="scene">
            <DeskScene key={sceneKey} getSnapshot={getSnapshot} xray={xray} view={view} />
            <p className="scene-hint">Drag to rotate, scroll to zoom, right-drag to pan. Slow the simulation down to follow the worm and gears.</p>
          </div>
        </section>
      </main>
      <aside className="right">
        <TelemetryPanel snapshot={snapshot} />
        <ConfigPanel sim={sim} snapshot={snapshot} refresh={refresh} />
      </aside>
    </div>
  );
}
