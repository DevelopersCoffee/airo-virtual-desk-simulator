import type { BoardToCore, CoreToBoard, InterfaceCase, KeyButton } from '../desk-hardware';
import type { DeskRig } from './desk-rig';
import type { BridgeState } from './bridge-client';

/** State the board reports on its bench link (firmware/board/bench_link.h). */
export interface BenchState {
  bench_mode: boolean;
  mode: 'bench' | 'desk';
  drive_enabled: boolean;
  inputs: 'link' | 'physical';
  desk_mode_compiled: boolean;
  pins_verified: boolean;
  interface_case: InterfaceCase;
}

/** Laptop -> board bench messages. */
export type ToBench =
  | { t: 'hello'; case: InterfaceCase; manual_only: boolean; powered: boolean }
  | { t: 'in'; s: Record<string, boolean> }
  | { t: 'rx'; line: 'htx' | 'dtx'; bytes: number[] }
  | { t: 'power'; on: boolean }
  | { t: 'hang'; ms: number }
  | { t: 'halt' }
  | { t: 'reboot' };

/** Board -> laptop bench messages. */
export type FromBench =
  | ({ t: 'bench' } & BenchState)
  | { t: 'out'; s: Record<string, boolean> }
  | { t: 'tx'; line: 'htx'; bytes: number[] };

const A1_SENSE: [KeyButton, string][] = [
  ['up', 'sense_up'],
  ['down', 'sense_down'],
  ['sit', 'sense_sit'],
  ['stand', 'sense_stand'],
  ['reset', 'sense_reset'],
];
const A1_INJ: [KeyButton, string][] = [
  ['up', 'inj_up'],
  ['down', 'inj_down'],
  ['sit', 'inj_sit'],
  ['stand', 'inj_stand'],
];
const HS_INJ = ['inj_hs0', 'inj_hs1', 'inj_hs2', 'inj_hs3'];
const HS_SENSE = ['sense_hs0', 'sense_hs1', 'sense_hs2', 'sense_hs3'];

/**
 * Pure translation between the simulated Interface Board's bridge messages
 * (what the board model would put on Port C) and pin-level bench messages
 * (what Esp32HardwareAdapter reads and drives). No socket, so it is unit-tested.
 */
export class PinTranslator {
  private interfaceCase: InterfaceCase = 'A2';
  private manualOnly = false;
  private powered = true;
  private outs: Record<string, boolean> = {};

  /** Board model -> board under test. */
  toBench(m: BoardToCore): ToBench[] {
    switch (m.t) {
      case 'hello':
        this.interfaceCase = m.case;
        this.manualOnly = m.manual_only;
        this.powered = m.powered;
        this.outs = {};
        return [{ t: 'hello', case: m.case, manual_only: m.manual_only, powered: m.powered }];
      case 'case':
        // A board built for another case: the core reboots onto it, as on the laptop host.
        this.interfaceCase = m.case;
        this.outs = {};
        return [{ t: 'hello', case: m.case, manual_only: this.manualOnly, powered: this.powered }];
      case 'sense': {
        const s: Record<string, boolean> = {};
        if ('lines' in m) for (const [b, name] of A1_SENSE) s[name] = m.lines.includes(b);
        else HS_SENSE.forEach((name, bit) => (s[name] = ((m.hs >> bit) & 1) === 1));
        return [{ t: 'in', s }];
      }
      case 'uart':
        return [{ t: 'rx', line: m.line, bytes: m.bytes }];
      case 'bypass':
        this.manualOnly = m.on;
        return [{ t: 'in', s: { bypass_state: m.on } }];
      case 'sim.power':
        this.powered = m.on;
        return [{ t: 'power', on: m.on }];
    }
  }

  /** Board under test -> board model. */
  fromBench(m: FromBench): CoreToBoard[] {
    if (m.t === 'tx') return [{ t: 'uart', line: 'htx', bytes: m.bytes }];
    if (m.t !== 'out') return [];
    const out: CoreToBoard[] = [];
    const before = { ...this.outs };
    Object.assign(this.outs, m.s);
    if ('heartbeat' in m.s && m.s.heartbeat !== before.heartbeat) out.push({ t: 'hb' });
    if ('arm' in m.s && m.s.arm !== !!before.arm) out.push({ t: 'arm', on: m.s.arm });
    if (this.interfaceCase === 'A1' && A1_INJ.some(([, n]) => n in m.s))
      out.push({ t: 'inject', lines: A1_INJ.filter(([, n]) => this.outs[n]).map(([b]) => b) });
    if (this.interfaceCase === 'A2' && HS_INJ.some((n) => n in m.s))
      out.push({ t: 'inject', hs: HS_INJ.reduce((code, n, bit) => code | (this.outs[n] ? 1 << bit : 0), 0) });
    return out;
  }
}

/**
 * Connects a DeskRig to a board's /bench WebSocket (the ESP32-S3, or
 * `airo-host --adapter esp32`). The desk model plays the Interface Board,
 * keypad and controller; the board under test runs the real
 * Esp32HardwareAdapter pin mapping. Reconnects like BridgeClient.
 */
export class PinBridge {
  state: BridgeState = 'closed';
  bench: BenchState | null = null;
  onState?: (s: BridgeState) => void;
  onBench?: (s: BenchState) => void;
  private ws: WebSocket | null = null;
  private retry: ReturnType<typeof setTimeout> | null = null;
  private stopped = false;
  private tr = new PinTranslator();

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

  /** Bench controls, used by the hardware-in-the-loop scenarios. */
  control(m: Extract<ToBench, { t: 'hang' | 'halt' | 'reboot' }>): void {
    if (this.ws?.readyState === WebSocket.OPEN) this.ws.send(JSON.stringify(m));
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
    this.tr = new PinTranslator();
    ws.onopen = () => {
      this.set('open');
      this.rig.attach({
        send: (text) => {
          if (ws.readyState !== WebSocket.OPEN) return;
          for (const m of this.tr.toBench(JSON.parse(text) as BoardToCore)) ws.send(JSON.stringify(m));
        },
      });
    };
    ws.onmessage = (e) => {
      let m: FromBench;
      try {
        m = JSON.parse(String(e.data)) as FromBench;
      } catch {
        return;
      }
      if (m.t === 'bench') {
        const { t: _t, ...s } = m;
        this.bench = s;
        this.onBench?.(s);
        return;
      }
      for (const c of this.tr.fromBench(m)) this.rig.receive(JSON.stringify(c));
    };
    ws.onclose = () => {
      if (this.ws === ws) this.ws = null;
      this.rig.detach();
      this.set('closed');
      this.schedule();
    };
    ws.onerror = () => undefined;
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
