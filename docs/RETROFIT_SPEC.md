# Airo Smart Desk Controller: Retrofit Architecture and Protocol Specification

| | |
|---|---|
| Status | Draft v0.2, for review before buying interface parts or touching any wiring |
| Date | 2026-11-14 |
| Scope | Add local Wi-Fi app control (and later Alexa) to the existing Green Soul desk while keeping its original motor, controller and five-button keypad |
| Out of scope | Replacing the motor, gearbox or motor driver; changing the controller's firmware; cloud services in the core device |

## 0. How to read this document

Nothing about **this desk's** keypad interface has been measured yet. The document therefore:

1. Defines a safe, passive procedure to find out which interface the keypad uses (section 5).
2. Specifies the interface board circuit for each possible outcome (section 6).
3. Specifies one firmware core, one command schema and one local API that behave identically whichever circuit is used, and identically on a laptop simulation and on the real board (sections 8–11).

Facts are tagged:

- **[VERIFIED-DESK]**: measured on the user's desk. *Nothing has this tag yet.*
- **[COMMUNITY]**: reported by public reverse-engineering of other Jiecang-based desks. Plausible, not confirmed here.
- **[ASSUMPTION]**: a design assumption that must be checked.
- **[UNKNOWN]**: listed in Open Questions (section 17).
- **⚠ CONFLICT**: a point where the refined plan and an earlier safety finding disagree. Each is resolved or left as a decision in section 0.2.

### 0.1 Changes in v0.2

| Area | v0.1 | v0.2 |
|---|---|---|
| Processor | ESP32-WROOM-32 | **ESP32-S3** ("Airo Core") |
| Power and debug | USB 5 V | **USB-C** power, flashing and UART/USB-CDC diagnostics |
| Setup | SoftAP captive page | **Bluetooth LE** provisioning |
| Hardware split | One module | **Airo Core** + separate replaceable **Airo Interface Board** with Ports A (keypad), B (controller), C (Core) |
| Airo-side connectors | RJ45/RJ12 pass-through | **Keyed custom connector** for Port C; Ports A/B see section 3.3 |
| Commands | `up`/`down`/`preset` actions | **Unified command schema** `desk.up`, `desk.down`, `desk.stop`, `desk.sitting`, `desk.standing`, `desk.get_status` |
| Integrations | None | **Alexa later via Home Assistant**; optional MQTT bridge |
| Testing | Bench plan only | **Laptop end-to-end simulation** with `VirtualHardwareAdapter` and `Esp32HardwareAdapter` sharing one firmware core |
| Purchasing | Block list | **Staged purchase list and budget** (buy now vs wait) |
| Roadmap | Not covered | **Six-phase retrofit roadmap** |

### 0.2 Conflicts flagged

| # | Refined plan says | Earlier finding | Resolution in this spec |
|---|---|---|---|
| C1 | Buy **ESP32-DevKitC-32E V4** (₹600) | Architecture now says **ESP32-S3** | ⚠ The DevKitC-32E is a classic ESP32 (WROOM-32E), not an S3. Buy an **ESP32-S3-DevKitC-1** (or equivalent S3 board with USB-C) instead so the prototype matches the product. Price not yet checked. |
| C2 | Avoid RJ45; use a dedicated keyed connector | The ultimate bypass is "unplug Airo, plug the keypad back into the controller" (G4) | ⚠ Airo's **own** connections (Port C, Core ↔ Interface Board) use the keyed custom connector. **Ports A and B must mate with whatever connector the desk already uses** (possibly RJ45 or RJ12), otherwise the keypad cannot be plugged into Airo without cutting its cable, and Airo cannot plug into the controller. The desk's connector is not Airo's choice. |
| C3 | Buy two **RJ45** passive breakouts now | Desk connector type is unknown | ⚠ Look at the keypad plug first (discovery step 1, a 2-minute check needing no tools). Buy RJ45 breakouts only if it is an 8-pin modular plug; RJ12 (6-pin) or a proprietary plug needs a different breakout. |
| C4 | "Presets require confirmed position feedback" | Presets can be executed by the controller's own memory (replaying the keypad's sit/stand button), which needs no feedback in Airo | ⚠ **Decision needed (Q14).** This spec applies the rule to *Airo-computed* target heights (never offered without feedback), and allows *controller-native* presets without feedback, because the controller already performs them from the keypad today. If the user wants the stricter reading, `desk.sitting`/`desk.standing` stay disabled until height feedback exists. |
| C5 | "STOP has priority" | Physical keypad must always win | Consistent once ordered: hardware bypass > physical keypad > STOP > app movement (section 7.2). |
| C6 | "Conflict is rejected or safely arbitrated" | Shared encoded button lines make simultaneous presses dangerous (Down=1 + Up=2 reads as Preset 1) | Kept: on encoded lines, Airo never drives in parallel with the keypad; a fail-safe selector chooses one source (section 6.3). Conflicts resolve to "keypad wins, app releases". |
| C7 | Virtual selector offers "Electrical / digital / bypass" modes | User's own note: these are capabilities, not modes to switch between until the circuit is known | Consistent. The real board is built for exactly one interface case; only bypass vs Airo-path is switchable at runtime. The simulator may model all cases. |
| C8 | Simulation: "same firmware logic" | The existing simulator has a TypeScript `controller` package | ⚠ The TypeScript simulator must **not** grow a second implementation of command arbitration. The C++ firmware core runs on the laptop and drives the 3D simulator through `VirtualHardwareAdapter` (section 12). The TS controller package models the **desk's** controller, not Airo's logic. |
| C9 | ESP32 logic is 3.3 V; logic analyzer and USB-TTL adapter in purchase list | Desk lines are reported to be pulled up to 5 V | Kept: never connect any ESP32 pin or the USB-TTL adapter to desk pins directly. Check the logic analyzer's input range (most 24 MHz clones accept 0–5 V, some do not) before probing. |
| C10 | Generic relay modules | Bypass must work with the ESP32 completely unpowered | Consistent with the user's own warning. Generic relay modules are often active-low and use NO contacts; neither is acceptable for the bypass path. |

---

## 1. Goals and non-goals

### Goals

| # | Goal |
|---|---|
| G1 | Original motor, controller and five-button keypad stay in service, unmodified. |
| G2 | The app (and later Alexa) triggers the five existing functions. No new desk functions are invented. |
| G3 | Physical keypad and app coexist. The keypad always works and always wins a conflict. |
| G4 | Removing power from, or unplugging, Airo returns the desk to its factory behaviour. |
| G5 | No two active drivers are ever connected to the same signal line. |
| G6 | No cloud dependency in the core device. Works on a LAN with no internet. |
| G7 | No wires are cut. All desk connections are plug-in. |
| G8 | Electronics and firmware logic are testable end-to-end on a laptop before touching the desk. |
| G9 | Alexa can be added later without changing interface or motor-side hardware. |

### Non-goals (v1)

- Absolute height control without height feedback.
- Re-implementing collision detection, end-stops or current limits. These remain the desk controller's job.
- Exposing reset/recalibration before its behaviour is verified.
- Matter support. A later integration decision, not a v1 requirement.

---

## 2. Hardware summary

| Interface | Technology | Purpose |
|---|---|---|
| Main processor | ESP32-S3 (Airo Core) | Control logic, local API |
| Power | USB-C, 5 V, independent of the desk | Power and development |
| Wireless | Wi-Fi 2.4 GHz | App connectivity |
| Discovery | mDNS | Automatic discovery on the LAN |
| Commands | REST | `desk.*` commands |
| Live updates | WebSocket | Status and hold-to-move leases |
| Setup | Bluetooth LE | Wi-Fi provisioning and pairing |
| Desk connection | Airo Interface Board (Ports A/B/C) | Existing controller interface |
| Debugging | USB-C (USB-CDC / UART) | Logs, diagnostics, flashing |

ESP32-S3 notes: GPIO is 3.3 V and **not 5 V tolerant**; avoid strapping pins (GPIO0, 3, 45, 46) and, on modules with octal PSRAM, GPIO35–37 for interface signals; native USB uses GPIO19/20.

---

## 3. System architecture

```
 Control inputs:   Physical keypad                Airo app (mobile / laptop)    Alexa (later, via Home Assistant)
                         │                                    │  Wi-Fi (REST/WS), BLE for setup only
                         │                                    ▼
                         │                     ┌──────────────────────────────┐
                         │                     │ Airo Core — ESP32-S3         │
                         │                     │ local API · discovery ·      │
                         │                     │ command arbitration · safety │
                         │                     └──────────────┬───────────────┘
                         │                                    │ Port C (keyed custom connector)
                         ▼                                    ▼
                  ┌──────────────────────────────────────────────────────────────┐
     Port A ─────►│ Airo Interface Board                                         │◄──── Port B
   (keypad plug)  │ sense taps · injector or UART path · fail-safe selector ·    │ (to controller)
                  │ MANUAL ONLY switch · hardware watchdog · test points         │
                  └──────────────────────────────────────────────────────────────┘
                                                      │
                                         Original desk controller ──► Original motor and lifting mechanism
```

### 3.1 Airo Core

ESP32-S3 dev board (prototype), later a custom PCB. Runs the firmware core (section 8). Powered by USB-C.

### 3.2 Airo Interface Board

A separate, replaceable board, so the Core never changes when the interface case is discovered. Ports:

| Port | Connects to | Connector |
|---|---|---|
| A | Original keypad cable | Must accept the keypad's **native plug** (section 3.3) |
| B | Original controller's keypad socket | Short lead with the **native plug** |
| C | Airo Core | Keyed custom connector (proposed below) |

All signals have labelled test points. No pin numbers are assigned to Ports A/B until discovery is complete.

### 3.3 Connector policy (⚠ C2)

- **Airo-to-Airo (Port C):** keyed, polarised, latching wire-to-board connector, for example JST-PH or JST-XH 2.0/2.5 mm, or a Molex Micro-Fit for the production board. Power and signal contacts separated by at least one ground pin. Keyed so it cannot be inserted reversed.
- **Desk side (Ports A and B):** whatever the desk uses. If it is RJ45, Ports A and B are RJ45 even though Airo avoids RJ45 elsewhere. This is what lets the keypad plug into Airo, Airo plug into the controller, and the keypad plug straight back into the controller if Airo is removed.

Proposed Port C pinout (10-pin, keyed; final assignment after discovery):

| Pin | Signal | Direction (Core view) | Notes |
|---|---|---|---|
| 1 | GND | | |
| 2 | +3V3 (Core → board logic) | out | Powers board-side 3.3 V logic only; relay coils from 5 V |
| 3 | +5V (USB-C VBUS) | out | Relay coils / opto LEDs; fused or current-limited |
| 4 | GND | | Separates power from signals |
| 5 | HEARTBEAT | out | Watchdog toggle (section 6.1 R7) |
| 6 | ARM | out | Requests selector to the Airo path; ANDed with watchdog and MANUAL ONLY |
| 7 | BYPASS_STATE | in | Reads MANUAL ONLY switch and actual selector position |
| 8 | DATA_TX / INJ_SDA | out | UART TX (digital case) or I²C to an I/O expander driving injectors (switch cases) |
| 9 | DATA_RX / INJ_SCL | in/out | UART RX from keypad side, or I²C clock |
| 10 | SENSE_IRQ / DTX_RX | in | Keypad activity interrupt, or controller-to-keypad UART listen line |

Using a small I²C expander (for example MCP23017) on the Interface Board keeps Port C at a fixed width whatever the number of desk lines.

---

## 4. The five keypad functions and command names

| Physical button | Command | Notes |
|---|---|---|
| UP arrow | `desk.up` | Hold-to-move with lease **[ASSUMPTION]** |
| DOWN arrow | `desk.down` | Hold-to-move with lease **[ASSUMPTION]** |
| Sitting icon | `desk.sitting` | Controller-native preset; see C4 |
| Standing icon | `desk.standing` | Controller-native preset; see C4 |
| Circular arrow | `desk.reset` (reserved) | **[UNKNOWN]**; not in the v1 command set (section 10.7) |
| (none) | `desk.stop` | App-side concept; method in section 7.3 |
| (none) | `desk.get_status` | Read-only |

---

## 5. Discovery: identifying the keypad interface

### 5.1 Safety rules

1. Desk empty, nothing under it, area clear.
2. No cutting or soldering on the original cable. Use a passive breakout of the right type (⚠ C3).
3. Unpowered first: continuity and resistance tests with the controller unplugged from mains.
4. Powered measurements are passive only: multimeter, or logic analyzer through 10 kΩ series resistors. Never connect an ESP32 pin or the USB-TTL adapter to a desk pin.
5. Never short a line to see what happens until it is identified as a switch input.
6. Hand on the mains switch during the first powered test of anything that can cause motion.

### 5.2 Procedure

| Step | Action | Tools | What it tells us |
|---|---|---|---|
| 1 | Photograph the controller label and both ends of the keypad cable. Record model (for example `JCB35N2`, `JCB36N2`), connector type and contact count. | Phone | Model lookup; RJ45 vs RJ12 vs proprietary; which breakout to buy |
| 2 | If the keypad opens with screws only, photograph the PCB. Microcontroller chip, or only switches and resistors? | Screwdriver | No chip = no digital protocol |
| 3 | Look for a second, unused controller port (often for a Bluetooth dongle). | Eyes | Case B0 possible |
| 4 | **Unpowered:** keypad into breakout, not into controller. Resistance between every pin pair, buttons released, then each held. | Multimeter | Switch per line vs encoded lines vs ladder vs chip |
| 5 | **Powered, no button:** DC voltage of each pin to GND. | Multimeter | 5 V pull-ups **[COMMUNITY]**; supply pin; possible UART idle-high lines |
| 6 | **Powered passive capture:** logic analyzer on all non-power pins through 10 kΩ. 10 s idle, then each button 1 s. | Logic analyzer + PulseView | Settles the case |
| 7 | Decode serial lines as UART 9600 8N1 first **[COMMUNITY]**, then other rates. Commit captures to `hardware/captures/`. | PulseView | Frames, height reports |

### 5.3 Decision tree

```
Does pressing a button change resistance between two pins (unpowered)?
├── YES, each button shorts its own pin to GND/common ─────────────► Case A1: plain switches (6.2)
├── YES, buttons combine onto 2–4 shared lines (binary code) ─────► Case A2: encoded lines (6.3)
├── YES, one pin pair, different resistance per button ───────────► Case A3: resistor ladder (6.4)
└── NO change ─► Powered capture shows bytes when a button is pressed?
                 ├── YES, no other line changes ──────────────────► Case B: digital protocol (6.5)
                 └── YES, and button lines also change ───────────► Case C: hybrid (6.3 + 6.5 listen)
Spare controller port that accepts frames? ───────────────────────► Case B0 (6.6), preferred if it works
```

Community reports for Jiecang handset ports describe **Case C**: buttons as encoded lines `HS0`–`HS3` pulled up to 5 V and pulled low by the keypad, plus `DTX`/`HTX` serial for display handsets **[COMMUNITY]**. A five-button keypad without a display may use only the button lines **[ASSUMPTION]**.

### 5.4 Exit criteria

Discovery is complete when this table is filled in and tagged **[VERIFIED-DESK]**:

| Item | Value |
|---|---|
| Controller model | |
| Connector type and pin count | |
| Pin map | |
| Supply voltage and idle voltage per signal pin | |
| Interface case (A1 / A2 / A3 / B / B0 / C) | |
| Line state or frame per button | |
| Height reported? Line, format, units | |
| Presets one-touch or hold-to-run | |
| Circular-arrow behaviour, short and long press | |
| What stops a preset move in progress | |

---

## 6. Interface Board circuits

### 6.1 Rules common to every case

| Rule | Detail |
|---|---|
| R1. Independent power | Airo is powered from its own USB-C supply. It does not draw from the keypad port's 5 V **[ASSUMPTION]**; reported budgets around 300 mA leave no margin for Wi-Fi peaks. The desk's 12 V adapter is not used. |
| R2. Isolation preferred | Switch cases use optocouplers/photoMOS, so desk and Airo grounds stay separate. UART cases join grounds at one point. |
| R3. No 5 V on ESP32-S3 pins | Every desk line reaches the Core through an opto, a 5 V-tolerant buffer, or a divider. |
| R4. One driver per line | Parallel pull-low sources only where each button has its own line (A1). Otherwise a selector chooses exactly one source. |
| R5. Fail-safe default = keypad | Every selector's **unpowered** state connects keypad straight through to controller (relay NC contacts or normally-closed photoMOS). Analog switch ICs are not the fail-safe element. |
| R6. MANUAL ONLY switch | Panel toggle that forces the keypad path and cuts injector outputs in hardware, independent of firmware. |
| R7. Hardware watchdog | Selector and injectors enabled only while the Core toggles `HEARTBEAT`. A retriggerable monostable (for example 74HC123) drops the enable within ~200 ms of toggling stopping. |
| R8. High-impedance sensing | Sense taps ≥ 10 kΩ or opto LED at ≤ 1 mA, so sensing never changes what the controller sees. |

### 6.2 Case A1: one line per button

```
 Port B line_UP ──────┬──────────────────────────── Port A line_UP ──┤ UP ├── GND
  (pulled up to 5 V)  │
                      ├──┤ photoMOS NO (AQY212 / TLP222A class) ├── GND_desk
                      └── 22 kΩ ── PC817 LED ── GND_desk ─► phototransistor ─► Core (sense)
 Core / expander ── 330 Ω ── photoMOS LED ── ENABLE (watchdog AND MANUAL ONLY AND ARM)
```

PhotoMOS output in **parallel** with each button. Both only pull low, so they cannot fight. Five injectors, five sense channels. If sense shows a line low that Airo is not driving, a physical button is pressed: release everything (section 7.2). Behaviour with two lines low at once is **[UNKNOWN]** (Q9).

### 6.3 Case A2 / Case C button lines: encoded lines

Reported codes on one Jiecang controller (`HS2 HS1 HS0`, inverted): Down = 1, Up = 2, Preset 1 = 3, Preset 2 = 4 **[COMMUNITY]**.

**Parallel pull-downs are not safe here.** Keypad Down (1) plus Airo Up (2) reads as 3, **Preset 1**, a move nobody asked for. So a selector is used:

```
                     ┌──── K1: 4-pole signal relay, or NC + NO photoMOS pairs ────┐
 Port A HS0..HS3 ────┤ NC  (de-energised: keypad → controller)                     │
                     │ COM ─────────────────────────────────────────────────────── ├──► Port B HS0..HS3
 Injectors HS0..3 ───┤ NO  (energised: Airo → controller)                          │
                     └─────────────────────────────────────────────────────────────┘
 Port A HS0..HS3 ── 10 kΩ ── sense ──► Core       (watched in both positions)
```

K1 energises only during an app command, and only after the keypad has been idle (section 7.2). Any keypad activity drops K1. Controller-side pull-ups must keep lines high with the keypad disconnected; confirm in discovery.

### 6.4 Case A3: resistor ladder

One line, each button selects a different resistance. Reproduce each with a photoMOS switching a 1 % resistor (values measured in discovery) behind a selector as in 6.3. No digital potentiometer (undefined unpowered state).

### 6.5 Case B: digital protocol

`DTX` (controller → keypad) has one driver; Airo only listens. `HTX` (keypad → controller) has one driver, the keypad; Airo transmitting on it too would violate R4. Design: man-in-the-middle on `HTX` with a bypass relay.

```
 Port A HTX ──┬──────────────── K2 NC ──────────────────┐
              │                                         ├──► Port B HTX
              └─► RX buffer ──► Core UART RX             │
                  Core UART TX ──► open-drain buffer ────┴ K2 NO   (74LVC1G07 or BSS138)
 Port B DTX ─────────────────────────────────────────────────────► Port A DTX   (never broken)
            └─► RX buffer ──► Core UART RX2 (listen only, height reports)
 RX buffer: 10 k / 20 k divider into 74LVC1G17 at 3.3 V
```

- K2 de-energised (default, power loss, watchdog, MANUAL ONLY): factory path.
- K2 energised: Airo forwards every keypad frame byte-for-byte within one frame time and inserts its own frames only in gaps. Airo is the single driver of controller `HTX`.
- A keypad movement or stop frame makes Airo abandon its command, forward the frame, and drop K2 after 50 ms of idle.
- Airo only transmits frames recorded from the real keypad during discovery (section 13).

Rejected alternative: shared open-drain `HTX` with collision avoidance. It cannot guarantee the keypad never starts a frame mid-way through Airo's, and violates G5.

### 6.6 Case B0: spare controller port

Some Jiecang controllers have a second port for a Bluetooth/app dongle (reported RJ12: GND, DTX, VCC 5 V, HTX) **[COMMUNITY]**. If present and it accepts frames, Airo uses it: the keypad path is untouched, no K2 needed. Preferred if bench tests show sane behaviour when keypad and port conflict.

### 6.7 Interface Board block list

| Block | Example part | Needed in |
|---|---|---|
| Ports A/B | Native desk connector jacks/plugs (⚠ C2) | All except B0 |
| Port C | Keyed 10-pin JST-PH/XH or Micro-Fit | All |
| I/O expander | MCP23017 | A1, A2, A3, C |
| Injector per line | AQY212 / TLP222A photoMOS + 330 Ω | A1, A2, A3, C |
| Sense per line | PC817 + 22 kΩ, or divider + 74LVC1G17 | All |
| Selector | 4-pole signal relay (NC = keypad path) + MOSFET driver + flyback diode, or NC/NO photoMOS pairs | A2, A3, C (K1); B (K2) |
| UART RX / TX | Divider + 74LVC1G17; 74LVC1G07 open-drain | B, B0, C |
| Watchdog | 74HC123 gating selector and injector enables | All |
| MANUAL ONLY | SPST panel toggle | All |
| Status LED | Green keypad-only, blue Airo path armed, red fault | All |

---

## 7. Fail-safe behaviour and arbitration

### 7.1 Fault table

| Condition | Hardware | Firmware | Desk result |
|---|---|---|---|
| Airo unplugged | n/a | n/a | Keypad back in controller; factory behaviour |
| Core unpowered | Selectors de-energise (R5) | n/a | Keypad path |
| MANUAL ONLY on | Enables cut (R6) | Rejects commands `409 bypass_active` | Keypad path |
| Core crash/hang | Watchdog drops enables ≤ 200 ms (R7) | n/a | App movement stops; keypad path |
| Wi-Fi drop / app closed while holding | none | Lease expires, release | Movement stops |
| Reconnect after drop | none | **No automatic restart**; client must issue a new command | Desk stays stopped |
| Physical key during app command | none | Release ≤ 20 ms, enter `MANUAL_ACTIVE` | Keypad wins |
| Repeated/duplicate command | none | Deduplicated by `request_id`; repeated `desk.up` renews, never stacks | No uncontrolled movement |
| Command exceeds `max_hold_ms` | none | Release | Movement stops |
| Controller limit or error (B/C) | none | Release; `desk_error` event; block the same direction until a reversal or 2 s | Controller protection stays in charge |
| Brown-out / reboot | Selectors de-energised during boot | No driving until 2 s keypad idle and self-test pass | No movement on power-up |

### 7.2 Priority and rules

Priority, highest first:

1. Hardware bypass and watchdog.
2. Physical keypad.
3. `desk.stop` (from any authenticated client or integration).
4. Firmware timers (lease expiry, max duration).
5. Movement commands.

Rules (merging the user's command-service rules with v0.1):

- Only one movement direction at a time; `desk.up` and `desk.down` are never asserted together.
- STOP always accepted and always wins over movement.
- Movement commands expire (lease).
- No automatic movement restart after reconnecting.
- No app command starts while the keypad is active or within 300 ms after it ends; after a manual press, commands are rejected for 1 s.
- One owner per active command; others get `busy`, but anyone may stop.
- A new movement first releases the previous one, waits 50 ms, then applies.
- Presets: see C4 and Q14.
- Local authentication for every command.

### 7.3 Movement semantics

**Hold-to-move (`desk.up`, `desk.down`):** the client holds a lease (`lease_ms` default 400, max 1000) and renews every ≤ 150 ms. Missing renewals release the line.

**Presets (`desk.sitting`, `desk.standing`):** if the controller is one-touch **[UNKNOWN]**, Airo sends a tap (`tap_ms` 150 or one frame); if hold-to-run, presets use leases.

**Stop:**

| Case | Method | Status |
|---|---|---|
| A / C | Release everything, then tap the direction opposite to travel | **[ASSUMPTION]**; must verify it stops without starting a new move |
| B / B0 | Send the controller's stop frame (reported `0x2B`) | **[COMMUNITY]**; verify |

Until verified, the app warns that STOP releases app control only, and the physical keypad must be used to interrupt a one-touch preset.

---

## 8. Firmware architecture

PlatformIO with ESP-IDF (or Arduino-ESP32) for the board, plus a PlatformIO `native` environment for the laptop.

```
┌───────────────────────── airo-core (portable C++, no ESP-IDF includes) ─────────────────────────┐
│ CommandService: schema validation, request_id dedupe, leases, arbitration (7.2), state machine  │
│ StatusModel: state, height (if any), faults                                                      │
│ uses ► HardwareAdapter (interface)                Clock (interface)                               │
└───────────────┬───────────────────────────────────────────────┬──────────────────────────────────┘
                │                                               │
   ┌────────────┴─────────────┐                    ┌────────────┴──────────────────────────────┐
   │ Esp32HardwareAdapter     │                    │ VirtualHardwareAdapter                     │
   │ (on the ESP32-S3)        │                    │ (on the laptop, native build)              │
   │  ├─ SwitchDriver   (A1)  │                    │  ├─ Virtual keypad (5 buttons)            │
   │  ├─ EncodedDriver  (A2/C)│                    │  ├─ Virtual selector (bypass / Airo path) │
   │  ├─ LadderDriver   (A3)  │                    │  ├─ Virtual interface case (A1…B0)        │
   │  └─ FrameDriver    (B/B0)│                    │  └─ Bridge to the 3D simulator (WS)       │
   └──────────────────────────┘                    └───────────────────────────────────────────┘
 Transport layer (shared): REST + WebSocket server, mDNS (board) / localhost (laptop), optional MQTT
 Board-only: BLE provisioning, NVS config, hardware watchdog heartbeat, USB-CDC logging
```

### 8.1 HardwareAdapter interface

```cpp
enum class Action { Up, Down, Sitting, Standing, Reset };

struct HardwareAdapter {
  virtual bool selfTest() = 0;                  // false = refuse to arm, enter FAULT
  virtual void arm(bool on) = 0;                // selector to Airo path (no-op in A1)
  virtual void press(Action a) = 0;             // start asserting
  virtual void tap(Action a, uint16_t ms) = 0;
  virtual void releaseAll() = 0;                // idempotent, must complete within 20 ms
  virtual bool manualActive() = 0;              // keypad activity right now
  virtual bool bypassActive() = 0;              // MANUAL ONLY switch / selector state
  virtual std::optional<uint16_t> heightMm() = 0;
  virtual std::optional<DeskEvent> pollEvent() = 0;  // limit hit, controller error, manual press
};
```

`airo-core` is the single implementation of command logic. It never calls a GPIO directly.

### 8.2 State machine

```
 boot ─► SELF_TEST ─fail─► FAULT (power cycle or authenticated selftest)
             │ ok
             ▼
 ┌───────► IDLE ◄───────────────────────────────────────────────┐
 │           │ command accepted, keypad idle ≥ 300 ms             │
 │           ▼                                                    │
 │       APP_ACTIVE ── lease expiry / stop / max duration / limit ┘
 │           │ keypad activity
 │           ▼
 └ idle 1 s ─ MANUAL_ACTIVE
 BYPASS: from any state while MANUAL ONLY is on; back to IDLE when off.
```

### 8.3 Timing constants

| Name | Default | Purpose |
|---|---|---|
| `manual_release_ms` | 20 | Max time to release after a manual press |
| `manual_quiet_ms` | 1000 | Keypad idle before commands accepted again |
| `pre_command_quiet_ms` | 300 | Keypad idle required before starting |
| `lease_default_ms` / `lease_max_ms` | 400 / 1000 | Hold-to-move lease |
| `tap_ms` | 150 | Simulated short press |
| `max_hold_ms` | 30000 | Cap on any movement |
| `watchdog_period_ms` | 50 | Heartbeat toggle (hardware timeout ~200 ms) |
| `request_id_window` | 64 ids / 60 s | Duplicate suppression |

---

## 9. Setup, discovery and security

### 9.1 BLE provisioning

1. Unprovisioned (or after a 10 s press of the Core's setup button), the Core advertises `Airo-Desk-XXXX` over BLE.
2. The app connects using ESP-IDF unified provisioning over BLE with Security 2 (SRP6a + AES-GCM) and a proof-of-possession code printed on a device label / shown on first USB-C log line.
3. The app sends Wi-Fi credentials; the Core joins Wi-Fi and returns its IP.
4. During the same session the Core generates a random 128-bit API token and returns it over the encrypted BLE channel. The app stores it.
5. BLE stops advertising after provisioning (saves power, reduces attack surface). BLE is setup-only in v1; it is not a control channel.

### 9.2 mDNS

Hostname `airo-desk-XXXX.local`, service `_airo-desk._tcp` on port 80, TXT `api=1`, `fw=<ver>`, `id=<device_id>`.

### 9.3 Authentication

Every request except `GET /api/v1/info` carries `Authorization: Bearer <token>`; WebSocket clients authenticate in the first message. Token reset requires the physical setup button. Plain HTTP on the LAN is accepted for v1 **[ASSUMPTION]**; TLS is Q10.

---

## 10. Unified command schema and local API

Every control path (app REST, app WebSocket, Home Assistant, MQTT, future Alexa) produces the **same command envelope** and goes through the same `CommandService`.

### 10.1 Command envelope

```json
{
  "device_id": "airo-desk-001",
  "command": "desk.up",
  "source": "mobile_app",
  "request_id": "6f1c2a9e-…",
  "lease_ms": 400
}
```

| Field | Required | Notes |
|---|---|---|
| `device_id` | yes | Must match the device; protects against cross-device mistakes |
| `command` | yes | One of 10.2 |
| `source` | yes | `mobile_app`, `web_dashboard`, `home_assistant`, `alexa`, `mqtt`, `simulator`, `test` |
| `request_id` | yes | Unique per command; duplicates are acknowledged but not re-executed |
| `lease_ms` | for `desk.up`/`desk.down` | Default 400, max 1000 |

The user's earlier example form `{"command":"move","direction":"up"}` maps to `desk.up`; v1 accepts only the dotted names to keep one spelling.

### 10.2 Commands

| Command | Effect | Notes |
|---|---|---|
| `desk.up` | Hold-to-move up | Lease; renew with `desk.renew` |
| `desk.down` | Hold-to-move down | Lease |
| `desk.stop` | Release all, then stop method (7.3) | Always accepted from any authenticated source |
| `desk.sitting` | Controller sitting preset | C4 / Q14 |
| `desk.standing` | Controller standing preset | C4 / Q14 |
| `desk.get_status` | Returns status (10.5) | Read-only |
| `desk.renew` | Extend the active lease | Owner only |
| `desk.release` | End hold-to-move | Owner only |

### 10.3 Transport

| Method | Path | Auth | Purpose |
|---|---|---|---|
| GET | `/api/v1/info` | none | Device id, firmware, API version, interface case, capabilities |
| POST | `/api/v1/commands` | token | Submit an envelope; `202` with status, or an error |
| GET | `/api/v1/status` | token | Same as `desk.get_status` |
| POST | `/api/v1/selftest` | token | Re-run self-test, clear `FAULT` |
| GET/PATCH | `/api/v1/config` | token | Timings within safe bounds; PATCH only when `IDLE` |
| WS | `/api/v1/ws` | token in first message | Envelopes in, status/events out; preferred for hold-to-move |

`GET /api/v1/info`:

```json
{
  "device_id": "airo-desk-001",
  "fw": "0.2.0",
  "api": 1,
  "hardware": "esp32-s3",
  "adapter": "esp32",
  "interface_case": "A1",
  "capabilities": {
    "commands": ["desk.up", "desk.down", "desk.stop", "desk.sitting", "desk.standing", "desk.get_status"],
    "height_feedback": false,
    "stop_method_verified": false,
    "preset_mode": "unknown",
    "reset_exposed": false
  }
}
```

### 10.4 WebSocket events (module → client)

```json
{ "type": "status", "state": "APP_ACTIVE", "command": "desk.up", "owner": "mobile_app", "lease_remaining_ms": 310, "height_mm": null, "bypass": false }
{ "type": "ack",    "request_id": "6f1c…", "result": "accepted" }
{ "type": "event",  "event": "manual_override" }
{ "type": "event",  "event": "lease_expired" }
{ "type": "event",  "event": "limit_reached", "direction": "up" }
{ "type": "error",  "request_id": "6f1c…", "code": "manual_active" }
```

Status broadcast on every change and at 2 Hz while `APP_ACTIVE`. A closed owner socket releases its lease immediately.

### 10.5 Status object (`desk.get_status`)

| Field | Type | Meaning |
|---|---|---|
| `state` | `IDLE` \| `APP_ACTIVE` \| `MANUAL_ACTIVE` \| `BYPASS` \| `FAULT` | 8.2 |
| `command` | string \| null | Active command |
| `owner` | string \| null | `source` of the owner |
| `height_mm` | int \| null | Only if `height_feedback` |
| `height_age_ms` | int \| null | |
| `bypass` | bool | MANUAL ONLY / selector state |
| `last_manual_ms` | int | Since last physical key |
| `fault` | string \| null | |

The app shows `-- cm` / "Height feedback not yet confirmed" whenever `height_mm` is null.

### 10.6 Errors

| HTTP | Code | Meaning |
|---|---|---|
| 400 | `bad_request` | Malformed envelope or unknown command |
| 401 | `unauthorized` | Missing or wrong token |
| 403 | `command_not_enabled` | For example `desk.reset`, or presets under strict C4 |
| 404 | `wrong_device` | `device_id` mismatch |
| 409 | `manual_active` | Keypad in use or within quiet time |
| 409 | `bypass_active` | MANUAL ONLY on |
| 409 | `busy` | Another source owns the active command |
| 409 | `direction_blocked` | Limit reached in that direction |
| 423 | `fault` | Run self-test |
| 429 | `rate_limited` | More than 20 commands/s from one source |

### 10.7 Reset

`desk.reset` is reserved and returns `403 command_not_enabled` until discovery records what the circular-arrow button does. When enabled it requires an app confirmation step and uses hold-to-run, never a tap.

---

## 11. Integrations

### 11.1 Optional MQTT bridge

For Home Assistant and debugging with MQTT Explorer. Off by default. Topics carry the same envelope:

| Topic | Direction | Payload |
|---|---|---|
| `airo/<device_id>/cmd` | in | Command envelope (10.1), `source: "mqtt"` |
| `airo/<device_id>/status` | out, retained | Status object (10.5) |
| `airo/<device_id>/event` | out | Events (10.4) |

Broker on the LAN, username/password required. Hold-to-move over MQTT is discouraged; integrations should use presets and stop.

### 11.2 Alexa (later, phase 6)

```
"Alexa, set desk to standing" ─► Amazon Alexa ─► Home Assistant (Alexa integration) ─► Airo local API ─► Airo Core ─► Interface Board ─► desk controller
```

- The Core has no Amazon code and no cloud dependency. Home Assistant is the integration layer.
- Depending on the Alexa route (Home Assistant Cloud or a self-hosted skill), internet access and remote reachability of Home Assistant are needed. **Fully offline Alexa voice control is not promised.**
- Voice exposes only `desk.sitting`, `desk.standing` and `desk.stop`. No hold-to-move by voice, because a voice lease cannot be renewed safely.
- No interface or motor-side hardware changes are needed (G9).

Matter is a later decision. The ESP32-S3 can run Matter (ESP-Matter SDK), but nothing in v1 depends on it.

---

## 12. Laptop end-to-end simulation

Goal (G8): test the whole system on a laptop before the desk is touched, **using the same firmware core** that ships on the board (⚠ C8).

```
 Flutter app / Web dashboard      UP · DOWN · Sitting · Standing · STOP
        │ REST / WebSocket (/api/v1) [/ MQTT]
        ▼
 airo-core (native build)         same CommandService as the ESP32-S3
        │ HardwareAdapter
        ▼
 VirtualHardwareAdapter           virtual keypad · virtual selector (bypass/Airo path) · chosen interface case
        │ WebSocket bridge
        ▼
 Virtual desk controller          existing TS controller/motor/mechanical engines: movement · limits · faults
        ▼
 3D desk simulation               existing React Three Fiber visualisation
```

| Layer | Implementation |
|---|---|
| Firmware core | `firmware/airo-core/` C++, built twice: PlatformIO `esp32-s3` env and `native` env |
| Laptop host | `native` build exposes `/api/v1` on `localhost:8080` |
| Virtual hardware | `VirtualHardwareAdapter` speaks a small JSON bridge protocol to the browser simulator (`keypad.press`, `line.state`, `controller.height`, `controller.limit`, `power.cut`) |
| Desk model | Existing simulator packages model the **desk's** controller and mechanics, not Airo's logic |
| Board-level sim | Wokwi runs the real ESP32-S3 binary with a virtual logic analyzer, for GPIO timing and watchdog tests; it does not model the Green Soul controller's electrical behaviour |
| Unit tests | PlatformIO `pio test -e native` on `airo-core` |

### 12.1 Simulation test matrix

| Test | Expected result |
|---|---|
| Physical UP button | Desk moves up |
| App `desk.up` | Desk moves up while the lease is renewed |
| Physical DOWN button | Desk moves down |
| App `desk.down` | Desk moves down |
| `desk.sitting` | Moves to the controller's sitting preset (C4) |
| `desk.standing` | Moves to the standing preset (C4) |
| Physical and app together | App releases within 20 ms; keypad action wins; no unrequested movement (encoded-line case: no phantom preset) |
| Airo power loss | Selector returns to keypad path; keypad works |
| Network disconnect during hold | Lease expires; movement stops |
| Reconnect after drop | No automatic restart |
| Repeated `desk.up` with same `request_id` | Executed once; no stacked movement |
| `desk.stop` | Movement stops; accepted from a non-owner source |
| Upper/lower limit | Further movement in that direction blocked (`direction_blocked`) |
| Watchdog | Core hang releases within 200 ms (Wokwi) |
| MANUAL ONLY | All commands rejected; keypad works |

Passing these proves the **logic**, not the electrical safety of the real circuit. That still needs bench stages T1–T7 (section 14).

---

## 13. Digital protocol reference (Case B / B0 / C)

> **Everything here is [COMMUNITY] and unverified on this desk.** Different Jiecang models, and different ports on one controller, use different protocols. Treat this as a hypothesis to test against captures, never as a list to send blind.

### 13.1 Physical layer (reported)

| Item | Reported value |
|---|---|
| UART | 9600 baud, 8N1 |
| Levels | Pulled up to 5 V by the controller, pulled low to signal |
| RJ45 handset port, one model | 1 HS3, 2 DTX (controller→handset), 3 GND, 4 HTX (handset→controller), 5 VCC 5 V, 6 HS2, 7 HS1, 8 HS0 |
| RJ12 accessory port, one model | 1 NC, 2 GND, 3 DTX, 4 VCC 5 V, 5 HTX, 6 NC |

### 13.2 Frame format (reported)

```
[addr][addr][cmd][len][param × len][checksum][0x7E]
addr     = 0xF1 0xF1 (handset → controller) or 0xF2 0xF2 (controller → handset)
checksum = (cmd + len + Σparams) & 0xFF
```

### 13.3 Handset → controller (reported)

| Function | Cmd | Frame | Use in Airo |
|---|---|---|---|
| Up (step) | `0x01` | `F1 F1 01 00 01 7E` | `desk.up`, repeated while held |
| Down (step) | `0x02` | `F1 F1 02 00 02 7E` | `desk.down` |
| Move to memory 1 / 2 | `0x05` / `0x06` | `F1 F1 05 00 05 7E` / `F1 F1 06 00 06 7E` | Candidates for sitting/standing |
| Move to memory 3 / 4 | `0x27` / `0x28` | | Not used |
| Stop | `0x2B` | `F1 F1 2B 00 2B 7E` | Candidate for `desk.stop` (one source only) |
| Wake / poll | `0x29` | `F1 F1 29 00 29 7E` | Possibly needed for height reports |
| Settings / heights | `0x07` | `F1 F1 07 00 07 7E` | Sources disagree on reply |
| Move to height (mm) | `0x1B` | `F1 F1 1B 02 HI LO CK 7E` | Not used in v1 |
| Save memory, calibrate, limits, units, sensitivity | `0x03` `0x04` `0x25` `0x26` `0x91` `0x20`–`0x23` `0x0E` `0x1D` | | **Never sent by Airo** |

Airo replays whatever frame the real keypad sends for each icon, as captured in discovery, rather than relying on this table.

### 13.4 Controller → handset (reported)

| Report | Cmd | Format | Example |
|---|---|---|---|
| Height | `0x01` | `F2 F2 01 03 HI LO xx CK 7E`; mm or tenths of an inch | `F2 F2 01 03 05 06 07 14 7E` → 1286 mm |
| Memory 1–4 heights | `0x25`–`0x28` | HI LO | |
| Physical limits | `0x07` | upper HI LO, lower HI LO | `05 14 02 8A` → 1300 / 650 mm |

### 13.5 Pitfalls (reported)

- Some controllers fall into a second, unframed format after idle time.
- Memory saves are not acknowledged.
- Height reports repeat while the display is awake.
- On encoded-line handsets, pulling `HS0` + `HS3` low together wakes height reporting on one model: a possible height-feedback route in Case C.
- Sources disagree on which port carries frames, what `0x07` returns, and whether `0x2B` exists.

### 13.6 "JIECANG_0xFF00"

An earlier dashboard labelled the desk profile `JIECANG_0xFF00`. No public source found uses that identifier. Origin **[UNKNOWN]** (Q2).

### Sources

- phord/Jarvis README and discussion #4: https://github.com/phord/Jarvis
- DeskUp Pro RJ12 hex codes: https://smarthomeguys.github.io/DeskUp-Pro-Controller-RJ12/docs/diy/desk-hex-codes.html
- Home Assistant community, "Desky Standing Desk (ESPHome) [Works with Desky, Uplift, Jiecang, Assmann & others]": https://community.home-assistant.io/t/desky-standing-desk-esphome-works-with-desky-uplift-jiecang-assmann-others/383790
- Rocka84 ESPHome components (via the DeskUp page; README not readable during research): https://github.com/Rocka84/esphome_components

---

## 14. Bench bring-up plan

| Stage | What | Pass criteria |
|---|---|---|
| T0 | Discovery (section 5), passive only | 5.4 table filled in |
| T1 | Interface Board on the bench, **not on the desk**; dummy 5 V + 10 kΩ pull-ups | Correct lines per command; watchdog trips ≤ 200 ms; MANUAL ONLY cuts outputs; unpowered selector connects A↔B (continuity) |
| T2 | Inline, MANUAL ONLY on | Desk behaves exactly as without Airo |
| T3 | MANUAL ONLY off, app idle | Still identical; every physical press sensed |
| T4 | `desk.up`/`desk.down`, desk empty, hand on mains | Moves only while leased; Wi-Fi cut stops it |
| T5 | Physical keys during each app command | Release ≤ 20 ms; no phantom moves |
| T6 | Presets and stop | Same heights as keypad; verified stop method works |
| T7 | Pull USB-C / reboot / kill app mid-move | Stops or never starts; keypad works immediately |
| T8 | One-week soak | No unexpected movement or keypad fault |

---

## 15. Purchase stages and budget

Total envelope: ₹5,000–₹10,000, not spent up front. Prices below are the user's planning estimates or listed prices, not verified quotes.

### 15.1 Buy now (development, simulation and passive discovery)

| Item | Purpose | Note |
|---|---|---|
| ESP32-S3 development board with USB-C | Airo Core prototype | ⚠ C1: not the DevKitC-32E (classic ESP32) |
| Digital multimeter (listed ₹869) | Voltage, resistance, continuity | |
| 8-channel 24 MHz USB logic analyzer | Passive capture | Check input range covers 5 V (C9) |
| Two passive breakouts **matching the keypad plug** | Pin access without cutting | ⚠ C3: check plug type first; no Ethernet magnetics |
| 830-point breadboard + jumper kit | Isolated bench prototypes only | Never for unknown desk signals |
| USB-to-TTL adapter (3.3 V) | Firmware diagnostics | Never on desk pins |
| 10 kΩ resistors (probe series resistors) | Safe passive probing | Small cost, needed for step 6 |

### 15.2 Do not buy yet

| Component | Waiting because |
|---|---|
| Signal relay (4-pole / DPDT) | Contact count and arrangement depend on the case |
| PhotoMOS / optocouplers | Signal type and polarity |
| Analog switch IC | Not used as the fail-safe element in any case |
| Digital isolator / level shifter | Depends on measured logic voltage and protocol |
| Port A/B connectors | Depends on the desk's connector |
| Custom PCB, enclosure | Needs the finalised schematic |
| Any desk power converter | Airo is powered from USB-C (R1) |
| Generic relay module | Not acceptable as the bypass (C10) |

### 15.3 Budget stages

| Stage | Purchase | Target |
|---|---|---|
| 1 | ESP32-S3 board, breadboard, wires, USB-C cable | ₹1,000–₹1,500 |
| 2 | Multimeter, passive breakouts | ₹1,000–₹1,800 |
| 3 | Logic analyzer and probes | ₹500–₹1,200 |
| 4 | Interface components after identification | ₹800–₹2,000 |
| 5 | PCB, enclosure, connectors | ₹1,000–₹2,000 |

---

## 16. Retrofit roadmap

| Phase | Name | Deliverable | Exit criterion |
|---|---|---|---|
| 1 | Laptop simulation | `airo-core` native build, `VirtualHardwareAdapter`, virtual keypad and selector wired to the 3D desk | Section 12.1 matrix passes in CI |
| 2 | ESP32 bench prototype | `airo-core` on ESP32-S3 with `Esp32HardwareAdapter`, driving a simulated keypad circuit (LEDs/pull-ups), not the desk | Same matrix passes against the board; BLE provisioning and mDNS work |
| 3 | Interface identification | Discovery (section 5) | 5.4 table complete, captures committed |
| 4 | Interface Board | Schematic and board for the identified case | Bench T1 passes |
| 5 | Desk integration | Airo inline with original keypad | T2–T8 pass; manual operation independently verified |
| 6 | Alexa | Home Assistant integration (11.2) | Voice sitting/standing/stop work; no Core changes |

These retrofit phases are separate from the simulator's PRD phases in `docs/PRD.md` (added with the Phase 1 simulator work). Retrofit phase 1 reuses that simulator's 3D desk and desk-controller model.

---

## 17. Open questions

| # | Question | How to answer |
|---|---|---|
| Q1 | Controller model and keypad connector? | Discovery step 1 |
| Q2 | Origin of the `JIECANG_0xFF00` label? | User / earlier dashboard source |
| Q3 | Interface case? | Discovery steps 2–7 |
| Q4 | What does the circular-arrow button do, short and long press? | Observe with desk empty; manual |
| Q5 | Presets one-touch or hold-to-run? | Press and release with desk empty |
| Q6 | What stops a preset in progress without starting a new move? | T6 |
| Q7 | Is height reported anywhere, and in what units? | Capture; try the wake behaviour in 13.5 |
| Q8 | Spare accessory port (Case B0)? | Discovery step 3 |
| Q9 | Controller response to two button lines low at once? | T5, desk empty |
| Q10 | Plain HTTP on the LAN acceptable, or TLS in v1? | User decision |
| Q11 | More than one phone controlling the desk? | User decision; API supports one owner plus stop-from-anyone |
| Q12 | Keypad port current budget? | Measure; default stays USB-C power |
| Q13 | Does the added cable length affect a digital bus? | Captures at T2 |
| Q14 | Strict or controller-native reading of "presets require confirmed position feedback" (C4)? | User decision |
| Q15 | Exact ESP32-S3 board and price (C1)? | Pick before ordering stage 1 |
