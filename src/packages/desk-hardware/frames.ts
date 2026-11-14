/**
 * Jiecang-style UART frames (spec section 13) [COMMUNITY], mirroring
 * firmware/airo-core/src/frames.cpp:
 *   [addr][addr][cmd][len][params][checksum][0x7E], checksum = (cmd + len + params) & 0xFF
 */
import type { KeyButton } from './types';

export const HANDSET = 0xf1;
export const CONTROLLER = 0xf2;
export const END = 0x7e;
export const CMD = { up: 0x01, down: 0x02, memory1: 0x05, memory2: 0x06, stop: 0x2b, height: 0x01 } as const;

export interface Frame {
  addr: number;
  cmd: number;
  params: number[];
}

export function encode(addr: number, cmd: number, params: number[] = []): number[] {
  const sum = (cmd + params.length + params.reduce((a, b) => a + b, 0)) & 0xff;
  return [addr, addr, cmd, params.length, ...params, sum, END];
}

/** Frame the keypad sends for a button; reset has none in v1. */
export function buttonFrame(b: KeyButton): number[] | null {
  switch (b) {
    case 'up':
      return encode(HANDSET, CMD.up);
    case 'down':
      return encode(HANDSET, CMD.down);
    case 'sit':
      return encode(HANDSET, CMD.memory1);
    case 'stand':
      return encode(HANDSET, CMD.memory2);
    case 'reset':
      return null;
  }
}

export function heightReport(mm: number): number[] {
  const v = Math.max(0, Math.min(0xffff, Math.round(mm)));
  return encode(CONTROLLER, CMD.height, [v >> 8, v & 0xff, 0]);
}

export class FrameDecoder {
  private buf: number[] = [];
  bad = 0;

  feed(bytes: readonly number[]): Frame[] {
    this.buf.push(...bytes);
    const out: Frame[] = [];
    let i = 0;
    for (;;) {
      while (i + 1 < this.buf.length && !((this.buf[i] === HANDSET || this.buf[i] === CONTROLLER) && this.buf[i + 1] === this.buf[i])) i++;
      if (i + 4 > this.buf.length) break;
      const len = this.buf[i + 3]!;
      const total = 4 + len + 2;
      if (len > 16) {
        this.bad++;
        i++;
        continue;
      }
      if (i + total > this.buf.length) break;
      const params = this.buf.slice(i + 4, i + 4 + len);
      const sum = (this.buf[i + 2]! + len + params.reduce((a, b) => a + b, 0)) & 0xff;
      if (this.buf[i + 4 + len] !== sum || this.buf[i + 5 + len] !== END) {
        this.bad++;
        i++;
        continue;
      }
      out.push({ addr: this.buf[i]!, cmd: this.buf[i + 2]!, params });
      i += total;
    }
    this.buf = this.buf.slice(i);
    if (this.buf.length > 256) this.buf = [];
    return out;
  }
}
