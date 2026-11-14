import { buttonFrame } from './frames';
import { ENCODED_CODE, KEY_BUTTONS, type KeyButton } from './types';

/** Repeat period of a held button's frame in the digital case [ASSUMPTION]. */
export const KEYPAD_FRAME_REPEAT_MS = 50;

/**
 * The original five-button keypad. It only knows which buttons a finger is
 * holding; how that reaches the controller depends on the interface case.
 */
export class VirtualKeypad {
  private held = new Set<KeyButton>();
  private lastFrameMs = -Infinity;
  private version = 0;

  press(b: KeyButton): void {
    if (!this.held.has(b)) {
      this.held.add(b);
      this.version++;
      this.lastFrameMs = -Infinity; // a new press sends a frame at once
    }
  }

  release(b: KeyButton): void {
    if (this.held.delete(b)) this.version++;
  }

  releaseAll(): void {
    if (this.held.size) this.version++;
    this.held.clear();
  }

  isPressed(b: KeyButton): boolean {
    return this.held.has(b);
  }

  pressed(): KeyButton[] {
    return KEY_BUTTONS.filter((b) => this.held.has(b));
  }

  /** Changes whenever the set of held buttons changes. */
  get stateVersion(): number {
    return this.version;
  }

  /**
   * Encoded-lines case: each button pulls its own bit pattern low, so two
   * buttons together OR their codes. Down (1) + Up (2) reads as 3, Preset 1:
   * the phantom-preset hazard the selector exists to prevent (spec 6.3).
   */
  encodedCode(): number {
    let code = 0;
    for (const b of this.held) code |= ENCODED_CODE[b];
    return code;
  }

  /** Digital case: the keypad's microcontroller repeats one frame while a button is held. */
  frameDue(nowMs: number): number[] | null {
    const b = this.pressed().find((k) => buttonFrame(k) !== null);
    if (!b) return null;
    if (nowMs - this.lastFrameMs < KEYPAD_FRAME_REPEAT_MS) return null;
    this.lastFrameMs = nowMs;
    return buttonFrame(b);
  }
}
