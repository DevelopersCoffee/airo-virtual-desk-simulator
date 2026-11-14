import type { DeskRig } from './desk-rig';

export type BridgeState = 'connecting' | 'open' | 'closed';

/**
 * Connects a DeskRig to airo-host's /bridge WebSocket and reconnects when
 * the host restarts. Uses the standard WebSocket (browser, Node >= 22).
 */
export class BridgeClient {
  state: BridgeState = 'closed';
  private ws: WebSocket | null = null;
  private retry: ReturnType<typeof setTimeout> | null = null;
  private stopped = false;
  onState?: (s: BridgeState) => void;

  constructor(
    private readonly rig: DeskRig,
    private readonly url: string,
    private readonly retryMs = 1000,
  ) {}

  start(): void {
    this.stopped = false;
    this.open();
  }

  stop(): void {
    this.stopped = true;
    if (this.retry) clearTimeout(this.retry);
    this.retry = null;
    this.ws?.close();
    this.ws = null;
    this.rig.detach();
    this.set('closed');
  }

  private open(): void {
    this.set('connecting');
    let ws: WebSocket;
    try {
      ws = new WebSocket(this.url);
    } catch {
      this.schedule();
      return;
    }
    this.ws = ws;
    ws.onopen = () => {
      this.set('open');
      this.rig.attach({ send: (t) => ws.readyState === WebSocket.OPEN && ws.send(t) });
    };
    ws.onmessage = (e) => this.rig.receive(String(e.data));
    ws.onclose = () => {
      if (this.ws === ws) this.ws = null;
      this.rig.detach();
      this.set('closed');
      this.schedule();
    };
    ws.onerror = () => undefined; // a close event always follows
  }

  private schedule(): void {
    if (this.stopped || this.retry) return;
    this.retry = setTimeout(() => {
      this.retry = null;
      if (!this.stopped) this.open();
    }, this.retryMs);
  }

  private set(s: BridgeState): void {
    this.state = s;
    this.onState?.(s);
  }
}
