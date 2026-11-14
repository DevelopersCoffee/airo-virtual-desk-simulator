import type { AiroClient, AiroInfo, AiroStatus } from '../packages/airo-link';
import type { LogLine } from './useAiroApp';

interface Props {
  client: AiroClient;
  connected: boolean;
  info: AiroInfo | null;
  status: AiroStatus | null;
  log: LogLine[];
}

const STATE_LABEL: Record<AiroStatus['state'], string> = {
  SELF_TEST: 'Self-test',
  IDLE: 'Idle',
  APP_ACTIVE: 'App moving',
  MANUAL_ACTIVE: 'Keypad in use',
  BYPASS: 'Manual only',
  FAULT: 'Fault',
};

/** Hold-to-move button: lease renewed while the pointer is down. */
function HoldButton({ client, cmd, label, disabled, active }: { client: AiroClient; cmd: 'desk.up' | 'desk.down'; label: string; disabled: boolean; active: boolean }) {
  const end = () => void client.releaseHold();
  return (
    <button
      className={`btn ${active ? 'active' : ''}`}
      disabled={disabled}
      onPointerDown={(e) => {
        (e.target as HTMLElement).setPointerCapture?.(e.pointerId);
        void client.startHold(cmd, 400, 100);
      }}
      onPointerUp={end}
      onPointerCancel={end}
      onLostPointerCapture={end}
    >
      {label}
    </button>
  );
}

export function AppPanel({ client, connected, info, status, log }: Props) {
  const state = status?.state;
  const canMove = connected && (state === 'IDLE' || state === 'APP_ACTIVE');
  const height = status?.height_mm ?? null;

  return (
    <div className="app-panel">
      <div className="brand">
        <div className="logo">A</div>
        <div>
          <h1>Airo Desk</h1>
          <p className="muted small">App · local API {info ? `· ${info.device_id}` : ''}</p>
        </div>
      </div>

      <div className="status-row">
        <span className={`dot ${!connected ? 'off' : state === 'APP_ACTIVE' ? 'live' : state === 'FAULT' ? 'fault' : state === 'IDLE' ? '' : 'paused'}`} />
        <span className="muted">Airo Core</span>
        <span className="right-align">{connected && state ? STATE_LABEL[state] : 'Offline'}</span>
      </div>
      {!connected && (
        <p className="muted small">
          Start the firmware host with <code>npm run airo</code> (or <code>npm run sim</code> for both). The physical keypad
          keeps working without it.
        </p>
      )}
      {connected && status?.fault && <p className="err small">Fault: {status.fault}. The interface board may be disconnected.</p>}

      <div className="height">
        <div className="big">
          {height === null ? '--' : (height / 10).toFixed(1)}
          <span className="unit">cm</span>
        </div>
        <div className="muted caps">Reported height</div>
        <div className="muted small">{height === null ? 'Height feedback not yet confirmed' : `from the controller · ${status?.height_age_ms ?? 0} ms old`}</div>
      </div>

      <div className="btn-grid">
        <HoldButton client={client} cmd="desk.up" label="▲ Up" disabled={!canMove} active={status?.command === 'desk.up'} />
        <HoldButton client={client} cmd="desk.down" label="▼ Down" disabled={!canMove} active={status?.command === 'desk.down'} />
        <button className={`btn ${status?.command === 'desk.sitting' ? 'active' : ''}`} disabled={!canMove} onClick={() => void client.send('desk.sitting')}>
          Sitting
        </button>
        <button className={`btn ${status?.command === 'desk.standing' ? 'active' : ''}`} disabled={!canMove} onClick={() => void client.send('desk.standing')}>
          Standing
        </button>
        <button className="btn stop wide" disabled={!connected} onClick={() => void client.stop()}>
          ■ Stop
        </button>
      </div>
      <p className="muted small center-text">Hold Up/Down to move. Space = Stop.</p>
      {info && !info.capabilities.stop_method_verified && (
        <p className="muted small">STOP releases app control; stopping a running preset uses an unverified method.</p>
      )}

      <dl className="telemetry compact">
        <div>
          <dt>Command</dt>
          <dd className="mono">{status?.command ?? '—'}</dd>
        </div>
        <div>
          <dt>Owner</dt>
          <dd className="mono">{status?.owner ?? '—'}</dd>
        </div>
        <div>
          <dt>Lease</dt>
          <dd className="mono">{status?.lease_remaining_ms ? `${status.lease_remaining_ms} ms` : '—'}</dd>
        </div>
        <div>
          <dt>Blocked</dt>
          <dd className="mono">{status?.blocked_direction ?? '—'}</dd>
        </div>
        <div>
          <dt>Presets</dt>
          <dd className="mono">{info ? (info.capabilities.presets_require_height_feedback ? 'need feedback' : 'controller-native') : '—'}</dd>
        </div>
      </dl>

      <div className="log">
        {log.map((l) => (
          <div key={l.at + l.text} className={`log-${l.tone} small mono`}>
            {l.text}
          </div>
        ))}
      </div>
    </div>
  );
}
