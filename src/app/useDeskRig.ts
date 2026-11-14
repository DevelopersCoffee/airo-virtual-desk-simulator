import { useEffect, useMemo, useRef, useState } from 'react';
import { BridgeClient, DeskRig, type BridgeState } from '../packages/airo-link';
import type { BoardSnapshot } from '../packages/desk-hardware';
import type { SimulationSnapshot } from '../packages/simulation-engine';

export interface RigView {
  sim: SimulationSnapshot;
  board: BoardSnapshot;
  bridge: BridgeState;
  activePreset: 'sit' | 'stand' | null;
}

function wsUrl(path: string): string {
  const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
  return `${proto}//${location.host}${path}`;
}

/**
 * Owns the desk side of the laptop simulation (keypad, interface board,
 * original controller, mechanism) and connects it to airo-host's /bridge.
 * The loop only reports elapsed wall time; the rig runs whole fixed steps.
 */
export function useDeskRig(uiHz = 15) {
  const rig = useMemo(() => new DeskRig(), []);
  const [bridgeState, setBridgeState] = useState<BridgeState>('closed');
  const view = (bridge: BridgeState): RigView => ({
    sim: rig.sim.snapshot(),
    board: rig.board.snapshot(),
    bridge,
    activePreset: rig.controller.activePreset,
  });
  const [snapshot, setSnapshot] = useState<RigView>(() => view('closed'));
  const latest = useRef(snapshot.sim);
  const bridgeRef = useRef<BridgeState>('closed');

  useEffect(() => {
    const bridge = new BridgeClient(rig, wsUrl('/bridge'));
    bridge.onState = (s) => {
      bridgeRef.current = s;
      setBridgeState(s);
    };
    bridge.start();
    let raf = 0;
    let last = performance.now();
    const loop = (now: number) => {
      const elapsed = Math.min(0.05, (now - last) / 1000);
      last = now;
      rig.advance(elapsed);
      latest.current = rig.sim.snapshot();
      raf = requestAnimationFrame(loop);
    };
    raf = requestAnimationFrame(loop);
    // Keep the rig running at a steady rate even when rAF is throttled, so
    // keypad sensing and heartbeats are not delayed by rendering.
    const tick = setInterval(() => {
      const now = performance.now();
      if (now - last > 20) {
        rig.advance(Math.min(0.05, (now - last) / 1000));
        last = now;
      }
    }, 5);
    const ui = setInterval(() => setSnapshot(view(bridgeRef.current)), 1000 / uiHz);
    return () => {
      cancelAnimationFrame(raf);
      clearInterval(tick);
      clearInterval(ui);
      bridge.stop();
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [rig, uiHz]);

  const getSnapshot = useMemo(() => () => latest.current, []);
  const refresh = () => {
    rig.flush();
    latest.current = rig.sim.snapshot();
    setSnapshot(view(bridgeRef.current));
  };

  return { rig, snapshot: { ...snapshot, bridge: bridgeState }, getSnapshot, refresh };
}
