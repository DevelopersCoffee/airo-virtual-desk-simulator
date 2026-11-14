/**
 * Client for the Airo local API (spec section 10). Used by the web dashboard
 * and by the end-to-end tests; a Flutter app would implement the same calls.
 */

export type DeskCommandName =
  | 'desk.up'
  | 'desk.down'
  | 'desk.stop'
  | 'desk.sitting'
  | 'desk.standing'
  | 'desk.get_status'
  | 'desk.renew'
  | 'desk.release'
  | 'desk.reset';

export type AiroSource = 'mobile_app' | 'web_dashboard' | 'home_assistant' | 'alexa' | 'mqtt' | 'simulator' | 'test';

export interface AiroStatus {
  type: 'status';
  state: 'SELF_TEST' | 'IDLE' | 'APP_ACTIVE' | 'MANUAL_ACTIVE' | 'BYPASS' | 'FAULT';
  command: string | null;
  owner: string | null;
  lease_remaining_ms: number;
  height_mm: number | null;
  height_age_ms: number | null;
  bypass: boolean;
  last_manual_ms: number | null;
  fault: string | null;
  blocked_direction: 'up' | 'down' | null;
  armed: boolean;
}

export interface AiroInfo {
  device_id: string;
  fw: string;
  api: number;
  hardware: string;
  adapter: string;
  interface_case: 'A1' | 'A2' | 'B' | null;
  board_connected: boolean;
  powered?: boolean;
  capabilities: {
    commands: string[];
    height_feedback: boolean;
    stop_method_verified: boolean;
    preset_mode: string;
    presets_require_height_feedback: boolean;
    reset_exposed: boolean;
  };
}

export interface AiroReply {
  type: 'ack' | 'error';
  request_id: string | null;
  result?: 'accepted' | 'duplicate';
  code?: string;
  detail?: string;
  status?: AiroStatus;
}

export interface AiroEvent {
  type: 'event';
  event: string;
  direction?: 'up' | 'down';
  detail?: string;
}

export interface AiroClientOptions {
  /** e.g. http://127.0.0.1:8080, or '' for same origin (Vite proxy). */
  baseUrl: string;
  /** e.g. ws://127.0.0.1:8080/api/v1/ws */
  wsUrl: string;
  token: string;
  deviceId?: string;
  source?: AiroSource;
}

let counter = 0;
export function requestId(): string {
  counter = (counter + 1) % 1e9;
  return `${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 8)}-${counter}`;
}

type Extra = { lease_ms?: number; request_id?: string };

export class AiroClient {
  readonly deviceId: string;
  readonly source: AiroSource;
  private ws: WebSocket | null = null;
  private pending = new Map<string, (r: AiroReply) => void>();
  private hold: { cmd: DeskCommandName; timer: ReturnType<typeof setInterval> } | null = null;
  /** Test hook: stop renewing without telling the device (a silent network drop). */
  renewalsPaused = false;
  lastStatus: AiroStatus | null = null;
  info: AiroInfo | null = null;
  onStatus?: (s: AiroStatus) => void;
  onEvent?: (e: AiroEvent) => void;
  onReply?: (r: AiroReply) => void;
  onClose?: () => void;

  constructor(private readonly opts: AiroClientOptions) {
    this.deviceId = opts.deviceId ?? 'airo-desk-001';
    this.source = opts.source ?? 'web_dashboard';
  }

  get connected(): boolean {
    return this.ws?.readyState === WebSocket.OPEN;
  }

  async getInfo(): Promise<AiroInfo> {
    const r = await fetch(`${this.opts.baseUrl}/api/v1/info`);
    if (!r.ok) throw new Error(`info: HTTP ${r.status}`);
    return (await r.json()) as AiroInfo;
  }

  async getStatus(): Promise<{ http: number; body: AiroStatus }> {
    const r = await fetch(`${this.opts.baseUrl}/api/v1/status`, { headers: this.headers() });
    return { http: r.status, body: (await r.json()) as AiroStatus };
  }

  async patchConfig(patch: Record<string, unknown>): Promise<{ http: number; body: Record<string, unknown> }> {
    const r = await fetch(`${this.opts.baseUrl}/api/v1/config`, {
      method: 'PATCH',
      headers: { ...this.headers(), 'Content-Type': 'application/json' },
      body: JSON.stringify(patch),
    });
    return { http: r.status, body: (await r.json()) as Record<string, unknown> };
  }

  envelope(command: DeskCommandName, extra: Extra = {}) {
    return {
      device_id: this.deviceId,
      command,
      source: this.source,
      request_id: extra.request_id ?? requestId(),
      ...(extra.lease_ms !== undefined ? { lease_ms: extra.lease_ms } : {}),
    };
  }

  /** POST /api/v1/commands. */
  async rest(command: DeskCommandName, extra: Extra = {}): Promise<{ http: number; body: AiroReply }> {
    const r = await fetch(`${this.opts.baseUrl}/api/v1/commands`, {
      method: 'POST',
      headers: { ...this.headers(), 'Content-Type': 'application/json' },
      body: JSON.stringify(this.envelope(command, extra)),
    });
    return { http: r.status, body: (await r.json()) as AiroReply };
  }

  connect(): Promise<AiroInfo> {
    return new Promise((resolve, reject) => {
      const ws = new WebSocket(this.opts.wsUrl);
      this.ws = ws;
      let authed = false;
      ws.onopen = () => ws.send(JSON.stringify({ type: 'auth', token: this.opts.token }));
      ws.onmessage = (e) => {
        const m = JSON.parse(String(e.data)) as { type: string } & Record<string, unknown>;
        if (m.type === 'auth_ok') {
          authed = true;
          this.info = m.info as AiroInfo;
          resolve(this.info);
          return;
        }
        if (m.type === 'status') {
          this.lastStatus = m as unknown as AiroStatus;
          this.onStatus?.(this.lastStatus);
        } else if (m.type === 'event') {
          this.onEvent?.(m as unknown as AiroEvent);
        } else if (m.type === 'ack' || m.type === 'error') {
          const reply = m as unknown as AiroReply;
          if (!authed && reply.code === 'unauthorized') reject(new Error('unauthorized'));
          this.onReply?.(reply);
          const id = reply.request_id;
          if (id && this.pending.has(id)) {
            this.pending.get(id)!(reply);
            this.pending.delete(id);
          }
        }
      };
      ws.onclose = () => {
        this.endHold();
        for (const done of this.pending.values()) done({ type: 'error', request_id: null, code: 'disconnected' });
        this.pending.clear();
        if (this.ws === ws) this.ws = null;
        if (!authed) reject(new Error('connection closed'));
        this.onClose?.();
      };
      ws.onerror = () => undefined; // a close event always follows
    });
  }

  /** Send over the WebSocket and resolve with the ack or error. */
  send(command: DeskCommandName, extra: Extra = {}): Promise<AiroReply> {
    const env = this.envelope(command, extra);
    return new Promise((resolve) => {
      if (!this.connected) {
        resolve({ type: 'error', request_id: env.request_id, code: 'disconnected' });
        return;
      }
      this.pending.set(env.request_id, resolve);
      this.ws!.send(JSON.stringify(env));
    });
  }

  /**
   * Hold-to-move: start desk.up / desk.down and renew every renewMs until
   * releaseHold(). If renewals stop (network drop), the lease expires on the device.
   */
  async startHold(cmd: 'desk.up' | 'desk.down', leaseMs = 400, renewMs = 150): Promise<AiroReply> {
    this.endHold();
    const reply = await this.send(cmd, { lease_ms: leaseMs });
    if (reply.type !== 'ack') return reply;
    const timer = setInterval(() => {
      if (!this.renewalsPaused) void this.send('desk.renew', { lease_ms: leaseMs });
    }, renewMs);
    this.hold = { cmd, timer };
    return reply;
  }

  async releaseHold(): Promise<void> {
    if (this.endHold()) await this.send('desk.release');
  }

  get holding(): DeskCommandName | null {
    return this.hold?.cmd ?? null;
  }

  stop(): Promise<AiroReply> {
    this.endHold();
    return this.send('desk.stop');
  }

  /** Abrupt close (no release): the device must release the lease itself. */
  close(): void {
    this.endHold();
    this.ws?.close();
    this.ws = null;
  }

  private endHold(): boolean {
    if (!this.hold) return false;
    clearInterval(this.hold.timer);
    this.hold = null;
    return true;
  }

  private headers(): Record<string, string> {
    return { Authorization: `Bearer ${this.opts.token}` };
  }
}
