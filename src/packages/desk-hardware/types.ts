/** The five buttons of the original keypad (spec section 4). */
export type KeyButton = 'up' | 'down' | 'sit' | 'stand' | 'reset';
export const KEY_BUTTONS: readonly KeyButton[] = ['up', 'down', 'sit', 'stand', 'reset'];

/**
 * Interface case from the discovery decision tree (spec 5.3). The real board
 * is built for exactly one; the simulator can model each (conflict C7).
 * - A1: one switch line per button ("electrical")
 * - A2: buttons encoded on shared HS lines ("electrical, encoded")
 * - B: UART frames ("digital")
 */
export type InterfaceCase = 'A1' | 'A2' | 'B';
export const INTERFACE_CASES: readonly InterfaceCase[] = ['A1', 'A2', 'B'];

/** Encoded HS codes reported for one Jiecang controller [COMMUNITY]. Reset is a placeholder. */
export const ENCODED_CODE: Record<KeyButton, number> = { down: 1, up: 2, sit: 3, stand: 4, reset: 8 };

export function decodeEncoded(code: number): KeyButton | null {
  for (const b of Object.keys(ENCODED_CODE) as KeyButton[]) if (ENCODED_CODE[b] === code) return b;
  return null;
}
