# Bench test: ESP32-S3 Airo Core on a breadboard (retrofit phase 2)

This is the procedure for the first time the ESP32-S3 arrives: flash it, wire it to a breadboard, and run the same scenario table the laptop end-to-end tests run. It covers retrofit phase 2 in [RETROFIT_SPEC.md](RETROFIT_SPEC.md) section 16 ("ESP32 bench prototype ... driving a simulated keypad circuit, not the desk").

> **Status: written before the hardware arrived. None of this has been run on a board yet.** The firmware builds in CI and the hardware-in-the-loop suite passes against a simulated board (`airo-host --adapter esp32`), but the first real run is the checks below.

## 1. Safety rules

1. **The ESP32-S3 is never connected to the desk, its keypad, its controller or its cables in this phase.** Only to the breadboard and the laptop's USB port.
2. **Power only from USB-C.** No 5 V, 12 V or ground from anything belonging to the desk (spec R1).
3. **No 5 V on any GPIO.** ESP32-S3 pins are 3.3 V and not 5 V tolerant (spec section 2). Everything on the breadboard runs from the board's own 3V3 pin.
4. The firmware is **bench mode only**:
   - every output pin is **high impedance** at boot and stays that way, whatever the app or the test suite asks;
   - outputs are only driven after you type `drive on BENCH-ONLY` at the USB serial console. That cannot be done over Wi-Fi, is not stored, and resets to OFF at every boot;
   - desk mode is not compiled in. `firmware/esp32/src/main.cpp` refuses to build if `AIRO_ALLOW_DESK_MODE` is defined.
5. **Every pin number is a placeholder** (marked UNVERIFIED in `/api/v1/info` and on the console). The keypad interface has not been identified (spec section 5), so the pin map only describes the breadboard.

## 2. What you need

From the dev kit order: ESP32-S3 DevKitC-1 (or compatible ESP32-S3 board with USB-C), USB-C data cable, 830-point breadboard and jumpers, multimeter, 8-channel logic analyzer. Small parts: 6 LEDs, 6 × 330 Ω resistors, 6 push buttons, 1 slide or toggle switch. Optional for case B checks: the 3.3 V USB-to-TTL adapter.

On the laptop: Node 22, CMake and a C++17 compiler (already needed for the simulator), and PlatformIO:

```bash
pip install platformio      # or the PlatformIO extension in VS Code
```

## 3. Flash

```bash
npm run esp32:build          # pio run -d firmware/esp32  (first build downloads ESP-IDF, ~1 GB)
npm run esp32:flash          # plug the board's USB port first
npm run esp32:monitor        # serial console at 115200; Ctrl+C to leave
```

On a DevKitC-1 use the port labelled **UART** (the console is on UART0). If upload fails to connect, hold BOOT, tap RESET, release BOOT and try again.

Expected first lines on the console:

```
AIRO_BOOT fw=0.3.0-sim mode=bench drive=off pins=UNVERIFIED case=A2
No Wi-Fi configured. Type: wifi <ssid> <password>
Airo console. Commands: ...
```

Then, at the console:

```
wifi <your-ssid> <your-password>     -> prints AIRO_IP 192.168.x.y ... and http://airo-desk-001.local
token                                -> prints the API token (random, made at first boot)
info                                 -> JSON: "bench":{"bench_mode":true,"drive_enabled":false,"inputs":"link",...}
pins                                 -> the pin map for the current case, no ERROR lines
```

Check from the laptop: `curl http://airo-desk-001.local/api/v1/info` (or the IP) returns the same JSON. If `.local` does not resolve on your network, use the IP everywhere below.

## 4. Breadboard wiring (placeholders)

Default pins from `firmware/board/src/pin_map.cpp`. Change any of them with `-DAIRO_PIN_<NAME>=<gpio>` in `firmware/esp32/platformio.ini`. The map refuses strapping pins (0, 3, 45, 46), USB (19, 20), flash (26–32), the console UART (43, 44) and the RGB LED (38, 48).

**Outputs** (each: GPIO → 330 Ω → LED anode, LED cathode → GND; active high, LED on = asserted):

| Signal | GPIO | Case | Meaning on the real Interface Board |
|---|---|---|---|
| `heartbeat` | 4 | all | Watchdog toggle (R7), 10 Hz square wave when the core runs |
| `arm` | 5 | all | Selector request (K1 in A2, K2 in B) |
| `inj_up` / `inj_hs0` | 7 | A1 / A2 | UP injector / encoded bit 0 |
| `inj_down` / `inj_hs1` | 15 | A1 / A2 | DOWN injector / encoded bit 1 |
| `inj_sit` / `inj_hs2` | 16 | A1 / A2 | Sitting preset injector / encoded bit 2 |
| `inj_stand` / `inj_hs3` | 17 | A1 / A2 | Standing preset injector / encoded bit 3 |
| `uart_htx_tx` | 18 | B | Frames to controller HTX (through K2 NO) |

There is no reset output: `desk.reset` is never driven.

**Inputs** (each: push button between GPIO and GND; the internal pull-up is on, pressed = asserted):

| Signal | GPIO | Case |
|---|---|---|
| `bypass_state` (MANUAL ONLY) | 6 | all (use the switch) |
| `sense_up` / `sense_hs0` | 8 | A1 / A2 |
| `sense_down` / `sense_hs1` | 9 | A1 / A2 |
| `sense_sit` / `sense_hs2` | 10 | A1 / A2 |
| `sense_stand` / `sense_hs3` | 11 | A1 / A2 |
| `sense_reset` | 12 | A1 (optional) |
| `uart_htx_rx` / `uart_dtx_rx` | 13 / 14 | B (keypad HTX listen, controller DTX listen) |

Case B on the breadboard: connect the USB-to-TTL adapter's **TX** to GPIO 14 (to play controller height frames) or GPIO 13 (keypad frames), its **RX** to GPIO 18, and GND to GND. 3.3 V adapter only.

## 5. Checks

Fill in the result column as you go and commit the table (or paste it into the thread). Stop at the first failure.

| # | Check | How | Pass | Result |
|---|---|---|---|---|
| B0 | Boots in bench mode | Console after reset | `AIRO_BOOT ... mode=bench drive=off`; `pins` has no ERROR | |
| B1 | Outputs high impedance by default | Drive off. `inputs physical` (so the core leaves FAULT without a bench link). Multimeter (V) on each output GPIO to GND, then send `desk.up` (section 6) and look again | The command is `accepted` and `info` shows the output asserted in software, yet no LED lights and every output reads floating (near 0 V, no stiff level) | |
| B2 | Bench drive is opt-in and does not survive a reboot | `drive on BENCH-ONLY`, watch GPIO 4; `reboot`; `info` | Heartbeat LED blinks at about 10 Hz only after the command; after reboot `drive_enabled:false` and LEDs off | |
| B3 | Line mapping, case A1 | `case A1`, `drive on BENCH-ONLY`, `inputs physical`; send `desk.up`, `desk.down`, `desk.sitting`, `desk.standing` (section 6) | Exactly one injector LED per command (UP = 7, DOWN = 15, SIT = 16, STAND = 17), `arm` stays off, never two at once | |
| B4 | Line mapping, case A2 | `case A2`; same commands | `arm` (5) on first; codes on 7/15/16/17 as bits 0–3: DOWN = 1, UP = 2, SIT = 3, STAND = 4 | |
| B5 | Sense inputs and MANUAL ONLY | `inputs physical`; hold each button and read `GET /api/v1/status`; flip the bypass switch | Each button gives `MANUAL_ACTIVE`; the switch gives `BYPASS` and every movement command is refused with `bypass_active` | |
| B6 | Keypad wins, on the pins | Logic analyzer on GPIO 7 (A1 UP injector) and GPIO 9 (DOWN button). Hold `desk.up` from the app, press DOWN | The injector falls within **20 ms** of the button press (spec T5) | |
| B7 | Heartbeat stops when the core stops | Logic analyzer on GPIO 4; run the HIL `core-hang` and `core-crash` scenarios (section 7) with drive on | Toggling stops within one period (50 ms); in the real board the 74HC123 then drops enables within ~200 ms | |
| B8 | Hardware-in-the-loop suite | `drive off`, `inputs link`, then section 7 | All selected scenarios pass | |
| B9 | Power removed | Unplug USB-C | Every LED off. (On the real board: selector falls back to the keypad path, R5) | |

Turn drive off again (`drive off`) before running the HIL suite: it refuses to run while pins are driven unless you pass `--allow-drive`.

## 6. Sending commands by hand

```bash
export AIRO=http://airo-desk-001.local AIRO_TOKEN=<token from the console>
curl -s $AIRO/api/v1/status -H "Authorization: Bearer $AIRO_TOKEN"
curl -s -X POST $AIRO/api/v1/commands -H "Authorization: Bearer $AIRO_TOKEN" \
  -d '{"device_id":"airo-desk-001","command":"desk.up","source":"test","request_id":"b3-'$RANDOM'","lease_ms":1000}'
```

`desk.up`/`desk.down` hold for the lease (≤ 1000 ms) and then release; `desk.sitting`/`desk.standing` are a 150 ms tap. A fresh boot refuses commands for 2 s (`boot_quiet_ms`). After a manual press the API answers `manual_active` for 1 s.

## 7. Hardware-in-the-loop suite

The laptop runs the desk model (keypad, Interface Board, original controller, mechanism) and connects it to the board's `/bench` WebSocket. The board runs the real `Esp32HardwareAdapter`; the bench link carries the logical level of every output and input, so the scenario table from `e2e/scenarios.ts` runs unchanged without any wire to a desk.

```bash
node scripts/hil.mjs --list                                        # scenario ids and cases
node scripts/hil.mjs --board $AIRO --token $AIRO_TOKEN              # everything (about 3 min)
node scripts/hil.mjs --board $AIRO --token $AIRO_TOKEN --only keypad-wins,core-crash --case A2
node scripts/hil.mjs --simulated                                   # same suite, simulated board, no hardware
```

Before any test runs, the suite reads `/api/v1/info` and **refuses** unless the board reports bench mode, `desk_mode_compiled:false`, inputs from the link, and drive off (or `--allow-drive`). Each scenario also re-checks when it opens its bench.

Timing checks get `--slack` ms extra (default 150) for the two Wi-Fi hops; the true release time is B6 on the logic analyzer, not this suite. Scenarios that kill the core use bench messages instead of signals: `core-crash` sends `halt` (the core stops until the next test's hello), `core-hang` sends `hang` (the main loop freezes for 2.5 s, as `SIGSTOP` does on the laptop).

If a scenario fails on the board but passes with `--simulated`, the difference is in the ESP-IDF glue (`firmware/esp32/src`) or timing, not in the adapter logic: both runs execute the same `Esp32HardwareAdapter`.

## 8. What this does not prove

- Nothing about the desk. The pin map, polarity, line count and interface case are guesses until discovery (spec section 5) fills in the 5.4 table.
- Not the Interface Board's fail-safes (R4–R7): the breadboard has no selector relay, photoMOS or 74HC123. Those are bench stage T1 on the real board.
- Not the stop method for a running preset (spec 7.3): `stop_method_verified` stays `false`.
- Not BLE provisioning: Wi-Fi is set at the serial console for now.

After B0–B9 pass, the next step is discovery on the desk with the multimeter and logic analyzer only (spec section 5, passive), still without the ESP32 connected to it.
