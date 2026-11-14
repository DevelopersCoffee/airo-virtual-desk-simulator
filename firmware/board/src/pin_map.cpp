#include "airo/board/pin_map.h"

#include <set>

// Breadboard placeholders. [UNVERIFIED] Change with -DAIRO_PIN_<NAME>=n.
#ifndef AIRO_PIN_HEARTBEAT
#define AIRO_PIN_HEARTBEAT 4
#endif
#ifndef AIRO_PIN_ARM
#define AIRO_PIN_ARM 5
#endif
#ifndef AIRO_PIN_BYPASS_STATE
#define AIRO_PIN_BYPASS_STATE 6
#endif
// A1 injectors and A2 encoded injectors share pins: one board is one case.
#ifndef AIRO_PIN_INJ_UP
#define AIRO_PIN_INJ_UP 7
#endif
#ifndef AIRO_PIN_INJ_DOWN
#define AIRO_PIN_INJ_DOWN 15
#endif
#ifndef AIRO_PIN_INJ_SIT
#define AIRO_PIN_INJ_SIT 16
#endif
#ifndef AIRO_PIN_INJ_STAND
#define AIRO_PIN_INJ_STAND 17
#endif
#ifndef AIRO_PIN_INJ_HS0
#define AIRO_PIN_INJ_HS0 7
#endif
#ifndef AIRO_PIN_INJ_HS1
#define AIRO_PIN_INJ_HS1 15
#endif
#ifndef AIRO_PIN_INJ_HS2
#define AIRO_PIN_INJ_HS2 16
#endif
#ifndef AIRO_PIN_INJ_HS3
#define AIRO_PIN_INJ_HS3 17
#endif
#ifndef AIRO_PIN_SENSE_UP
#define AIRO_PIN_SENSE_UP 8
#endif
#ifndef AIRO_PIN_SENSE_DOWN
#define AIRO_PIN_SENSE_DOWN 9
#endif
#ifndef AIRO_PIN_SENSE_SIT
#define AIRO_PIN_SENSE_SIT 10
#endif
#ifndef AIRO_PIN_SENSE_STAND
#define AIRO_PIN_SENSE_STAND 11
#endif
#ifndef AIRO_PIN_SENSE_RESET
#define AIRO_PIN_SENSE_RESET 12
#endif
#ifndef AIRO_PIN_SENSE_HS0
#define AIRO_PIN_SENSE_HS0 8
#endif
#ifndef AIRO_PIN_SENSE_HS1
#define AIRO_PIN_SENSE_HS1 9
#endif
#ifndef AIRO_PIN_SENSE_HS2
#define AIRO_PIN_SENSE_HS2 10
#endif
#ifndef AIRO_PIN_SENSE_HS3
#define AIRO_PIN_SENSE_HS3 11
#endif
#ifndef AIRO_PIN_UART_HTX_TX
#define AIRO_PIN_UART_HTX_TX 18
#endif
#ifndef AIRO_PIN_UART_HTX_RX
#define AIRO_PIN_UART_HTX_RX 13
#endif
#ifndef AIRO_PIN_UART_DTX_RX
#define AIRO_PIN_UART_DTX_RX 14
#endif
// Breadboard default: outputs light an LED when asserted, inputs are buttons
// to GND with the internal pull-up (pressed = LOW).
#ifndef AIRO_OUTPUTS_ACTIVE_LOW
#define AIRO_OUTPUTS_ACTIVE_LOW 0
#endif
#ifndef AIRO_INPUTS_ACTIVE_LOW
#define AIRO_INPUTS_ACTIVE_LOW 1
#endif

namespace airo::board {

namespace {
struct Name {
  Signal s;
  const char* name;
};
constexpr Name kNames[] = {
    {Signal::Heartbeat, "heartbeat"},   {Signal::Arm, "arm"},
    {Signal::InjUp, "inj_up"},          {Signal::InjDown, "inj_down"},
    {Signal::InjSit, "inj_sit"},        {Signal::InjStand, "inj_stand"},
    {Signal::InjHs0, "inj_hs0"},        {Signal::InjHs1, "inj_hs1"},
    {Signal::InjHs2, "inj_hs2"},        {Signal::InjHs3, "inj_hs3"},
    {Signal::BypassState, "bypass_state"}, {Signal::SenseUp, "sense_up"},
    {Signal::SenseDown, "sense_down"},  {Signal::SenseSit, "sense_sit"},
    {Signal::SenseStand, "sense_stand"}, {Signal::SenseReset, "sense_reset"},
    {Signal::SenseHs0, "sense_hs0"},    {Signal::SenseHs1, "sense_hs1"},
    {Signal::SenseHs2, "sense_hs2"},    {Signal::SenseHs3, "sense_hs3"},
};

// ESP32-S3 pins that must never carry an interface signal (spec section 2):
// strapping 0/3/45/46, native USB 19/20, SPI flash 26-32, UART0 console 43/44,
// and the DevKitC-1 RGB LED (38 on v1.0, 48 on v1.1).
bool forbidden(int g) {
  return g == 0 || g == 3 || g == 45 || g == 46 || g == 19 || g == 20 || (g >= 26 && g <= 32) || g == 43 ||
         g == 44 || g == 38 || g == 48;
}
bool exists(int g) { return (g >= 0 && g <= 21) || (g >= 26 && g <= 48); }
}  // namespace

const char* toString(Signal s) {
  for (const Name& n : kNames)
    if (n.s == s) return n.name;
  return "?";
}

std::optional<Signal> parseSignal(const std::string& name) {
  for (const Name& n : kNames)
    if (name == n.name) return n.s;
  return std::nullopt;
}

const char* toString(UartLine l) {
  switch (l) {
    case UartLine::HtxTx: return "uart_htx_tx";
    case UartLine::HtxRx: return "uart_htx_rx";
    case UartLine::DtxRx: return "uart_dtx_rx";
    case UartLine::Count: break;
  }
  return "?";
}

PinMap defaultPinMap() {
  PinMap m;
  auto out = [&](Signal s, int g) { m[s] = {g, AIRO_OUTPUTS_ACTIVE_LOW != 0}; };
  auto in = [&](Signal s, int g) { m[s] = {g, AIRO_INPUTS_ACTIVE_LOW != 0}; };
  out(Signal::Heartbeat, AIRO_PIN_HEARTBEAT);
  out(Signal::Arm, AIRO_PIN_ARM);
  out(Signal::InjUp, AIRO_PIN_INJ_UP);
  out(Signal::InjDown, AIRO_PIN_INJ_DOWN);
  out(Signal::InjSit, AIRO_PIN_INJ_SIT);
  out(Signal::InjStand, AIRO_PIN_INJ_STAND);
  out(Signal::InjHs0, AIRO_PIN_INJ_HS0);
  out(Signal::InjHs1, AIRO_PIN_INJ_HS1);
  out(Signal::InjHs2, AIRO_PIN_INJ_HS2);
  out(Signal::InjHs3, AIRO_PIN_INJ_HS3);
  in(Signal::BypassState, AIRO_PIN_BYPASS_STATE);
  in(Signal::SenseUp, AIRO_PIN_SENSE_UP);
  in(Signal::SenseDown, AIRO_PIN_SENSE_DOWN);
  in(Signal::SenseSit, AIRO_PIN_SENSE_SIT);
  in(Signal::SenseStand, AIRO_PIN_SENSE_STAND);
  in(Signal::SenseReset, AIRO_PIN_SENSE_RESET);
  in(Signal::SenseHs0, AIRO_PIN_SENSE_HS0);
  in(Signal::SenseHs1, AIRO_PIN_SENSE_HS1);
  in(Signal::SenseHs2, AIRO_PIN_SENSE_HS2);
  in(Signal::SenseHs3, AIRO_PIN_SENSE_HS3);
  m.uart(UartLine::HtxTx) = AIRO_PIN_UART_HTX_TX;
  m.uart(UartLine::HtxRx) = AIRO_PIN_UART_HTX_RX;
  m.uart(UartLine::DtxRx) = AIRO_PIN_UART_DTX_RX;
  m.verified = false;
  return m;
}

std::vector<Signal> signalsFor(InterfaceCase c) {
  std::vector<Signal> v = {Signal::Heartbeat, Signal::Arm, Signal::BypassState};
  switch (c) {
    case InterfaceCase::A1:
      for (Signal s : {Signal::InjUp, Signal::InjDown, Signal::InjSit, Signal::InjStand, Signal::SenseUp,
                       Signal::SenseDown, Signal::SenseSit, Signal::SenseStand, Signal::SenseReset})
        v.push_back(s);
      break;
    case InterfaceCase::A2:
      for (Signal s : {Signal::InjHs0, Signal::InjHs1, Signal::InjHs2, Signal::InjHs3, Signal::SenseHs0,
                       Signal::SenseHs1, Signal::SenseHs2, Signal::SenseHs3})
        v.push_back(s);
      break;
    case InterfaceCase::B: break;
  }
  return v;
}

std::vector<UartLine> uartLinesFor(InterfaceCase c) {
  if (c == InterfaceCase::B) return {UartLine::HtxTx, UartLine::HtxRx, UartLine::DtxRx};
  return {};
}

std::vector<std::string> validate(const PinMap& m, InterfaceCase c) {
  std::vector<std::string> errs;
  std::set<int> used;
  auto check = [&](const std::string& name, int g, bool required) {
    if (g < 0) {
      if (required) errs.push_back(name + " is not assigned");
      return;
    }
    if (!exists(g)) errs.push_back(name + ": GPIO" + std::to_string(g) + " does not exist on the ESP32-S3");
    else if (forbidden(g)) errs.push_back(name + ": GPIO" + std::to_string(g) + " is reserved (strapping/USB/flash/console/LED)");
    if (!used.insert(g).second) errs.push_back(name + ": GPIO" + std::to_string(g) + " is used twice");
  };
  for (Signal s : signalsFor(c)) {
    // SenseReset is optional: the reset key is only ever sensed, never driven.
    check(toString(s), m[s].gpio, s != Signal::SenseReset);
  }
  for (UartLine l : uartLinesFor(c)) check(toString(l), m.uart(l), true);
  return errs;
}

std::vector<std::string> warnings(const PinMap& m, InterfaceCase c) {
  std::vector<std::string> w;
  auto look = [&](const std::string& name, int g) {
    if (g >= 33 && g <= 37) w.push_back(name + ": GPIO" + std::to_string(g) + " is used by octal PSRAM on N8R8/N16R8 modules");
    if (g >= 39 && g <= 42) w.push_back(name + ": GPIO" + std::to_string(g) + " is a JTAG pin");
  };
  for (Signal s : signalsFor(c)) look(toString(s), m[s].gpio);
  for (UartLine l : uartLinesFor(c)) look(toString(l), m.uart(l));
  if (!m.verified) w.push_back("pin map is UNVERIFIED: breadboard placeholders, not measured on the desk");
  return w;
}

std::optional<Signal> injectorFor(Action a) {
  switch (a) {
    case Action::Up: return Signal::InjUp;
    case Action::Down: return Signal::InjDown;
    case Action::Sitting: return Signal::InjSit;
    case Action::Standing: return Signal::InjStand;
    case Action::Reset: return std::nullopt;  // never driven
  }
  return std::nullopt;
}

Signal senseFor(Action a) {
  switch (a) {
    case Action::Up: return Signal::SenseUp;
    case Action::Down: return Signal::SenseDown;
    case Action::Sitting: return Signal::SenseSit;
    case Action::Standing: return Signal::SenseStand;
    case Action::Reset: return Signal::SenseReset;
  }
  return Signal::SenseReset;
}

}  // namespace airo::board
