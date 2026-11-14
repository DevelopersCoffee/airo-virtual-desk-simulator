import { useEffect, useState } from 'react';
import { DeskScene } from '../packages/visualization';
import { AppPanel } from './AppPanel';
import { ConfigPanel } from './ConfigPanel';
import { HardwarePanel } from './HardwarePanel';
import { SimControls } from './SimControls';
import { TelemetryPanel } from './TelemetryPanel';
import { useAiroApp } from './useAiroApp';
import { useDeskRig } from './useDeskRig';

export function App() {
  const { rig, snapshot, getSnapshot, refresh } = useDeskRig();
  const app = useAiroApp();
  const [xray, setXray] = useState(false);
  const [view, setView] = useState<'overview' | 'drive'>('overview');
  const sim = snapshot.sim;
  // Remount the scene when transmission geometry changes (gear teeth, screw lead).
  const sceneKey = `${sim.profile.mechanical.gearRatio.value}-${sim.profile.mechanical.travelPerShaftRevMm.value}`;

  useEffect(() => {
    const key = (e: KeyboardEvent): 'up' | 'down' | null => (e.key === 'ArrowUp' ? 'up' : e.key === 'ArrowDown' ? 'down' : null);
    const onDown = (e: KeyboardEvent) => {
      if (e.target instanceof HTMLInputElement) return;
      const k = key(e);
      if (k) rig.keypad.press(k);
      else if (e.key === ' ' || e.key === 'Escape') void app.client.stop();
      else return;
      e.preventDefault();
    };
    const onUp = (e: KeyboardEvent) => {
      const k = key(e);
      if (k) rig.keypad.release(k);
    };
    const onBlur = () => rig.keypad.releaseAll();
    window.addEventListener('keydown', onDown);
    window.addEventListener('keyup', onUp);
    window.addEventListener('blur', onBlur);
    return () => {
      window.removeEventListener('keydown', onDown);
      window.removeEventListener('keyup', onUp);
      window.removeEventListener('blur', onBlur);
    };
  }, [rig, app.client]);

  return (
    <div className="layout">
      <aside className="left">
        <AppPanel {...app} />
        <SimControls sim={rig.sim} snapshot={sim} refresh={refresh} />
      </aside>
      <main className="center">
        <section className="card scene-card">
          <header className="card-head">
            <h2>Mechanism · true height {(sim.mechanical.heightMm / 10).toFixed(1)} cm</h2>
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
            <p className="scene-hint">Drag to rotate, scroll to zoom, right-drag to pan.</p>
          </div>
        </section>
      </main>
      <aside className="right">
        <HardwarePanel rig={rig} view={snapshot} refresh={refresh} />
        <TelemetryPanel snapshot={sim} />
        <ConfigPanel sim={rig.sim} snapshot={sim} refresh={refresh} />
      </aside>
    </div>
  );
}
