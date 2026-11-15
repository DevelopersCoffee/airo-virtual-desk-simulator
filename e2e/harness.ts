import { spawn, type ChildProcess } from 'node:child_process';
import { existsSync } from 'node:fs';
import { join } from 'node:path';
import {
  AiroClient,
  benchRefusals,
  BridgeClient,
  DeskRig,
  PinBridge,
  type AiroEvent,
  type AiroInfo,
  type AiroSource,
} from '../src/packages/airo-link';
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
  readonly bridge: BridgeClient | PinBridge;
  private timer: ReturnType<typeof setInterval>;
  private last = performance.now();
  private watchers: (() => void)[] = [];

  /**
   * link 'bridge': the laptop host's /bridge (VirtualHardwareAdapter).
   * link 'bench': a board's /bench, pin level (Esp32HardwareAdapter on the
   * ESP32-S3 or on `airo-host --adapter esp32`).
   */
  constructor(wsBase: string, interfaceCase: InterfaceCase, profile: DeskProfile = defaultProfile, link: 'bridge' | 'bench' = 'bridge') {
    this.rig = new DeskRig({ interfaceCase, profile });
    this.bridge =
      link === 'bridge' ? new BridgeClient(this.rig, `${wsBase}/bridge`, 200) : new PinBridge(this.rig, `${wsBase}/bench`, 200);
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

export function appClient(base: string | number, source: AiroSource = 'mobile_app', token = TOKEN): AiroClient {
  const http = typeof base === 'number' ? `http://127.0.0.1:${base}` : base;
  return new AiroClient({
    baseUrl: http,
    wsUrl: `${http.replace(/^http/, 'ws')}/api/v1/ws`,
    token,
    source,
  });
}

export function recordEvents(c: AiroClient): AiroEvent[] {
  const ev: AiroEvent[] = [];
  c.onEvent = (e) => ev.push(e);
  return ev;
}

export interface BenchOptions {
  profile?: DeskProfile;
  /** Config fields for this scenario (--set on the laptop, PATCH on a board). */
  config?: Record<string, unknown>;
}

/**
 * Where the scenario table runs:
 *  - laptop:       airo-host + VirtualHardwareAdapter over /bridge (PR #3 matrix)
 *  - laptop-esp32: airo-host --adapter esp32: the board's Esp32HardwareAdapter
 *                  on in-memory pins, pin-level over /bench. Runs in CI.
 *  - board:        the ESP32-S3 itself over Wi-Fi (AIRO_BOARD_URL), bench mode only.
 */
export type TargetName = 'laptop' | 'laptop-esp32' | 'board';

export interface Target {
  name: TargetName;
  adapter: 'virtual' | 'esp32';
  /** Extra time allowed for timing checks (Wi-Fi round trips on the board). */
  slackMs: number;
  open(c: InterfaceCase, opts?: BenchOptions): Promise<Bench>;
}

export interface Bench {
  target: Target;
  baseUrl: string;
  desk: LiveRig;
  app: AiroClient;
  events: AiroEvent[];
  /** The core dies mid-move (SIGKILL on the laptop, bench "halt" on the board). */
  crash(): Promise<void>;
  /** The core hangs (SIGSTOP / bench "hang"); resume() ends it. */
  hang(): Promise<void>;
  resume(): Promise<void>;
  close(): Promise<void>;
}

async function connectApp(baseUrl: string, token: string, desk: LiveRig, opts: { patch?: Record<string, unknown> }) {
  const app = appClient(baseUrl, 'mobile_app', token);
  await waitFor(async () => (await app.getStatus()).body.state === 'IDLE', 8000, 'core IDLE');
  if (opts.patch) {
    const r = await app.patchConfig(opts.patch);
    if (r.http !== 200) throw new Error(`PATCH /api/v1/config failed: ${JSON.stringify(r.body)}`);
  }
  await app.connect();
  return { app, events: recordEvents(app), desk };
}

function laptopTarget(name: 'laptop' | 'laptop-esp32'): Target {
  const esp32 = name === 'laptop-esp32';
  const t: Target = {
    name,
    adapter: esp32 ? 'esp32' : 'virtual',
    slackMs: 0,
    async open(c, opts = {}) {
      const args = Object.entries(opts.config ?? {}).flatMap(([k, v]) => ['--set', `${k}=${JSON.stringify(v)}`]);
      if (esp32) args.push('--adapter', 'esp32');
      const host = await startHost(args);
      const baseUrl = `http://127.0.0.1:${host.port}`;
      const desk = new LiveRig(`ws://127.0.0.1:${host.port}`, c, opts.profile, esp32 ? 'bench' : 'bridge');
      try {
        if (esp32) await assertBenchSafe(appClient(baseUrl));
        const { app, events } = await connectApp(baseUrl, TOKEN, desk, {});
        return {
          target: t,
          baseUrl,
          desk,
          app,
          events,
          crash: () => host.kill('SIGKILL'),
          hang: () => host.kill('SIGSTOP'),
          resume: () => host.kill('SIGCONT'),
          close: async () => {
            app.close();
            desk.stop();
            await host.kill('SIGCONT').catch(() => undefined);
            await host.kill();
          },
        };
      } catch (e) {
        desk.stop();
        await host.kill();
        throw e;
      }
    },
  };
  return t;
}

/** Throws unless the target is a board (or board adapter) in bench mode. */
export async function assertBenchSafe(client: AiroClient): Promise<AiroInfo> {
  let info: AiroInfo | null = null;
  try {
    info = await client.getInfo();
  } catch {
    info = null;
  }
  const why = benchRefusals(info, { allowDrive: process.env.AIRO_HIL_ALLOW_DRIVE === '1' });
  if (why.length) throw new Error(`refusing to run hardware-in-the-loop tests:\n  - ${why.join('\n  - ')}`);
  return info!;
}

/** The ESP32-S3 on the bench, reached over Wi-Fi. */
export function boardTarget(): Target {
  const baseUrl = (process.env.AIRO_BOARD_URL ?? '').replace(/\/$/, '');
  const token = process.env.AIRO_TOKEN ?? '';
  const HANG_MS = 2500;
  const t: Target = {
    name: 'board',
    adapter: 'esp32',
    slackMs: Number(process.env.AIRO_HIL_SLACK_MS ?? 150),
    async open(c, opts = {}) {
      if (!baseUrl || !token) throw new Error('set AIRO_BOARD_URL (e.g. http://airo-desk-001.local) and AIRO_TOKEN');
      await assertBenchSafe(appClient(baseUrl, 'mobile_app', token));
      const desk = new LiveRig(baseUrl.replace(/^http/, 'ws'), c, opts.profile, 'bench');
      const bridge = desk.bridge as PinBridge;
      let hungUntil = 0;
      try {
        await waitFor(() => bridge.state === 'open' && bridge.bench !== null, 8000, 'bench link');
        // Every scenario starts from default config; Config lives in RAM on the board.
        const { app, events } = await connectApp(baseUrl, token, desk, {
          patch: { presets_require_height_feedback: false, ...opts.config },
        });
        return {
          target: t,
          baseUrl,
          desk,
          app,
          events,
          crash: async () => bridge.control({ t: 'halt' }),
          hang: async () => {
            hungUntil = Date.now() + HANG_MS;
            bridge.control({ t: 'hang', ms: HANG_MS });
          },
          resume: async () => {
            const left = hungUntil - Date.now();
            if (left > 0) await sleep(left + 200);
          },
          close: async () => {
            app.close();
            if (hungUntil > Date.now()) await sleep(hungUntil - Date.now() + 200);
            desk.stop(); // closing /bench reboots the core into FAULT until the next hello
            await sleep(200);
          },
        };
      } catch (e) {
        desk.stop();
        throw e;
      }
    },
  };
  return t;
}

export const targets = {
  laptop: () => laptopTarget('laptop'),
  'laptop-esp32': () => laptopTarget('laptop-esp32'),
  board: boardTarget,
};

export function shortProfile(rangeMm: number): DeskProfile {
  const p = structuredClone(defaultProfile);
  p.mechanical.maxHeightMm.value = p.mechanical.minHeightMm.value + rangeMm;
  return p;
}
