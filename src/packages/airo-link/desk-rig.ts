import { defaultProfile, type DeskProfile } from '../desk-profiles';
import {
  InterfaceBoard,
  OriginalDeskController,
  VirtualKeypad,
  type BoardToCore,
  type CoreToBoard,
  type InterfaceCase,
  type OriginalControllerOptions,
} from '../desk-hardware';
import { Simulation, type SimulationOptions } from '../simulation-engine';

export interface DeskRigOptions {
  profile?: DeskProfile;
  interfaceCase?: InterfaceCase;
  controller?: Partial<OriginalControllerOptions>;
  simulation?: Omit<SimulationOptions, 'createController' | 'beforeStep'>;
}

/** Anything that can carry bridge text to the Airo Core (WebSocket, or a test double). */
export interface CoreLink {
  send(text: string): void;
}

/**
 * The whole desk side of the laptop simulation, on one fixed clock:
 * keypad -> interface board -> original controller -> motor -> mechanism.
 * The Airo Core (C++ airo-host) attaches through a CoreLink.
 */
export class DeskRig {
  readonly keypad = new VirtualKeypad();
  readonly controller: OriginalDeskController;
  readonly board: InterfaceBoard;
  readonly sim: Simulation;
  private link: CoreLink | null = null;

  constructor(opts: DeskRigOptions = {}) {
    const profile = opts.profile ?? defaultProfile;
    const min = profile.mechanical.minHeightMm.value;
    const max = profile.mechanical.maxHeightMm.value;
    this.controller = new OriginalDeskController({
      sitHeightMm: Math.round(min + 0.06 * (max - min)),
      standHeightMm: Math.round(min + 0.8 * (max - min)),
      ...opts.controller,
    });
    this.board = new InterfaceBoard(this.keypad, this.controller, opts.interfaceCase ?? 'A2');
    this.sim = new Simulation(profile, {
      ...opts.simulation,
      createController: () => this.controller,
      beforeStep: (t) => this.board.step(t * 1000),
    });
  }

  get linked(): boolean {
    return this.link !== null;
  }

  attach(link: CoreLink): void {
    this.link = link;
    this.board.takeOutbox(); // the hello carries the current state
    link.send(JSON.stringify(this.board.hello()));
  }

  detach(): void {
    this.link = null;
    this.board.coreDisconnected();
  }

  /** A bridge message from the core. */
  receive(text: string): void {
    let msg: CoreToBoard;
    try {
      msg = JSON.parse(text) as CoreToBoard;
    } catch {
      return;
    }
    this.board.receive(msg);
    this.flush();
  }

  /** Feed elapsed wall-clock seconds; steps the whole rig. */
  advance(realElapsedS: number): number {
    const n = this.sim.advance(realElapsedS);
    this.flush();
    return n;
  }

  step(): void {
    this.sim.step();
    this.flush();
  }

  /** Sends pending board messages to the core. Unlinked, the board keeps them (bounded). */
  flush(): void {
    if (!this.link) {
      this.board.trimOutbox(200);
      return;
    }
    const out: BoardToCore[] = this.board.takeOutbox();
    for (const m of out) this.link.send(JSON.stringify(m));
  }
}
