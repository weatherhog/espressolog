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

const char* FIRMWARE_VERSION = "0.4.0-stage4";
const char* DETECTOR_VERSION = "0a.1";

static ScaleManager scales;
static ShotDetector detector;
static Spool spool;
static Net net;
static QueueHandle_t sample_queue;
static bool raw_stream = true;
static bool spool_ok = false;

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
  // Valid shots and faults are spooled (a fault still holds a curve worth
  // keeping); rejects (<5 g) are noise by definition and only logged.
  if (r.valid || r.fault) {
    if (!spool_ok) {
      Serial.println("# spool unavailable — record NOT persisted");
    } else {
      String path = spool.writeShot(r, scales.slotMac(ScaleRole::YIELD));
      if (path.length()) Serial.printf("# spooled %s\n", path.c_str());
    }
  } else {
    Serial.println("# rejected (<5 g) — not spooled");
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
    out.println("# status|s  tare|t  raw|r  verbose|v  ls  dump <file>  rm <file>  fake  net  set <ssid|pass|endpoint> <val>  reboot");
  } else if (cmd == "status" || cmd == "s") {
    out.printf("# yield scale: state=%d mac=%s | detector=%s flow=%ld mg/s | spool=%u pending | heap=%lu\n",
               (int)scales.slotState(ScaleRole::YIELD),
               scales.slotMac(ScaleRole::YIELD).c_str(),
               stateName(detector.state()),
               (long)detector.flowNowMgps(),
               (unsigned)spool.pendingCount(),
               (unsigned long)ESP.getFreeHeap());
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
  Serial.println("# scanning for Bookoo… ('help' for commands)");
}

void loop() {
  ScaleSample smp;
  while (xQueueReceive(sample_queue, &smp, 0) == pdTRUE) {
    if (smp.role != (uint8_t)ScaleRole::YIELD) continue;   // dose scale: stage 5
    ShotState before = detector.state();
    bool complete = detector.feed(smp.t_ms, smp.weight_mg);
    if (raw_stream) {
      Serial.printf("%lu,%ld,%s\n", (unsigned long)smp.t_ms,
                    (long)smp.weight_mg, stateName(detector.state()));
    }
    if (detector.state() != before && !complete) {
      Serial.printf("# state %s -> %s\n", stateName(before), stateName(detector.state()));
    }
    if (complete) handleShotComplete(detector.result());
  }

  scales.tick(millis());
  // Network (and its flash reads) only while nothing is brewing.
  ShotState st = detector.state();
  net.tick(millis(), st == ShotState::IDLE || st == ShotState::ARMED);
  pollSerial(Serial);
  pollSerial(Serial0);
  delay(5);   // ~10 Hz data; nothing here needs a tighter spin
}
