// Espresso shot logger — Phase 0a, stages 0–4.
// BLE scale in, shot detection, records spooled to LittleFS, uploaded over
// HTTP with delete-on-confirm. No machine contact (ever, in phase 0).

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include "scale.h"
#include "detector.h"
#include "spool.h"
#include "net.h"
#include "display.h"
#include <driver/gpio.h>

const char* FIRMWARE_VERSION = "0.8.0";
const char* DETECTOR_VERSION = "0a.6";

static ScaleManager scales;
static ShotDetector detector;
static WeighingDetector weighing[2];   // per role: yield, dose
static Spool spool;
static Net net;
static QueueHandle_t sample_queue;
static bool raw_stream = true;
static bool spool_ok = false;

// Weighings can complete on the dose scale while the yield scale is
// mid-pour; finished events wait here and reach flash only when no shot is
// in progress — invariant 2 holds globally, not per-scale.
struct PendingWeighing { uint8_t role; WeighingDetector::Event ev; };
static PendingWeighing pending_w[8];
static size_t pending_w_n = 0;

// ---------------------------------------------------------------- display bus
// Milestone 0b: the machine's own front display, tapped read-only at J5.
// Protocol and bit offsets are in display.h; the probe point is in CLAUDE.md.
// Idle it carries boiler temperature; during a brew it carries the machine's
// own shot timer, which is a truer pump-on/pump-off boundary than a scale can
// infer.
//
// OFF BY DEFAULT, enabled with `display on` (persisted in NVS). An
// unconnected input floats and would fire this ISR on mains hum at 100 Hz
// forever, so the interrupt is only attached once the tap is actually wired.
//
// Level conditioning — the bus is 5 V and these pins are NOT 5 V tolerant:
//
//     J5 line --[ 8.2K ]--+-- GPIO
//                         +--[ 15K ]-- GND
//
// Safe whichever way the line is driven: 3.23 V at the pin if push-pull,
// 2.69 V if open-drain through the board's 4.7K pull-ups (VIH is 2.48 V), and
// the machine's own high never falls below 4.16 V against a display needing
// 3.5 V. Do NOT enable an internal pull-down — 45K in parallel with the 15K
// leg drops the open-drain case to 2.33 V and the pin stops reading high.
static constexpr uint8_t DISPLAY_CLK_PIN  = 4;
static constexpr uint8_t DISPLAY_DATA_PIN = 5;

static DisplayBus display_bus;
static bool display_on = false;

// Same producer/consumer split as the BLE path: the ISR does nothing but
// stamp and enqueue, loop() is the single consumer. A plain ring rather than
// a FreeRTOS queue only because these edges arrive at ~10 kHz rather than the
// scales' 10 Hz, and per-edge queue overhead at that rate is not worth it.
// Framing is deliberately NOT done here — it lives in DisplayBus::feedEdge so
// the logic that ships is the logic the host tests exercise, and so no flash
// read can ever happen inside the ISR.
struct DbusEdge { uint32_t t_us; uint8_t bit; };
static constexpr uint16_t DBUS_RING = 512;      // ~50 ms of edges at 9.9 kHz
static volatile DbusEdge dbus_ring[DBUS_RING];
static volatile uint16_t dbus_head = 0, dbus_tail = 0;
static volatile uint32_t dbus_dropped = 0;
static uint32_t dbus_frames = 0;

static void IRAM_ATTR dbusIsr() {
  uint16_t h = dbus_head;
  uint16_t nxt = (uint16_t)((h + 1) % DBUS_RING);
  if (nxt == dbus_tail) { dbus_dropped++; return; }   // consumer stalled
  dbus_ring[h].t_us = micros();
  dbus_ring[h].bit  = gpio_get_level((gpio_num_t)DISPLAY_DATA_PIN) ? 1 : 0;
  dbus_head = nxt;
}

static void displaySetEnabled(bool on) {
  if (on == display_on) return;
  if (on) {
    pinMode(DISPLAY_CLK_PIN, INPUT);
    pinMode(DISPLAY_DATA_PIN, INPUT);
    dbus_head = dbus_tail = 0;
    attachInterrupt(digitalPinToInterrupt(DISPLAY_CLK_PIN), dbusIsr, RISING);
  } else {
    detachInterrupt(digitalPinToInterrupt(DISPLAY_CLK_PIN));
  }
  display_on = on;
}

static void pollDisplay() {
  if (!display_on) return;
  while (dbus_tail != dbus_head) {
    DbusEdge e = { dbus_ring[dbus_tail].t_us, dbus_ring[dbus_tail].bit };
    dbus_tail = (uint16_t)((dbus_tail + 1) % DBUS_RING);
    if (display_bus.feedEdge(e.t_us, e.bit)) dbus_frames++;
  }
  if (display_bus.poll(micros())) dbus_frames++;   // bus went quiet mid-frame
}

static void printDisplay(Stream& out) {
  if (!display_on) {
    out.println("# display bus off — 'display on' once the J5 tap is wired");
    return;
  }
  const DisplayBus::Reading& r = display_bus.reading();
  const char* mode = r.mode == DisplayBus::Mode::TEMPERATURE ? "temp"
                   : r.mode == DisplayBus::Mode::TIMER       ? "timer"
                   : r.mode == DisplayBus::Mode::SETPOINT    ? "setpoint"
                   : r.mode == DisplayBus::Mode::BLANK       ? "blank"
                   : r.mode == DisplayBus::Mode::TEXT        ? "text" : "none";
  out.printf("# display=[%s] mode=%s", r.text, mode);
  if (r.mode == DisplayBus::Mode::TEMPERATURE) out.printf(" %d C", (int)r.temp_c);
  else if (r.mode == DisplayBus::Mode::SETPOINT) out.printf(" %d C (being dialled)", (int)r.temp_c);
  else if (r.mode == DisplayBus::Mode::TIMER)  out.printf(" %u.%u s", r.timer_dl / 10, r.timer_dl % 10);
  if (display_bus.adjusting())       out.print(" ADJUSTING");
  if (display_bus.programming())     out.print(" PrG");
  if (display_bus.setpointTouched()) out.print(" SETPOINT-TOUCHED");
  if (display_bus.stale(micros(), 2000000)) out.print(" (STALE)");

  const DisplayBus::Buttons& b = display_bus.buttons();
  out.printf(" | buttons=%s%s", b.left ? "L" : "-", b.right ? "R" : "-");
  out.printf(" | frames=%lu dropped=%lu\n",
             (unsigned long)dbus_frames, (unsigned long)dbus_dropped);
}

static const char* stateName(ShotState s) {
  switch (s) {
    case ShotState::IDLE:     return "IDLE";
    case ShotState::ARMED:    return "ARMED";
    case ShotState::POURING:  return "POURING";
    case ShotState::SETTLING: return "SETTLING";
  }
  return "?";
}

static void printShotSummary(const ShotResult& r) {
  Serial.println("# ---- shot complete ----");
  Serial.printf("# valid=%d fault=%d truncated=%d detector=%s fw=%s\n",
                r.valid, r.fault, r.truncated, DETECTOR_VERSION, FIRMWARE_VERSION);
  Serial.printf("# stop_ms=%lu yield_at_stop_mg=%ld yield_final_mg=%ld settle_offset_mg=%ld\n",
                (unsigned long)r.stop_ms, (long)r.yield_at_stop_mg,
                (long)r.yield_final_mg, (long)r.settle_offset_mg);
  Serial.printf("# peak_flow_mgps=%ld mean_flow_mgps=%ld flow_win=%lu..%lu ms samples=%u\n",
                (long)r.peak_flow_mgps, (long)r.mean_flow_mgps,
                (unsigned long)r.flow_win_start_ms, (unsigned long)r.flow_win_end_ms,
                r.sample_count);
}

static void handleShotComplete(const ShotResult& r) {
  printShotSummary(r);
  // Everything the detector finishes is spooled — including rejects (<5 g),
  // flagged so the server excludes them. A missed second shot on 2026-09-04
  // was undiagnosable because a possible reject left no trace; a few bytes
  // of flash per false trigger buys that visibility.
  if (!spool_ok) {
    Serial.println("# spool unavailable — record NOT persisted");
  } else {
    String path = spool.writeShot(r, scales.slotMac(ScaleRole::YIELD));
    if (path.length()) Serial.printf("# spooled %s%s\n", path.c_str(), r.valid ? "" : " (reject)");
  }
  Serial.println("# ---- end shot ----");
}

// Synthetic record straight into the spool — lets the spool/CLI/upload path
// be exercised without brewing. Clearly marked via detector fields being
// plausible-but-fake: dump it, then rm it.
static void spoolFakeShot(Stream& out) {
  static sample_t fake[220];
  for (int i = 0; i < 220; i++) {
    uint16_t t = i * 100;
    int32_t w = (i < 40) ? i * 25 : 1000 + (i - 40) * 200;   // slow start, then 2 g/s
    fake[i] = { t, w, 0, INT16_MIN };
  }
  ShotResult r = {};
  r.valid = true;
  r.started_at_ms = millis();
  r.stop_ms = 20000;
  r.yield_at_stop_mg = fake[219].weight_mg;
  r.yield_final_mg = fake[219].weight_mg;
  r.peak_flow_mgps = 2000;
  r.mean_flow_mgps = 1900;
  r.flow_win_start_ms = 3000;
  r.flow_win_end_ms = 18000;
  r.sample_count = 220;
  r.samples = fake;
  String path = spool.writeShot(r, scales.slotMac(ScaleRole::YIELD));
  out.printf("# fake shot: %s\n", path.length() ? path.c_str() : "WRITE FAILED");
}

static void handleCommand(String line, Stream& out) {
  line.trim();
  if (line.isEmpty()) return;
  int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  String arg = sp < 0 ? "" : line.substring(sp + 1);

  if (cmd == "help" || cmd == "h") {
    out.println("# status|s  tare|t  raw|r  verbose|v  display [on|off]  ls  dump <file>  rm <file>  fake  net  scales  assign <yield|dose> <mac|last>  set <ssid|pass|endpoint> <val>  reboot");
  } else if (cmd == "status" || cmd == "s") {
    out.printf("# yield: state=%d mac=%s | dose: state=%d mac=%s\n",
               (int)scales.slotState(ScaleRole::YIELD), scales.slotMac(ScaleRole::YIELD).c_str(),
               (int)scales.slotState(ScaleRole::DOSE),
               scales.slotMac(ScaleRole::DOSE).isEmpty() ? "(unbound)" : scales.slotMac(ScaleRole::DOSE).c_str());
    out.printf("# detector=%s flow=%ld mg/s | spool=%u pending | heap=%lu\n",
               stateName(detector.state()), (long)detector.flowNowMgps(),
               (unsigned)spool.pendingCount(), (unsigned long)ESP.getFreeHeap());
    if (display_on) printDisplay(out);
  } else if (cmd == "display") {
    if (arg == "on" || arg == "off") {
      bool on = (arg == "on");
      Preferences p;
      p.begin("espl", false);
      p.putBool("display", on);
      p.end();
      displaySetEnabled(on);
    }
    printDisplay(out);
  } else if (cmd == "tare" || cmd == "t") {
    out.printf("# tare: %s\n", scales.tare(ScaleRole::YIELD) ? "sent" : "not connected");
  } else if (cmd == "raw" || cmd == "r") {
    raw_stream = !raw_stream;
    out.printf("# raw stream %s\n", raw_stream ? "on" : "off");
  } else if (cmd == "verbose" || cmd == "v") {
    static bool verbose = false;
    verbose = !verbose;
    scales.setVerbose(verbose);
    out.printf("# verbose %s\n", verbose ? "on" : "off");
  } else if (cmd == "ls") {
    spool.list(out);
  } else if (cmd == "dump") {
    spool.dump(arg, out);
  } else if (cmd == "rm") {
    out.printf("# rm %s: %s\n", arg.c_str(), spool.remove(arg) ? "ok" : "failed");
  } else if (cmd == "fake") {
    spoolFakeShot(out);
  } else if (cmd == "net") {
    net.status(out);
  } else if (cmd == "scales") {
    scales.listDiscovered(out);
  } else if (cmd == "assign") {
    int sp2 = arg.indexOf(' ');
    String which = sp2 < 0 ? arg : arg.substring(0, sp2);
    String mac = sp2 < 0 ? "" : arg.substring(sp2 + 1);
    if ((which != "yield" && which != "dose") || mac.isEmpty()) {
      out.println("# usage: assign <yield|dose> <mac|last>");
    } else {
      scales.assign(which == "yield" ? ScaleRole::YIELD : ScaleRole::DOSE, mac, out);
    }
  } else if (cmd == "set") {
    int sp2 = arg.indexOf(' ');
    String key = sp2 < 0 ? arg : arg.substring(0, sp2);
    String val = sp2 < 0 ? "" : arg.substring(sp2 + 1);
    const char* nvs_key = key == "ssid" ? "wifi_ssid"
                        : key == "pass" ? "wifi_pass"
                        : key == "endpoint" ? "endpoint" : nullptr;
    if (!nvs_key || val.isEmpty()) {
      out.println("# usage: set <ssid|pass|endpoint> <value>");
    } else {
      Preferences p;
      p.begin("espl", false);
      p.putString(nvs_key, val);
      p.end();
      out.printf("# %s saved to NVS — 'reboot' to apply\n", nvs_key);
    }
  } else if (cmd == "reboot") {
    out.println("# rebooting");
    out.flush();
    ESP.restart();
  } else {
    out.printf("# unknown command: %s ('help' lists them)\n", cmd.c_str());
  }
}

// The CLI answers on both ports: USB CDC (Serial, also the data stream) and
// the UART bridge (Serial0, also the debug log) — whichever the cable is in.
static void pollSerial(Stream& in) {
  static String lines[2];
  String& line = lines[&in == (Stream*)&Serial ? 0 : 1];
  while (in.available()) {
    char c = in.read();
    if (c == '\n' || c == '\r') {
      if (line.length()) handleCommand(line, in);
      line = "";
    } else if (line.length() < 96) {
      line += c;
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial0.begin(115200);   // UART bridge (COM port): debug log + CLI
  delay(2000);   // native USB CDC needs a moment before the first lines land
  Serial.printf("# espressolog %s (detector %s)\n", FIRMWARE_VERSION, DETECTOR_VERSION);

  // 4th arg: our partition is labeled "littlefs", not the wrapper's default "spiffs"
  if (!LittleFS.begin(true /* format on first use */, "/littlefs", 10, "littlefs")) {
    Serial.println("# LittleFS: MOUNT FAILED — shots will NOT be persisted");
  } else {
    spool_ok = spool.begin();
    Serial.printf("# LittleFS: %u/%u KB used, spool %s, %u pending\n",
                  (unsigned)(LittleFS.usedBytes() / 1024),
                  (unsigned)(LittleFS.totalBytes() / 1024),
                  spool_ok ? "ready" : "INIT FAILED",
                  (unsigned)spool.pendingCount());
  }

  sample_queue = xQueueCreate(64, sizeof(ScaleSample));
  scales.begin(sample_queue);
  net.begin(&spool);

  {
    Preferences p;
    p.begin("espl", true);
    bool on = p.getBool("display", false);
    p.end();
    if (on) displaySetEnabled(true);
    Serial.printf("# display bus %s (clk=GPIO%u data=GPIO%u)\n",
                  on ? "on" : "off (see 'display on')",
                  DISPLAY_CLK_PIN, DISPLAY_DATA_PIN);
  }

  Serial.println("# scanning for Bookoo… ('help' for commands)");
}

// Headless status glance: the DevKit's onboard RGB LED shows whether a shot
// would be captured right now. Off = cup scale not connected; amber = idle
// (settling toward a baseline); green = armed; blue = shot in progress.
// Dim on purpose — it lives in a kitchen. Boards with the LED on a
// different pin than the variant defines simply stay dark; nothing depends
// on it.
static void updateLed() {
#ifdef RGB_BUILTIN
  static uint8_t last = 255;
  uint8_t mode;
  if (!scales.connected(ScaleRole::YIELD)) mode = 0;
  else switch (detector.state()) {
    case ShotState::ARMED:   mode = 2; break;
    case ShotState::POURING:
    case ShotState::SETTLING: mode = 3; break;
    default:                 mode = 1; break;
  }
  if (mode == last) return;
  last = mode;
  switch (mode) {
    case 0: neopixelWrite(RGB_BUILTIN, 0, 0, 0); break;
    case 1: neopixelWrite(RGB_BUILTIN, 12, 6, 0); break;   // amber: idle
    case 2: neopixelWrite(RGB_BUILTIN, 0, 14, 0); break;   // green: armed
    case 3: neopixelWrite(RGB_BUILTIN, 0, 4, 16); break;   // blue: pouring
  }
#endif
}

static void queueWeighing(uint8_t role, const WeighingDetector::Event& ev) {
  Serial.printf("# weighing[%s]: %ld mg stable %lu ms\n",
                role == 0 ? "yield" : "dose", (long)ev.grams_mg, (unsigned long)ev.stable_ms);
  if (pending_w_n >= sizeof(pending_w) / sizeof(pending_w[0])) {
    Serial.println("# weighing queue full — event dropped");
    return;
  }
  pending_w[pending_w_n++] = { role, ev };
}

void loop() {
  ScaleSample smp;
  while (xQueueReceive(sample_queue, &smp, 0) == pdTRUE) {
    bool shot_active = detector.state() == ShotState::POURING
                    || detector.state() == ShotState::SETTLING;

    char frame[96];
    if (smp.role == (uint8_t)ScaleRole::YIELD) {
      ShotState before = detector.state();
      bool complete = detector.feed(smp.t_ms, smp.weight_mg);
      if (raw_stream) {
        Serial.printf("%lu,%ld,%s\n", (unsigned long)smp.t_ms,
                      (long)smp.weight_mg, stateName(detector.state()));
      }
      if (detector.state() != before && !complete) {
        Serial.printf("# state %s -> %s\n", stateName(before), stateName(detector.state()));
      }
      snprintf(frame, sizeof(frame), "{\"r\":0,\"t\":%lu,\"w\":%ld,\"s\":\"%s\"}",
               (unsigned long)smp.t_ms, (long)smp.weight_mg, stateName(detector.state()));
      net.sendLive(frame);
      if (complete) {
        handleShotComplete(detector.result());
        snprintf(frame, sizeof(frame), "{\"ev\":\"shot\",\"valid\":%d,\"fault\":%d}",
                 detector.result().valid, detector.result().fault);
        net.sendLive(frame);
      }
      shot_active = detector.state() == ShotState::POURING
                 || detector.state() == ShotState::SETTLING;
    } else {
      if (raw_stream) {
        Serial.printf("%lu,%ld,DOSE\n", (unsigned long)smp.t_ms, (long)smp.weight_mg);
      }
      snprintf(frame, sizeof(frame), "{\"r\":1,\"t\":%lu,\"w\":%ld}",
               (unsigned long)smp.t_ms, (long)smp.weight_mg);
      net.sendLive(frame);
    }

    // Every stable reading on any scale becomes an anonymous weighing —
    // attribution is the server's job when a shot closes.
    if (weighing[smp.role].feed(smp.t_ms, smp.weight_mg, shot_active)) {
      queueWeighing(smp.role, weighing[smp.role].event());
    }
  }

  ShotState st = detector.state();
  bool quiet = st == ShotState::IDLE || st == ShotState::ARMED;

  if (quiet && spool_ok) {
    while (pending_w_n > 0) {
      pending_w_n--;
      String path = spool.writeWeighing(pending_w[pending_w_n].role,
                                        pending_w[pending_w_n].ev,
                                        scales.slotMac((ScaleRole)pending_w[pending_w_n].role));
      if (path.length()) Serial.printf("# spooled %s\n", path.c_str());
    }
  }

  pollDisplay();
  scales.tick(millis());
  // Network (and its flash reads) only while nothing is brewing.
  net.tick(millis(), quiet);
  updateLed();
  pollSerial(Serial);
  pollSerial(Serial0);
  delay(5);   // ~10 Hz data; nothing here needs a tighter spin
}
