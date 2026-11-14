import type { DeskRig } from '../packages/airo-link';
import { INTERFACE_CASES, type InterfaceCase, type KeyButton } from '../packages/desk-hardware';
import type { RigView } from './useDeskRig';

const CASE_LABEL: Record<InterfaceCase, string> = {
  A1: 'Electrical · switch lines',
  A2: 'Electrical · encoded',
  B: 'Digital · UART',
};

const KEYS: { b: KeyButton; label: string; title: string }[] = [
  { b: 'up', label: '▲', title: 'Up (hold)' },
  { b: 'down', label: '▼', title: 'Down (hold)' },
  { b: 'reset', label: '⟳', title: 'Circular arrow: behaviour unknown, ignored by the model' },
  { b: 'sit', label: 'Sit', title: 'Sitting preset (one-touch)' },
  { b: 'stand', label: 'Stand', title: 'Standing preset (one-touch)' },
];

export function HardwarePanel({ rig, view, refresh }: { rig: DeskRig; view: RigView; refresh: () => void }) {
  const b = view.board;
  const act = (fn: () => void) => () => {
    fn();
    refresh();
  };
  const press = (k: KeyButton) => (e: React.PointerEvent) => {
    (e.target as HTMLElement).setPointerCapture?.(e.pointerId);
    rig.keypad.press(k);
    refresh();
  };
  const release = (k: KeyButton) => () => {
    rig.keypad.release(k);
    refresh();
  };

  return (
    <section className="card">
      <header className="card-head">
        <h2>Desk hardware (virtual)</h2>
        <span className={`pill ${view.bridge === 'open' ? 'ok' : 'off'}`}>{view.bridge === 'open' ? 'Port C linked' : 'Airo not linked'}</span>
      </header>

      <div>
        <h3 className="section">Original keypad</h3>
        <div className="keypad">
          {KEYS.map(({ b: k, label, title }) => (
            <button
              key={k}
              title={title}
              className={`key ${b.keypad.includes(k) ? 'down' : ''}`}
              onPointerDown={press(k)}
              onPointerUp={release(k)}
              onPointerCancel={release(k)}
              onLostPointerCapture={release(k)}
            >
              {label}
            </button>
          ))}
        </div>
        <p className="muted small">Keys: ↑ / ↓ hold the physical buttons.</p>
      </div>

      <div className="field">
        <span className="muted small">Selector · interface case</span>
        <div className="seg">
          {INTERFACE_CASES.map((c) => (
            <button key={c} className={b.interfaceCase === c ? 'on' : ''} onClick={act(() => rig.board.setInterfaceCase(c))} title={CASE_LABEL[c]}>
              {CASE_LABEL[c]}
            </button>
          ))}
        </div>
      </div>

      <div className="btn-grid">
        <button className={`btn ${b.manualOnly ? 'warn' : 'ghost'}`} onClick={act(() => rig.board.setManualOnly(!b.manualOnly))}>
          {b.manualOnly ? 'Bypass: MANUAL ONLY' : 'Bypass off'}
        </button>
        <button className={`btn ${b.corePowered ? 'ghost' : 'warn'}`} onClick={act(() => rig.board.setCorePowered(!b.corePowered))}>
          {b.corePowered ? 'Cut Airo power' : 'Restore Airo power'}
        </button>
      </div>

      <dl className="telemetry compact">
        <div className={b.watchdogOk ? 'ok' : 'warn'}>
          <dt>Watchdog (heartbeat)</dt>
          <dd className="mono">{b.watchdogOk ? 'OK' : 'tripped / no core'}</dd>
        </div>
        <div className={b.selector === 'airo' ? 'warn' : 'ok'}>
          <dt>Controller connected to</dt>
          <dd className="mono">{b.selector === 'parallel' ? 'keypad ∥ injectors' : b.selector === 'airo' ? 'Airo (selector energised)' : 'keypad (fail-safe)'}</dd>
        </div>
        <div>
          <dt>Airo injecting</dt>
          <dd className="mono">{b.injecting.join(' + ') || '—'}</dd>
        </div>
        <div>
          <dt>Controller sees</dt>
          <dd className="mono">
            {b.controllerSees.join(' + ') || '—'}
            {b.encodedAtController !== null ? ` (HS=${b.encodedAtController})` : ''}
          </dd>
        </div>
        <div>
          <dt>Preset running</dt>
          <dd className="mono">{view.activePreset ?? '—'}</dd>
        </div>
        <div>
          <dt>Controller memory</dt>
          <dd className="mono">
            sit {rig.controller.options.sitHeightMm} · stand {rig.controller.options.standHeightMm} mm
          </dd>
        </div>
      </dl>
      <p className="muted small">
        All interface behaviour here is modelled from the spec's assumptions; nothing is measured on the real desk yet.
      </p>
    </section>
  );
}
