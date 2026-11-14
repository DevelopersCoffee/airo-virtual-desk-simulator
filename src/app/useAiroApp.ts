import { useEffect, useMemo, useState } from 'react';
import { AiroClient, type AiroEvent, type AiroInfo, type AiroReply, type AiroStatus } from '../packages/airo-link';

export interface LogLine {
  at: number;
  text: string;
  tone: 'ok' | 'warn' | 'err';
}

const TOKEN = (import.meta.env.VITE_AIRO_TOKEN as string | undefined) ?? 'airo-sim-token';

/**
 * The "Airo app" half of the dashboard: a client of the local API exactly
 * like a phone app would be. It never touches the simulator directly.
 */
export function useAiroApp() {
  const client = useMemo(
    () =>
      new AiroClient({
        baseUrl: '',
        wsUrl: `${location.protocol === 'https:' ? 'wss:' : 'ws:'}//${location.host}/api/v1/ws`,
        token: TOKEN,
        source: 'web_dashboard',
      }),
    [],
  );
  const [connected, setConnected] = useState(false);
  const [info, setInfo] = useState<AiroInfo | null>(null);
  const [status, setStatus] = useState<AiroStatus | null>(null);
  const [log, setLog] = useState<LogLine[]>([]);

  const push = (text: string, tone: LogLine['tone']) =>
    setLog((l) => [{ at: Date.now(), text, tone }, ...l].slice(0, 8));

  useEffect(() => {
    let alive = true;
    let retry: ReturnType<typeof setTimeout> | null = null;
    client.onStatus = (s) => setStatus(s);
    client.onEvent = (e: AiroEvent) => push(`${e.event}${e.direction ? ` (${e.direction})` : ''}${e.detail ? `: ${e.detail}` : ''}`, 'warn');
    client.onReply = (r: AiroReply) => {
      if (r.type === 'error' && r.code !== 'not_owner') push(`rejected: ${r.code}`, 'err');
    };
    client.onClose = () => {
      setConnected(false);
      setStatus(null);
      if (alive) retry = setTimeout(connect, 1500);
    };
    const connect = () => {
      client
        .connect()
        .then((i) => {
          setInfo(i);
          setConnected(true);
          push('connected to Airo Core', 'ok');
        })
        .catch(() => undefined);
    };
    connect();
    const infoPoll = setInterval(() => {
      if (client.connected) client.getInfo().then(setInfo).catch(() => undefined);
    }, 2000);
    return () => {
      alive = false;
      if (retry) clearTimeout(retry);
      clearInterval(infoPoll);
      client.onClose = undefined;
      client.close();
    };
  }, [client]);

  return { client, connected, info, status, log };
}
