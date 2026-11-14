import type { VirtualKeypad } from './keypad';
import type { OriginalDeskController } from './original-controller';
import { KEY_BUTTONS, type InterfaceCase, type KeyButton } from './types';

/** Bridge messages, Interface Board -> Airo Core (Port C, core's inputs). */
export type BoardToCore =
  | { t: 'hello'; case: InterfaceCase; manual_only: boolean; powered: boolean }
  | { t: 'case'; case: InterfaceCase }
  | { t: 'sense'; lines: KeyButton[] }
  | { t: 'sense'; hs: number }
  | { t: 'uart'; line: 'htx' | 'dtx'; bytes: number[] }
  | { t: 'bypass'; on: boolean }
  | { t: 'sim.power'; on: boolean };

/** Bridge messages, Airo Core -> Interface Board (Port C, core's outputs). */
export type CoreToBoard =
  | { t: 'hb' }
  | { t: 'arm'; on: boolean }
  | { t: 'inject'; lines: KeyButton[] }
  | { t: 'inject'; hs: number }
  | { t: 'uart'; line: 'htx'; bytes: number[] };

export interface BoardSnapshot {
  interfaceCase: InterfaceCase;
  manualOnly: boolean;
  corePowered: boolean;
  watchdogOk: boolean;
  /** Which source the controller is connected to. A1 has no selector (parallel injectors). */
  selector: 'keypad' | 'airo' | 'parallel';
  armRequested: boolean;
  injecting: KeyButton[];
  keypad: KeyButton[];
  controllerSees: KeyButton[];
  encodedAtController: number | null;
}

export interface InterfaceBoardOptions {
  /** Retriggerable monostable timeout (spec 6.1 R7): ~200 ms. */
  watchdogMs?: number;
}

/**
 * Model of the Airo Interface Board (spec section 6): Ports A (keypad),
 * B (controller) and C (core, here the JSON bridge).
 *
 * Fail-safe rules modelled:
 * - R5: the unpowered / de-energised selector connects keypad -> controller.
 * - R6: MANUAL ONLY forces the keypad path and cuts injectors, regardless of firmware.
 * - R7: injectors and selector are enabled only while HEARTBEAT keeps arriving.
 * - R4: in A2 / B the selector picks exactly one source; only A1 injects in parallel.
 */
export class InterfaceBoard {
  interfaceCase: InterfaceCase;
  private manualOnly = false;
  private corePowered = true;
  private readonly watchdogMs: number;
  private lastHbMs = -Infinity;
  private armRequested = false;
  private injLines: KeyButton[] = [];
  private injHs = 0;
  private nowMs = 0;
  private lastSenseVersion = -1;
  private outbox: BoardToCore[] = [];
  private lastEncodedAtController: number | null = null;

  constructor(
    readonly keypad: VirtualKeypad,
    readonly controller: OriginalDeskController,
    interfaceCase: InterfaceCase = 'A2',
    opts: InterfaceBoardOptions = {},
  ) {
    this.interfaceCase = interfaceCase;
    this.watchdogMs = opts.watchdogMs ?? 200;
    this.controller.setReportsHeight(interfaceCase === 'B');
  }

  hello(): BoardToCore {
    return { t: 'hello', case: this.interfaceCase, manual_only: this.manualOnly, powered: this.corePowered };
  }

  /** Swap the board for one built for another case (models a different desk). */
  setInterfaceCase(c: InterfaceCase): void {
    if (c === this.interfaceCase) return;
    this.interfaceCase = c;
    this.clearCoreOutputs();
    this.controller.setLines([]);
    this.controller.setEncoded(0);
    this.controller.setReportsHeight(c === 'B');
    this.lastSenseVersion = -1;
    this.outbox.push({ t: 'case', case: c });
  }

  /** The panel's MANUAL ONLY toggle (hardware bypass). */
  setManualOnly(on: boolean): void {
    this.manualOnly = on;
    if (this.corePowered) this.outbox.push({ t: 'bypass', on });
  }

  /** Cut or restore the Airo Core's USB-C power. */
  setCorePowered(on: boolean): void {
    if (on === this.corePowered) return;
    this.corePowered = on;
    this.clearCoreOutputs();
    this.lastSenseVersion = -1;
    this.outbox.push({ t: 'sim.power', on });
  }

  /** The core disconnected or crashed: its outputs float, the watchdog will drop them. */
  coreDisconnected(): void {
    this.lastHbMs = -Infinity;
  }

  receive(msg: CoreToBoard): void {
    if (!this.corePowered) return; // an unpowered core drives nothing
    switch (msg.t) {
      case 'hb':
        this.lastHbMs = this.nowMs;
        break;
      case 'arm':
        this.armRequested = msg.on;
        break;
      case 'inject':
        if ('lines' in msg) this.injLines = msg.lines.filter((l) => KEY_BUTTONS.includes(l));
        else this.injHs = msg.hs | 0;
        break;
      case 'uart':
        // Core UART TX -> open-drain buffer -> K2 NO contact -> controller HTX.
        if (this.interfaceCase === 'B' && this.airoPath()) this.controller.receiveBytes(msg.bytes);
        break;
    }
  }

  /** Run one fixed step at simulated time nowMs. Must be called before the controller updates. */
  step(nowMs: number): void {
    this.nowMs = nowMs;
    const enabled = this.enabled();
    const airo = this.airoPath();
    switch (this.interfaceCase) {
      case 'A1': {
        // Injectors in parallel with each button; both only pull low (6.2).
        const s = new Set<KeyButton>(this.keypad.pressed());
        if (enabled) for (const l of this.injLines) s.add(l);
        this.controller.setLines(s);
        this.sendSense(() => ({ t: 'sense', lines: this.keypad.pressed() }));
        break;
      }
      case 'A2': {
        // Selector K1: NC = keypad, NO = injectors. Never both (6.3).
        const code = airo ? this.injHs : this.keypad.encodedCode();
        this.lastEncodedAtController = code;
        this.controller.setEncoded(code);
        this.sendSense(() => ({ t: 'sense', hs: this.keypad.encodedCode() }));
        break;
      }
      case 'B': {
        // K2: NC = keypad HTX straight through; NO = core is the only driver (6.5).
        const frame = this.keypad.frameDue(nowMs);
        if (frame) {
          if (!airo) this.controller.receiveBytes(frame);
          if (this.corePowered) this.outbox.push({ t: 'uart', line: 'htx', bytes: frame });
        }
        // DTX is never broken; the core listens through an RX buffer.
        for (const f of this.controller.takeTx()) if (this.corePowered) this.outbox.push({ t: 'uart', line: 'dtx', bytes: f });
        break;
      }
    }
  }

  takeOutbox(): BoardToCore[] {
    const out = this.outbox;
    this.outbox = [];
    return out;
  }

  trimOutbox(max: number): void {
    if (this.outbox.length > max) this.outbox.splice(0, this.outbox.length - max);
  }

  watchdogOk(): boolean {
    return this.corePowered && this.nowMs - this.lastHbMs <= this.watchdogMs;
  }

  /** Watchdog AND NOT MANUAL ONLY: injector and selector enable (R6, R7). */
  enabled(): boolean {
    return this.watchdogOk() && !this.manualOnly;
  }

  /** Selector energised: the controller listens to Airo instead of the keypad. */
  airoPath(): boolean {
    return this.enabled() && this.armRequested && this.interfaceCase !== 'A1';
  }

  snapshot(): BoardSnapshot {
    const enabled = this.enabled();
    return {
      interfaceCase: this.interfaceCase,
      manualOnly: this.manualOnly,
      corePowered: this.corePowered,
      watchdogOk: this.watchdogOk(),
      selector: this.interfaceCase === 'A1' ? 'parallel' : this.airoPath() ? 'airo' : 'keypad',
      armRequested: this.armRequested,
      injecting: !enabled
        ? []
        : this.interfaceCase === 'A1'
          ? [...this.injLines]
          : this.interfaceCase === 'A2' && this.injHs
            ? KEY_BUTTONS.filter((b) => ({ down: 1, up: 2, sit: 3, stand: 4, reset: 8 })[b] === this.injHs)
            : [],
      keypad: this.keypad.pressed(),
      controllerSees: this.controller.seen(),
      encodedAtController: this.interfaceCase === 'A2' ? this.lastEncodedAtController : null,
    };
  }

  private sendSense(make: () => BoardToCore): void {
    if (!this.corePowered) return;
    if (this.keypad.stateVersion === this.lastSenseVersion) return;
    this.lastSenseVersion = this.keypad.stateVersion;
    this.outbox.push(make());
  }

  private clearCoreOutputs(): void {
    this.armRequested = false;
    this.injLines = [];
    this.injHs = 0;
    this.lastHbMs = -Infinity;
  }
}
