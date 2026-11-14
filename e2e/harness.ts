import { spawn, type ChildProcess } from 'node:child_process';
import { existsSync } from 'node:fs';
import { join } from 'node:path';
import { AiroClient, BridgeClient, DeskRig, type AiroEvent, type AiroSource } from '../src/packages/airo-link';
import type { InterfaceCase } from '../src/packages/desk-hardware';
import { defaultProfile, type DeskProfile } from '../src/packages/desk-profiles';

export const HOST_BIN = join(__dirname, '..', 'firmware', 'build', 'airo-host');
export const TOKEN = 'e2e-token';

export function hostBuilt(): boolean {
  return existsSync(HOST_BIN);
}

export const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

export async function waitFor(pred: () => boolean | Promise<boolean>, timeoutMs = 3000, what = 'condition'): Promise<void> {
  const t0 = Date.now();
  while (!(await pred())) {
    if (Date.now() - t0 > timeoutMs) throw new Error(`timed out after ${timeoutMs} ms waiting for ${what}`);
    await sleep(5);
  }
}

export interface Host {
  port: number;
  proc: ChildProcess;
  kill(signal?: NodeJS.Signals): Promise<void>;
}

export function startHost(extra: string[] = []): Promise<Host> {
  return new Promise((resolve, reject) => {
    const proc = spawn(HOST_BIN, ['--port', '0', '--token', TOKEN, '--quiet', '--set', 'boot_quiet_ms=500', ...extra], {
      stdio: ['ignore', 'pipe', 'pipe'],
    });
    let out = '';
    const timer = setTimeout(() => reject(new Error(`airo-host did not start: ${out}`)), 5000);
    proc.stdout!.on('data', (d: Buffer) => {
      out += d.toString();
      const m = /AIRO_HOST_READY port=(\d+)/.exec(out);
      if (m) {
        clearTimeout(timer);
        const exited = new Promise<void>((r) => proc.once('exit', () => r()));
        resolve({
          port: Number(m[1]),
          proc,
          kill: async (signal = 'SIGTERM') => {
            if (proc.exitCode === null && proc.signalCode === null) proc.kill(signal);
            if (signal !== 'SIGSTOP' && signal !== 'SIGCONT') await exited;
          },
        });
      }
    });
    proc.stderr!.on('data', (d: Buffer) => (out += d.toString()));
    proc.once('exit', (code) => reject(new Error(`airo-host exited (${code}): ${out}`)));
  });
}

/** The desk side (keypad, board, controller, 3D mechanism) running in real time. */
export class LiveRig {
  readonly rig: DeskRig;
  readonly bridge: BridgeClient;
  private timer: ReturnType<typeof setInterval>;
  private last = performance.now();
  private watchers: (() => void)[] = [];

  constructor(port: number, interfaceCase: InterfaceCase, profile: DeskProfile = defaultProfile) {
    this.rig = new DeskRig({ interfaceCase, profile });
    this.bridge = new BridgeClient(this.rig, `ws://127.0.0.1:${port}/bridge`, 200);
    this.timer = setInterval(() => {
      const now = performance.now();
      this.rig.advance(Math.min(0.1, (now - this.last) / 1000));
      this.last = now;
      for (const w of this.watchers) w();
    }, 1);
    this.bridge.start();
  }

  /** Simulated time, ms. */
  get nowMs(): number {
    return this.rig.sim.snapshot().timeS * 1000;
  }

  get heightMm(): number {
    return this.rig.sim.snapshot().mechanical.heightMm;
  }

  get moving(): 'up' | 'down' | null {
    const s = this.rig.sim.snapshot().controllerStatus;
    return s === 'moving-up' ? 'up' : s === 'moving-down' ? 'down' : null;
  }

  /** Runs fn after every rig step batch, until it returns true. */
  watch(fn: () => boolean): void {
    const w = () => {
      if (fn()) this.watchers = this.watchers.filter((x) => x !== w);
    };
    this.watchers.push(w);
  }

  presetsStarted(): string[] {
    return this.rig.controller.log.filter((e) => e.kind === 'preset-sit' || e.kind === 'preset-stand').map((e) => e.kind);
  }

  stop(): void {
    clearInterval(this.timer);
    this.bridge.stop();
  }
}

export function appClient(port: number, source: AiroSource = 'mobile_app', token = TOKEN): AiroClient {
  return new AiroClient({
    baseUrl: `http://127.0.0.1:${port}`,
    wsUrl: `ws://127.0.0.1:${port}/api/v1/ws`,
    token,
    source,
  });
}

export function recordEvents(c: AiroClient): AiroEvent[] {
  const ev: AiroEvent[] = [];
  c.onEvent = (e) => ev.push(e);
  return ev;
}

export interface Bench {
  host: Host;
  desk: LiveRig;
  app: AiroClient;
  events: AiroEvent[];
  close(): Promise<void>;
}

/** Host + desk + one connected app client, waited until the core is IDLE. */
export async function bench(
  interfaceCase: InterfaceCase,
  opts: { hostArgs?: string[]; profile?: DeskProfile } = {},
): Promise<Bench> {
  const host = await startHost(opts.hostArgs);
  const desk = new LiveRig(host.port, interfaceCase, opts.profile);
  const app = appClient(host.port);
  await waitFor(async () => (await app.getStatus()).body.state === 'IDLE', 5000, 'core IDLE');
  await app.connect();
  const events = recordEvents(app);
  return {
    host,
    desk,
    app,
    events,
    close: async () => {
      app.close();
      desk.stop();
      await host.kill('SIGCONT').catch(() => undefined);
      await host.kill();
    },
  };
}

export function shortProfile(rangeMm: number): DeskProfile {
  const p = structuredClone(defaultProfile);
  p.mechanical.maxHeightMm.value = p.mechanical.minHeightMm.value + rangeMm;
  return p;
}
