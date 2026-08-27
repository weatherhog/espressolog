// Espresso shot logger — Phase 0a, stages 0–2.
// BLE scale in, shot detection, everything to serial. No flash writes of
// shot data yet (stage 3), no network (stage 4), no machine contact (ever,
// in phase 0).

#include <Arduino.h>
#include <LittleFS.h>
#include "scale.h"
#include "detector.h"

static const char* FIRMWARE_VERSION = "0.2.0-stage2";
static const char* DETECTOR_VERSION = "0a.1";

static ScaleManager scales;
static ShotDetector detector;
static QueueHandle_t sample_queue;
static bool raw_stream = true;

static const char* stateName(ShotState s) {
  switch (s) {
    case ShotState::IDLE:     return "IDLE";
    case ShotState::ARMED:    return "ARMED";
    case ShotState::POURING:  return "POURING";
    case ShotState::SETTLING: return "SETTLING";
  }
  return "?";
}

static void printShotResult(const ShotResult& r) {
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
  Serial.println("# t_ms,weight_mg");
  for (uint16_t i = 0; i < r.sample_count; i++) {
    Serial.printf("%u,%ld\n", r.samples[i].t_ms, (long)r.samples[i].weight_mg);
  }
  Serial.println("# ---- end shot ----");
}

static void handleSerial() {
  while (Serial.available()) {
    switch (Serial.read()) {
      case 't':
        Serial.printf("# tare: %s\n", scales.tare(ScaleRole::YIELD) ? "sent" : "not connected");
        break;
      case 'r':
        raw_stream = !raw_stream;
        Serial.printf("# raw stream %s\n", raw_stream ? "on" : "off");
        break;
      case 'v': {
        static bool verbose = false;
        verbose = !verbose;
        scales.setVerbose(verbose);
        Serial.printf("# verbose %s\n", verbose ? "on" : "off");
        break;
      }
      case 's':
        Serial.printf("# yield scale: state=%d mac=%s | detector=%s flow=%ld mg/s | heap=%lu\n",
                      (int)scales.slotState(ScaleRole::YIELD),
                      scales.slotMac(ScaleRole::YIELD).c_str(),
                      stateName(detector.state()),
                      (long)detector.flowNowMgps(),
                      (unsigned long)ESP.getFreeHeap());
        break;
      case 'h':
        Serial.println("# t=tare r=toggle raw stream v=toggle lib logs s=status");
        break;
      default:
        break;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);   // native USB CDC needs a moment before the first lines land
  Serial.printf("# espressolog %s (detector %s)\n", FIRMWARE_VERSION, DETECTOR_VERSION);

  // Mounted now so a filesystem problem is visible from stage 0, even
  // though nothing is written to it before stage 3.
  if (!LittleFS.begin(true /* format on first use */)) {
    Serial.println("# LittleFS: MOUNT FAILED");
  } else {
    Serial.printf("# LittleFS: %u/%u KB used\n",
                  (unsigned)(LittleFS.usedBytes() / 1024),
                  (unsigned)(LittleFS.totalBytes() / 1024));
  }

  sample_queue = xQueueCreate(64, sizeof(ScaleSample));
  scales.begin(sample_queue);
  Serial.println("# scanning for Bookoo… ('h' for commands)");
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
    if (complete) printShotResult(detector.result());
  }

  scales.tick(millis());
  handleSerial();
  delay(5);   // ~10 Hz data; nothing here needs a tighter spin
}
