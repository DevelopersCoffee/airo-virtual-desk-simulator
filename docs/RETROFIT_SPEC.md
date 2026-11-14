# Airo Smart Desk Retrofit: Circuit Architecture and Software Protocol Specification

| | |
|---|---|
| Status | Draft v0.1, for review before buying parts or touching any wiring |
| Date | 2026-11-14 |
| Scope | Add local Wi-Fi app control to an existing desk while keeping the original motor, controller and five-button keypad |
| Out of scope | Replacing the motor, gearbox or motor driver; cloud services; changes to the controller's firmware |

## 0. How to read this document

Everything about **this particular desk's** keypad interface is unknown until it is measured. The document therefore:

1. Defines a safe procedure to find out which interface the keypad uses (section 4).
2. Specifies a circuit for each possible outcome (section 5).
3. Specifies one firmware architecture and one local API that work the same way whichever circuit is used (sections 7 and 8).

Facts are tagged as follows:

- **[VERIFIED-DESK]**: measured on the user's desk. *Nothing in this draft has this tag yet.*
- **[COMMUNITY]**: reported by public reverse-engineering projects for other Jiecang-based desks. Plausible, not confirmed for this desk.
- **[ASSUMPTION]**: a design assumption that must be checked.
- **[UNKNOWN]**: listed in Open Questions (section 12).

---

## 1. Goals and non-goals

### Goals

| # | Goal |
|---|---|
| G1 | Original motor, controller and five-button keypad stay in service, unmodified. |
| G2 | The Airo app can trigger the five existing functions over local Wi-Fi. No new desk functions are invented. |
| G3 | Physical keypad and app coexist. The physical keypad always works, and always wins a conflict. |
| G4 | Removing power from, or unplugging, the Airo module returns the desk to exactly its factory behaviour. |
| G5 | No two active drivers are ever connected to the same signal line. |
| G6 | No cloud dependency. The module works on a LAN with no internet. |
| G7 | No wires are cut. All connections are made through a plug-in pass-through adapter. |

### Non-goals (v1)

- Absolute height control if the controller does not report height (section 9).
- Re-implementing the controller's collision detection, end-stops or motor current limits. These remain the controller's job and are never bypassed.
- Exposing reset/recalibration in the app before its exact behaviour is verified.

---

## 2. System architecture

```
                 ┌──────────────────────┐
                 │  Airo app (phone)    │
                 │  local Wi-Fi only    │
                 └──────────┬───────────┘
                            │ HTTP + WebSocket (LAN)
┌──────────────┐   ┌────────┴────────────────────────────────────────┐   ┌──────────────────┐   ┌────────┐
│ Original     │   │ Airo interface module                           │   │ Original desk    │   │Original│
│ keypad       ├───┤  keypad-side jack     ESP32-WROOM-32            ├───┤ controller       ├───┤ motor  │
│ (5 buttons)  │   │  ├─ sense taps (high-Z, isolated) ──► ESP32 in  │   │ (Jiecang-style,  │   │        │
└──────────────┘   │  ├─ selector / bypass (fail-safe = keypad)      │   │  [ASSUMPTION])   │   └────────┘
                   │  └─ injector (opto / photoMOS / open-drain)     │   └──────────────────┘
                   │  controller-side plug                           │
                   │  USB 5 V power, independent of the desk         │
                   └─────────────────────────────────────────────────┘
```

Physical form: the keypad's existing cable is unplugged from the controller and plugged into the module's **keypad-side jack**. A short patch lead goes from the module's **controller-side plug** into the controller's original keypad socket. If the controller has a second, unused port (section 4.1, step 3) the module may use that instead and the keypad is not touched at all.

**Ultimate bypass:** unplug the module and plug the keypad back into the controller. This must always remain possible, so the module never uses a connector the keypad cannot plug back into.

---

## 3. The five keypad functions

| Physical button | App action | API action id | Notes |
|---|---|---|---|
| UP arrow | Raise desk | `up` | Hold-to-move expected **[ASSUMPTION]** |
| DOWN arrow | Lower desk | `down` | Hold-to-move expected **[ASSUMPTION]** |
| Circular arrow | Reset / recalibration | `reset` | **[UNKNOWN]** exact behaviour. Hidden in the app in v1 (section 8.6). |
| Sitting icon | Sitting preset | `preset` + `sit` | One-touch or hold-to-run **[UNKNOWN]** |
| Standing icon | Standing preset | `preset` + `stand` | One-touch or hold-to-run **[UNKNOWN]** |
| (none) | Stop | `stop` | App-only concept. Implemented by the method in section 6.3. |

The app reproduces these functions. It does not add memory-set, unit change, limit-set or calibration commands, even where the controller may support them.

---

## 4. Discovery: finding out which interface the keypad uses

### 4.1 Safety rules for discovery

1. **Desk empty, nothing under it.** Clear the area under and around the desktop before any powered test.
2. **No cutting or soldering on the original cable.** Use a pass-through breakout (for example an RJ45 or RJ12 female-to-female coupler with screw terminals, or a keystone breakout board) so every line is reachable on a terminal.
3. **Unpowered first.** Do continuity and resistance tests with the controller unplugged from mains.
4. **Powered measurements are passive only.** Measure with a multimeter or a logic analyzer through a series resistor (10 kΩ) on each probe line. Never connect an ESP32 GPIO directly to a keypad line.
5. **Never short a line to ground or to another line to "see what happens"** until section 4.3 has identified that line as a switch input.
6. **Hand on the mains switch** during the first powered test of anything that can cause motion.

### 4.2 Procedure

| Step | Action | Tools | What it tells us |
|---|---|---|---|
| 1 | Photograph the controller label and both ends of the keypad cable. Record model number (for example `JCB35N2`, `JCB36N2`, `JCHT35…`), connector type and number of contacts. | Phone camera | Model lookup, RJ45 (8 pins) vs RJ12 (6 pins) vs proprietary. |
| 2 | Photograph the keypad PCB if it can be opened by removing screws only (no clips forced). Look for a microcontroller chip vs only tactile switches and resistors. | Screwdriver | A keypad with no chip cannot speak a digital protocol. |
| 3 | Check whether the controller has a second unused port (often labelled for a Bluetooth/app dongle). | Eyes | If yes, Case B0 (section 5.4) may avoid touching the keypad path. |
| 4 | **Unpowered:** with the keypad plugged into the breakout but not into the controller, measure resistance between every pair of pins with each button released, then with each button held in turn. | Multimeter | Simple switches show a near-0 Ω change between two pins per button. A resistor ladder shows different resistances on one pin pair. No change at all suggests a chip-based keypad. |
| 5 | **Powered, keypad connected, no button:** measure DC voltage of each pin to GND (GND identified in step 4 or by the controller label). | Multimeter | Idle 5 V on several pins = pulled-up inputs **[COMMUNITY]**. A pin around 4–5 V that flickers = possible UART line. |
| 6 | **Powered, passive capture:** attach a logic analyzer (8-channel, 24 MHz class, with PulseView/sigrok) through 10 kΩ series resistors to every non-power pin. Capture 10 s idle, then each button pressed for 1 s and released. | Logic analyzer | Distinguishes all cases in 4.3 definitively. |
| 7 | If a line shows serial traffic, decode it as UART 9600 8N1 first **[COMMUNITY]**, then try other common rates. Save captures to the repo under `hardware/captures/`. | PulseView UART decoder | Frame structure, command bytes, height reports. |

### 4.3 Decision tree

```
Does pressing a button change resistance between two pins (unpowered)?
├── YES, each button shorts its own pin to GND (or to a common) ────────► Case A1: plain switches (5.2)
├── YES, buttons combine onto 2–4 shared lines (binary code)  ─────────► Case A2: encoded switch lines (5.2.3)
├── YES, one pin pair, different resistance per button  ──────────────► Case A3: resistor ladder (5.2.4)
└── NO change ─► Powered capture shows bytes on a line when a button is pressed?
                 ├── YES, and no other line changes  ───────────────────► Case B: digital protocol (5.3)
                 └── YES, and button lines also change  ────────────────► Case C: hybrid (lines for buttons,
                                                                          serial for display/height) (5.2.3 + 5.3.2)
```

Community reports for Jiecang controllers describe **Case C** on handset ports: buttons appear as encoded lines `HS0`–`HS3`, pulled up to 5 V and pulled low by the keypad, while separate `DTX`/`HTX` lines carry framed serial data for display handsets **[COMMUNITY]**. A five-button keypad with no display may use only the button lines **[ASSUMPTION]**.

### 4.4 Exit criteria

Discovery is complete when the following table is filled in and committed to this document with the **[VERIFIED-DESK]** tag:

| Item | Value |
|---|---|
| Controller model | |
| Connector type and pin count | |
| Pin map (function per pin) | |
| Supply voltage on the connector and idle voltage on each signal pin | |
| Interface case (A1 / A2 / A3 / B / C) | |
| Line state or frame for each of the five buttons | |
| Does the controller report height? On which line, in what format? | |
| Preset behaviour: one-touch or hold-to-run | |
| What the circular-arrow button does on short press and on long press | |
| What stops a preset movement in progress | |

---

## 5. Circuit architecture

### 5.1 Rules common to every case

| Rule | Detail |
|---|---|
| R1. Independent power | The ESP32 is powered from its own USB 5 V supply. The module does not draw from the controller's keypad 5 V unless discovery shows ample spare current. **[ASSUMPTION]** Community notes mention around 300 mA available on Jiecang handset ports, which is not enough margin for ESP32 Wi-Fi peaks. |
| R2. Galvanic isolation preferred | Where the interface allows (Case A), the desk side and ESP32 side share no ground: all coupling is through optocouplers or photoMOS relays. Where it does not (Case B UART), grounds are joined at one point only. |
| R3. No 5 V on ESP32 pins | ESP32 GPIO absolute maximum is about 3.6 V. Every desk line reaches the ESP32 only through an optocoupler, a 5 V-tolerant buffer, or a resistor divider. |
| R4. One driver per line | A desk line is driven by at most one active source at any moment. Open-drain "pull-low only" sources in parallel are allowed only where the controller reads each line independently (Case A1). Everywhere else a selector chooses exactly one source. |
| R5. Fail-safe default = keypad | Every selector's **unpowered** state connects the original keypad straight through to the controller. Relays use normally-closed contacts for the keypad path. |
| R6. Physical bypass switch | A panel toggle `MANUAL ONLY` forces every selector to the keypad path and cuts injector outputs, independent of ESP32 firmware. |
| R7. Hardware watchdog | Selector relays and injectors are enabled only while the ESP32 toggles a `HEARTBEAT` pin. A retriggerable monostable (for example 74HC123, or a small dedicated supervisor) drops the enable within ~200 ms of the toggling stopping. |
| R8. Sense taps are high impedance | Every keypad line the ESP32 watches is sensed through ≥ 10 kΩ (or an opto LED with series resistor sized for ≤ 1 mA) so sensing cannot change what the controller sees. |

### 5.2 Case A: keypad is plain switches

#### 5.2.1 Case A1, one line per button

Each button connects its line to GND (or to a common). The controller has a pull-up on each line **[ASSUMPTION]**.

```
 Controller side                                     Keypad side
 line_UP ──────────────┬───────────────────────────── line_UP ──┤ UP button ├── GND
   (pulled up to 5 V)  │
                       │ photoMOS output (normally open)
                       ├──┤ AQY212 / TLP222A class ├── GND_desk
                       │
                       └── 22 kΩ ── PC817 LED ── GND_desk   (sense: ESP32 sees line low)
                                       │
                                       └► phototransistor ─► ESP32 GPIO (3.3 V pull-up)
 ESP32 GPIO ── 330 Ω ── photoMOS LED ── ENABLE (from watchdog + MANUAL ONLY switch)
```

- The photoMOS output sits **in parallel** with the physical button. Both can only pull the line low. Neither drives the line high, so they never fight (R4 allows this).
- Repeat for each of the five lines: five injectors, five sense channels.
- Why photoMOS over a plain phototransistor optocoupler: it switches in both directions, has very low leakage, and does not care which side is positive. A PC817 output also works if the polarity is confirmed.
- Why not a mechanical relay per button: slower, audible, wears out. A relay is acceptable for a first bench prototype.
- **Conflict rule:** if the sense channel shows a line low while the ESP32 is not driving it, a physical button is pressed. Firmware releases all its own injectors (section 6.2).

#### 5.2.2 Pressing two lines at once

In Case A1, if the user presses UP while the app is holding DOWN, the controller sees both pressed. Its response is controller-defined **[UNKNOWN]**. Firmware must release within 20 ms of seeing a manual press (section 6.2), and the bench test plan must check what the controller does with two lines low.

#### 5.2.3 Case A2 and Case C button lines, encoded lines

Community reports describe Jiecang handsets that encode buttons as a binary number on lines `HS0`–`HS3`, each pulled to 5 V by the controller and pulled low by the keypad. Reported codes for one controller (`HS2 HS1 HS0` inverted): Down = 1, Up = 2, Preset 1 = 3, Preset 2 = 4 … **[COMMUNITY]**.

This changes the safety analysis completely. Parallel pull-downs **are not safe** here: if the keypad pulls `HS0` (Down = 1) and the app pulls `HS1` (Up = 2), the controller reads 3, which is **Preset 1**, a movement nobody requested.

Therefore in Case A2/C the module uses a **selector**, not a parallel injector:

```
                    ┌──────── K1 (4-pole relay or 4× NC photoMOS) ────────┐
 keypad HS0..HS3 ───┤ NC  (de-energised: keypad → controller)              │
                    │ COM ──────────────────────────────────────────────── ├──► controller HS0..HS3
 injectors HS0..3 ──┤ NO  (energised: ESP32 injectors → controller)        │
                    └──────────────────────────────────────────────────────┘
 keypad HS0..HS3 ── 10 kΩ ── sense (opto or 5 V-tolerant buffer) ──► ESP32   (always watched, both positions)
```

- K1 energises only for the duration of an app command, then drops back. While K1 is energised, the keypad lines are still sensed. Any manual press makes the ESP32 drop K1 and the keypad takes over (section 6.2).
- While K1 is energised, the controller-side lines need their pull-ups. They are provided by the controller itself **[ASSUMPTION]**; confirm in discovery that the lines idle high with the keypad disconnected.
- Candidate parts: a 4PDT signal relay (for example Omron G6K/G5V class, or two DPDT), or normally-closed photoMOS (for example CPC1117N / AQY414 class) paired with normally-open photoMOS on the injector side. Analog switch ICs (74HC4066, CD4053) are **not** used as the fail-safe element because their unpowered state is not guaranteed to connect the keypad.
- If the encoding needs more than four lines, K1 scales accordingly.

#### 5.2.4 Case A3, resistor ladder

One sense line, with each button selecting a different resistance to GND. The module reproduces each button's resistance with a photoMOS switching a fixed resistor (measured in discovery, 1 % parts) onto the line, behind a selector as in 5.2.3. A digital potentiometer is not used because its unpowered state is undefined.

### 5.3 Case B: keypad speaks a digital protocol

#### 5.3.1 Bus picture

Community reports for framed Jiecang handset ports: 9600 baud 8N1, lines pulled up to 5 V by the controller and pulled low to signal, `DTX` (controller → keypad) and `HTX` (keypad → controller) **[COMMUNITY]**.

- `DTX` has one driver (the controller). The ESP32 may **listen** to it through a level-shifting receiver with no risk of contention.
- `HTX` has one driver (the keypad). If the ESP32 also transmits on `HTX`, there are two drivers on one line, which is forbidden by R4. Even if both are open-drain, overlapping frames corrupt each other and a corrupted frame could be misread as a different command.

#### 5.3.2 Chosen design: man-in-the-middle on HTX with a fail-safe bypass relay

```
 keypad HTX ──┬──────────────── K2 NC ─────────────────┐
              │                                        ├──► controller HTX
              └─► RX buffer ──► ESP32 UART2 RX          │
                  ESP32 UART2 TX ──► open-drain buffer ─┴ K2 NO
                                     (74LVC1G07 or BSS138, pull-up from controller side)

 controller DTX ──────────────────────────────────────────────────► keypad DTX   (never broken)
              └─► RX buffer ──► ESP32 UART1 RX                                   (listen only)

 RX buffer: 10 kΩ / 20 kΩ divider into a Schmitt input, or 74LVC1G17 powered at 3.3 V with 5 V-tolerant input.
```

- **K2 de-energised (default, power loss, watchdog trip, MANUAL ONLY):** keypad `HTX` connects straight to controller `HTX`. Factory behaviour.
- **K2 energised (app mode):** the keypad's `HTX` goes only to the ESP32. The ESP32 forwards every keypad frame unchanged to the controller within one frame time, and inserts app frames only in gaps when no keypad frame is in progress. The ESP32 is the single driver of controller `HTX`.
- **Manual priority:** any keypad frame that is a movement or stop command causes the ESP32 to abandon its current app command, forward the keypad frame, and drop K2 after the line has been idle for 50 ms.
- **Forwarding is byte-transparent.** The ESP32 never rewrites keypad frames. Unknown frames are forwarded as-is.
- **Frame integrity:** the ESP32 transmits only frames whose checksum it has computed and which match a command recorded from the real keypad in discovery (section 9.3).

#### 5.3.3 Alternative considered: shared open-drain HTX with collision avoidance

Rejected for v1. It relies on the keypad never starting a frame while the ESP32 is mid-frame, which the ESP32 cannot guarantee, and violates G5.

### 5.4 Case B0: a spare controller port

Some Jiecang controllers expose a second port intended for a Bluetooth/app dongle (reported as RJ12 on some models: pins GND, DTX, VCC 5 V, HTX, with the outer pins unused) **[COMMUNITY]**. If this desk has one and it accepts framed commands:

- The keypad path is not touched at all. Contention only exists between the controller's two ports, which the controller itself arbitrates.
- The module connects with an RJ12 lead, through the receiver/open-drain transmitter of 5.3.2, with no K2 relay needed.
- This becomes the **preferred** option if discovery confirms it works. It must still be bench-tested for what happens when keypad and port send conflicting commands.

### 5.5 Module block list (all cases)

| Block | Prototype part (example) | Needed in |
|---|---|---|
| MCU | ESP32-WROOM-32 dev board | All |
| Power | USB 5 V adapter, 1 A or more | All |
| Pass-through breakout | RJ45 or RJ12 coupler with screw-terminal breakout (type from discovery step 1) | All except B0 |
| Injector, per line | AQY212 / TLP222A photoMOS (NO) + 330 Ω | A1, A2, A3, C |
| Sense, per line | PC817 + 22 kΩ, or 74LVC1G17 behind a divider | All |
| Selector | 4-pole signal relay with NC keypad path + ULN2003 or MOSFET coil driver + flyback diode | A2, A3, C (K1); B (K2, 1 pole) |
| UART receive | 10 k / 20 k divider + 74LVC1G17 | B, B0, C (height) |
| UART transmit | 74LVC1G07 open-drain or BSS138 | B, B0 |
| Watchdog | 74HC123 retriggerable monostable gating relay coil and injector enables | All |
| Bypass switch | SPST panel toggle `MANUAL ONLY` | All |
| Status LED | RGB LED: green = keypad only, blue = app path armed, red = fault | All |
| Bench tools | Multimeter, 8-channel logic analyzer, 10 kΩ probe resistors | Discovery |

No motor, gearbox or motor driver is required in any case.

---

## 6. Fail-safe behaviour

### 6.1 Fault table

| Condition | Hardware response | Firmware response | Desk result |
|---|---|---|---|
| Module unplugged | n/a | n/a | Keypad plugged back into controller. Factory behaviour. |
| ESP32 unpowered / USB removed | All relays and injectors de-energise (R5) | n/a | Keypad path only. |
| `MANUAL ONLY` switch on | Enables cut in hardware (R6) | Reports `bypass: true`, rejects commands with `409 bypass_active` | Keypad path only. |
| ESP32 crash or hang | Watchdog drops enables within ~200 ms (R7) | n/a | Any app movement stops; keypad path only. |
| Wi-Fi drop or app closed during hold-to-move | none | Hold lease expires (6.3), injectors released | Movement stops within the lease time. |
| Physical button pressed during app command | none | Release all injectors / drop selector within 20 ms; enter `MANUAL_ACTIVE` | Keypad wins. |
| App command exceeds max duration | none | Release after `max_hold_ms` (default 30 s) | Movement stops. |
| Controller reports error or limit (Case B/C) | none | Release, surface `desk_error` event | Controller's own protection remains in charge. |
| Brown-out / reboot | Relays de-energised during boot | On boot: never drive any line until 2 s of idle keypad sense and a successful self-test | No movement on power-up. |

### 6.2 Arbitration and priority

Priority, highest first:

1. Hardware bypass and watchdog (outside firmware).
2. Physical keypad activity.
3. Firmware safety timers (lease expiry, max duration).
4. App `stop`.
5. App movement commands.

Rules:

- The module never begins an app command while any keypad line (or keypad frame) shows activity, or within 300 ms after it ends.
- Only one app command is active at a time. A new command first releases the previous one, waits 50 ms, then applies the new one.
- `up` and `down` are never driven at the same time.
- After a manual press, app commands are rejected with `409 manual_active` until the keypad has been idle for 1 s.

### 6.3 Movement semantics

**Hold-to-move (`up`, `down`):** the app holds a **lease**. It sends `hold` with a `lease_ms` (default 400 ms, maximum 1000 ms) and must renew it every ≤ 150 ms while the user's finger is down. On release, the app sends `release`. If no renewal arrives, the lease expires and the module releases the line. This makes "Wi-Fi dropped while moving" self-stopping.

**Presets (`sit`, `stand`):** behaviour depends on the controller **[UNKNOWN]**:

- If the controller is *one-touch*, the module sends a tap (line low for `tap_ms`, default 150 ms, or one frame) and the controller completes the move on its own.
- If the controller is *hold-to-run*, presets use the same lease mechanism as `up`/`down`.

**Stop:** the app's `stop` always releases every injector immediately. If a one-touch preset is in progress, the controller must also be told to stop. The method depends on discovery:

| Case | Stop method | Status |
|---|---|---|
| A / C | Tap the direction button opposite to travel for `tap_ms` (many controllers stop a preset on any key press) | **[ASSUMPTION]**; verify which key stops a preset without starting a new movement |
| B / B0 | Send the controller's stop frame (reported as command `0x2B` on some Jiecang controllers) | **[COMMUNITY]**; verify |

Until the stop method is verified on this desk, the app shows a warning that **STOP only releases app control** and the physical keypad must be used to interrupt a preset.

---

## 7. Firmware architecture (ESP32)

Target: ESP-IDF or Arduino-ESP32. The structure is identical either way.

```
┌────────────────────────────── Airo firmware ───────────────────────────────┐
│ Api (HTTP + WebSocket, mDNS)                                                │
│        │ commands                         ▲ state/events                    │
│        ▼                                  │                                 │
│ CommandArbiter  ── state machine (7.2), leases, priority (6.2)              │
│        │                                  ▲                                 │
│        ▼                                  │                                 │
│ KeypadDriver (interface)          KeypadSense (interface)    HeightSource   │
│   ├─ SwitchDriver   (A1)            ├─ LineSense               ├─ None      │
│   ├─ EncodedDriver  (A2/C)          └─ FrameSense              └─ FrameHeight│
│   ├─ LadderDriver   (A3)                                                    │
│   └─ FrameDriver    (B/B0)                                                  │
│ Safety: heartbeat pin, boot self-test, bypass input                         │
│ Config: Wi-Fi creds, API token, interface case, timings (NVS)               │
└─────────────────────────────────────────────────────────────────────────────┘
```

The interface case is a build-time or config choice recorded in NVS. The API and arbiter do not change between cases.

### 7.1 Driver interface

```cpp
enum class Action { Up, Down, PresetSit, PresetStand, Reset, Stop };

struct KeypadDriver {
  virtual bool begin() = 0;                 // self-test; false = refuse to arm
  virtual void press(Action a) = 0;         // start asserting the action
  virtual void release() = 0;               // release everything, idempotent
  virtual void tap(Action a, uint16_t ms) = 0;
};

struct KeypadSense {
  virtual bool manualActive() = 0;          // any physical key / keypad frame right now
  virtual uint32_t lastManualMs() = 0;
};

struct HeightSource {
  virtual bool available() = 0;
  virtual std::optional<uint16_t> heightMm() = 0;
  virtual uint32_t ageMs() = 0;
};
```

### 7.2 State machine

```
            boot ─► SELF_TEST ──fail──► FAULT
                       │ ok
                       ▼
 ┌──────────────────► IDLE ◄──────────────────────────────┐
 │                     │ app command & keypad idle ≥300 ms │
 │                     ▼                                    │
 │                 APP_ACTIVE ── lease expiry / release / stop / max duration ──┘
 │                     │ keypad activity
 │                     ▼
 └── keypad idle 1 s ─ MANUAL_ACTIVE
 BYPASS: entered from any state while MANUAL ONLY switch is on; exits to IDLE when switched off.
 FAULT: requires power cycle or authenticated `POST /api/v1/selftest`.
```

### 7.3 Timing constants (defaults, all configurable)

| Name | Default | Purpose |
|---|---|---|
| `manual_release_ms` | 20 | Max time to release after seeing a manual press |
| `manual_quiet_ms` | 1000 | Keypad idle time before app commands are accepted again |
| `pre_command_quiet_ms` | 300 | Keypad idle required before starting an app command |
| `lease_default_ms` / `lease_max_ms` | 400 / 1000 | Hold-to-move lease |
| `tap_ms` | 150 | Simulated short press |
| `max_hold_ms` | 30000 | Absolute cap on any app movement |
| `watchdog_period_ms` | 50 | Heartbeat toggle period (hardware times out at ~200 ms) |

---

## 8. Local API

### 8.1 Discovery and transport

- mDNS hostname `airo-desk.local`, service `_airo-desk._tcp`, TXT records `api=1`, `fw=<version>`, `id=<short-id>`.
- HTTP/1.1 on port 80, JSON bodies, base path `/api/v1`.
- WebSocket at `/api/v1/ws` for hold-to-move and live state.
- LAN only. The firmware has no cloud client and makes no outbound connections except NTP (optional).

### 8.2 Provisioning and pairing

1. First boot (or after a 10 s press of the module's own setup button): the ESP32 starts a SoftAP `Airo-Desk-XXXX` with a captive page to enter home Wi-Fi credentials.
2. During the same session the module generates a random 128-bit API token and shows it once on the setup page (and as a QR code). The app stores it.
3. Every API request except `GET /api/v1/info` must carry `Authorization: Bearer <token>`. WebSocket clients send the token in the first message.
4. Token reset requires the physical setup button. There is no remote reset.

Plain HTTP on a trusted LAN is accepted for v1 **[ASSUMPTION]**. TLS with a self-signed certificate pinned by the app is a v2 option (open question Q10).

### 8.3 REST endpoints

| Method | Path | Auth | Purpose |
|---|---|---|---|
| GET | `/info` | none | Device id, firmware version, API version, interface case, capabilities |
| GET | `/state` | token | Current state (8.5) |
| POST | `/command` | token | One-shot commands: `tap`, `stop`, presets |
| POST | `/selftest` | token | Re-run self-test, clear `FAULT` |
| GET | `/config` | token | Timings and settings |
| PATCH | `/config` | token | Change timings within safe bounds; rejected while not `IDLE` |

`GET /api/v1/info`:

```json
{
  "device": "airo-desk",
  "id": "a1b2c3",
  "fw": "0.1.0",
  "api": 1,
  "interface_case": "A1",
  "capabilities": {
    "actions": ["up", "down", "preset_sit", "preset_stand", "stop"],
    "reset_exposed": false,
    "height_feedback": false,
    "stop_method_verified": false,
    "preset_mode": "unknown"
  }
}
```

`POST /api/v1/command`:

```json
{ "action": "preset", "preset": "stand", "client_id": "phone-1", "seq": 17 }
```

```json
{ "action": "stop", "client_id": "phone-1", "seq": 18 }
```

Responses: `202 Accepted` with the new state, or an error from 8.7.

### 8.4 WebSocket protocol

Client → module:

```json
{ "type": "auth",    "token": "…" }
{ "type": "hold",    "action": "up", "lease_ms": 400, "seq": 101 }
{ "type": "renew",   "seq": 102 }
{ "type": "release", "seq": 103 }
{ "type": "stop",    "seq": 104 }
```

Module → client:

```json
{ "type": "state", "state": "APP_ACTIVE", "action": "up", "owner": "phone-1", "lease_remaining_ms": 310, "height_mm": null, "bypass": false }
{ "type": "event", "event": "manual_override", "at_ms": 123456 }
{ "type": "event", "event": "lease_expired" }
{ "type": "error", "code": "manual_active", "seq": 101 }
```

Rules:

- Only one client may own an active command. A second client's `hold` is rejected with `busy`, but **any** authenticated client's `stop` is always accepted.
- `renew` is ignored unless it comes from the owner. If the owner's socket closes, its lease is released immediately.
- `seq` values are echoed in replies so the app can match acknowledgements.
- The module broadcasts `state` on every change and at 2 Hz while `APP_ACTIVE`.

### 8.5 State object

| Field | Type | Meaning |
|---|---|---|
| `state` | `IDLE` \| `APP_ACTIVE` \| `MANUAL_ACTIVE` \| `BYPASS` \| `FAULT` | Section 7.2 |
| `action` | string \| null | Action being asserted by the app |
| `owner` | string \| null | `client_id` of the owning client |
| `height_mm` | int \| null | Only when `capabilities.height_feedback` is true |
| `height_age_ms` | int \| null | Age of the last height report |
| `bypass` | bool | `MANUAL ONLY` switch position |
| `last_manual_ms` | int | Milliseconds since last physical key |
| `fault` | string \| null | Fault reason |

The app shows `-- cm` and "Height feedback not yet confirmed" whenever `height_mm` is null, matching the current concept screen.

### 8.6 Reset / recalibration

`reset` is not in `capabilities.actions` and the endpoint rejects it with `403 action_not_enabled` until discovery records exactly what the circular-arrow button does (short and long press) and the user enables it in config. On many desks, reset drives the desk to its lowest position, so when enabled it will require a confirmation step in the app and will use hold-to-run, never a tap.

### 8.7 Errors

| HTTP | Code | Meaning |
|---|---|---|
| 400 | `bad_request` | Malformed body or unknown action |
| 401 | `unauthorized` | Missing or wrong token |
| 403 | `action_not_enabled` | Action exists but is disabled (for example `reset`) |
| 409 | `manual_active` | Physical keypad in use or within quiet time |
| 409 | `bypass_active` | `MANUAL ONLY` switch on |
| 409 | `busy` | Another client owns the active command |
| 423 | `fault` | Module in `FAULT`; run self-test |
| 429 | `rate_limited` | More than 20 commands/s from one client |

### 8.8 Relation to the simulator

The Phase 5 "ESP32 firmware sim" in the PRD (`docs/PRD.md`, added with the Phase 1 work) should implement this same API against the simulated controller, so the Airo app can be developed against the simulator first and pointed at real hardware later with no changes.

---

## 9. Digital protocol reference (Case B / B0 / C)

> **Everything in this section is [COMMUNITY] and unverified on this desk.** It summarises public reverse-engineering of other Jiecang-based desks. Different Jiecang controller models, and even different ports on the same controller, use different protocols. Treat it as a hypothesis to test against the captures from section 4, never as a command list to send blind.

### 9.1 Physical layer (reported)

| Item | Reported value |
|---|---|
| UART | 9600 baud, 8 data bits, no parity, 1 stop bit |
| Levels | Lines pulled up to 5 V by the controller, pulled low to signal |
| RJ45 handset port (8 pins), one model | 1 HS3, 2 DTX (controller→handset), 3 GND, 4 HTX (handset→controller), 5 VCC 5 V, 6 HS2, 7 HS1, 8 HS0 |
| RJ12 accessory port (6 pins), one model | 1 NC, 2 GND, 3 DTX, 4 VCC 5 V, 5 HTX, 6 NC |

### 9.2 Frame format (reported)

```
[addr][addr][cmd][len][param × len][checksum][0x7E]
addr     = 0xF1 0xF1 (handset → controller) or 0xF2 0xF2 (controller → handset)
checksum = (cmd + len + Σparams) & 0xFF
```

Example: `F1 F1 05 00 05 7E` = handset → controller, command 0x05, no params, checksum 0x05.

### 9.3 Handset → controller commands (reported)

| Function | Cmd | Frame | Relevance to Airo |
|---|---|---|---|
| Up (one step) | `0x01` | `F1 F1 01 00 01 7E` | `up`, repeated while held |
| Down (one step) | `0x02` | `F1 F1 02 00 02 7E` | `down`, repeated while held |
| Move to memory 1 | `0x05` | `F1 F1 05 00 05 7E` | Candidate for `preset_sit` |
| Move to memory 2 | `0x06` | `F1 F1 06 00 06 7E` | Candidate for `preset_stand` |
| Move to memory 3 / 4 | `0x27` / `0x28` | `F1 F1 27 00 27 7E` / `F1 F1 28 00 28 7E` | Not used |
| Stop | `0x2B` | `F1 F1 2B 00 2B 7E` | Candidate for `stop` (one source only) |
| Wake / poll | `0x29` | `F1 F1 29 00 29 7E` | Possibly needed to get height reports |
| Request settings / heights | `0x07` | `F1 F1 07 00 07 7E` | Sources disagree on what it returns |
| Move to absolute height (mm) | `0x1B` | `F1 F1 1B 02 HI LO CK 7E` | Not used in v1 (not a keypad function) |
| Save memory 1 / 2 / 3 / 4 | `0x03` / `0x04` / `0x25` / `0x26` | | **Never sent by Airo** (overwrites user presets) |
| Calibrate / limits / units / sensitivity | `0x91`, `0x20`–`0x23`, `0x0E`, `0x1D` | | **Never sent by Airo** |

The mapping of the keypad's sitting and standing icons to memory 1 and memory 2 is an **[ASSUMPTION]**: Airo replays whatever frame the real keypad sends for each icon, as recorded in discovery, rather than relying on this table.

### 9.4 Controller → handset reports (reported)

| Report | Cmd | Format | Example |
|---|---|---|---|
| Current height | `0x01` | `F2 F2 01 03 HI LO xx CK 7E`, height = HI×256+LO in mm (or tenths of an inch if the desk is set to inches); third param meaning unknown | `F2 F2 01 03 05 06 07 14 7E` → 1286 mm |
| Memory 1–4 heights | `0x25`–`0x28` | 2 params, HI LO | |
| Physical limits | `0x07` | 4 params: upper HI LO, lower HI LO | `05 14 02 8A` → 1300 / 650 mm |

### 9.5 Known pitfalls (reported)

- Some controllers sometimes fall into a second, unframed serial format after idle time. Its trigger is not known.
- Saving a memory is not acknowledged by the controller.
- Height reports repeat many times while the display is awake. A single report is not a final value.
- On encoded-line handsets, pulling two button lines together (`HS0` + `HS3` on one model) wakes the controller to send height reports. This is a possible height-feedback route even in Case C.
- Sources disagree on: which port carries the framed protocol on a given model, what `0x07` returns, and whether `0x2B` (stop) exists.

### 9.6 About the "JIECANG_0xFF00" label

An earlier dashboard screenshot labelled the desk profile `JIECANG_0xFF00`. No public source found during this research uses that identifier for a Jiecang protocol or model. Its origin is **[UNKNOWN]** (Q2); it is not used anywhere in this spec.

### Sources

- phord/Jarvis, README and discussion #4: https://github.com/phord/Jarvis
- DeskUp Pro RJ12 hex codes: https://smarthomeguys.github.io/DeskUp-Pro-Controller-RJ12/docs/diy/desk-hex-codes.html
- Home Assistant community, "Desky Standing Desk (ESPHome) [Works with Desky, Uplift, Jiecang, Assmann & others]": https://community.home-assistant.io/t/desky-standing-desk-esphome-works-with-desky-uplift-jiecang-assmann-others/383790
- Rocka84 `jiecang_desk_controller` ESPHome component (referenced via the DeskUp page; its own README was not readable during research): https://github.com/Rocka84/esphome_components

---

## 10. Bring-up and test plan

Each stage must pass before the next starts. Record results in this document.

| Stage | What | Pass criteria |
|---|---|---|
| T0 | Discovery (section 4), passive only | Section 4.4 table filled in |
| T1 | Module on the bench, **not connected to the desk**. Drive injectors into a dummy load (5 V + 10 kΩ pull-ups). | Correct line pulled low for each action; watchdog drops enables within 200 ms when firmware is halted; `MANUAL ONLY` cuts all outputs; unpowered selectors connect keypad side to controller side (continuity test). |
| T2 | Module inline, `MANUAL ONLY` on. | Desk behaves exactly as without the module, all five buttons. |
| T3 | `MANUAL ONLY` off, app idle. | Desk still behaves exactly as without the module. Sense channels report every physical press. |
| T4 | App `up`/`down` hold, desk empty, hand on mains switch. | Movement starts and stops with the app; lease expiry stops movement when Wi-Fi is cut on the phone. |
| T5 | Conflict tests: press physical keys during each app action. | App releases within 20 ms; no unrequested movement (especially Case A2/C preset codes). |
| T6 | Presets and stop. | Presets reach the same height as from the keypad; verified stop method interrupts a preset. |
| T7 | Faults: pull USB during movement, reboot during movement, kill the app. | Movement stops (or never starts); keypad works immediately afterwards. |
| T8 | 1-week soak with normal daily use. | No unexpected movement, no keypad malfunction. |

---

## 11. Cost note

No replacement motor, gearbox or motor driver is needed in any case. The interface parts in section 5.5 are low-cost signal components. The actual bill depends on the case found in discovery (Case A1 needs injectors and sense only; Cases A2/C/B add a selector relay). A logic analyzer is the one tool worth buying before anything else, because it settles the interface question.

---

## 12. Open questions

| # | Question | How to answer |
|---|---|---|
| Q1 | Which controller model is it, and which connector does the keypad use? | Discovery step 1 |
| Q2 | Where did the `JIECANG_0xFF00` label come from, and does it identify anything real? | Ask the user / check the earlier dashboard's source |
| Q3 | Is the keypad plain switches, encoded lines, a resistor ladder, a digital protocol, or hybrid? | Discovery steps 2–7 |
| Q4 | What exactly does the circular-arrow button do on short and long press? | Observe with desk empty; check the desk manual |
| Q5 | Are presets one-touch or hold-to-run? | Press and release a preset button with desk empty |
| Q6 | What stops a preset move in progress without starting another movement? | Bench test T6 |
| Q7 | Does the controller report height on any line we can read, and in what units? | Logic analyzer capture; try the wake behaviour noted in 9.5 |
| Q8 | Does the controller have a spare accessory port (Case B0)? | Discovery step 3 |
| Q9 | How does the controller react to two button lines low at once? | Bench test T5, desk empty |
| Q10 | Is plain HTTP on the home LAN acceptable, or is TLS needed in v1? | User decision |
| Q11 | Should more than one phone be able to control the desk? | User decision; API already supports one owner plus stop-from-anyone |
| Q12 | How much current can the keypad port supply, in case powering the ESP32 from it is ever wanted? | Measure; default remains independent USB power |
| Q13 | Does adding the pass-through cable length affect a digital bus (reflections, timing)? | Check captures with module inline at T2 |
