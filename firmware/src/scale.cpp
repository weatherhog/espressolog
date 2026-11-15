#include "scale.h"
#include <Preferences.h>
#include <NimBLEDevice.h>
#include "remote_scales.h"
#include "scales/bookoo.h"

#if __has_include("secrets.h")
  #include "secrets.h"
#endif

ScaleManager* ScaleManager::self = nullptr;

ScaleManager::ScaleManager() = default;
ScaleManager::~ScaleManager() = default;

static Preferences prefs;
static RemoteScalesScanner s_scanner;

static const char* nvsKey(ScaleRole role) {
  return role == ScaleRole::YIELD ? "yield_mac" : "dose_mac";
}

void ScaleManager::begin(QueueHandle_t out_queue) {
  self = this;
  queue = out_queue;
  scanner = &s_scanner;

  slots[0].role = ScaleRole::YIELD;
  slots[0].enabled = true;
  slots[1].role = ScaleRole::DOSE;

  prefs.begin("espl", false);
  for (Slot& s : slots) {
    s.bound_mac = prefs.getString(nvsKey(s.role), "");
    s.bound_mac.toLowerCase();
  }
  // The dose slot only runs once deliberately bound ('assign dose <mac>' or
  // secrets.h) — auto-binding the *second* scale would be a guess about
  // which physical scale plays which role.
  slots[1].enabled = !slots[1].bound_mac.isEmpty();
#ifdef SCALE_YIELD_MAC
  if (slots[0].bound_mac.isEmpty()) {
    slots[0].bound_mac = SCALE_YIELD_MAC;
    slots[0].bound_mac.toLowerCase();
    prefs.putString(nvsKey(ScaleRole::YIELD), slots[0].bound_mac);
  }
#endif
#ifdef SCALE_DOSE_MAC
  if (slots[1].bound_mac.isEmpty()) {
    slots[1].bound_mac = SCALE_DOSE_MAC;
    slots[1].bound_mac.toLowerCase();
    prefs.putString(nvsKey(ScaleRole::DOSE), slots[1].bound_mac);
    slots[1].enabled = true;
  }
#endif

  connect_queue = xQueueCreate(2, sizeof(Slot*));
  if (!connect_queue ||
      xTaskCreatePinnedToCore(&ScaleManager::connectTask, "scale_connect", 6144, this, 1,
                              nullptr, 0) != pdPASS) {
    // Without the task every connect would sit PENDING for ever, silently.
    Serial.println("# SCALE CONNECT TASK NOT STARTED — no scale will connect. Reboot.");
    if (connect_queue) { vQueueDelete(connect_queue); connect_queue = nullptr; }
  }

  NimBLEDevice::init("espressolog");
  BookooScalesPlugin::apply();

  for (Slot& s : slots) {
    if (s.enabled) s.state = SlotState::SCANNING;
  }
  updateScanState();
}

void ScaleManager::tick(uint32_t now) {
  for (Slot& s : slots) {
    if (s.enabled) tickSlot(s, now);
  }
}

void ScaleManager::tickSlot(Slot& s, uint32_t now) {
  switch (s.state) {
    case SlotState::CONNECTED: {
      if (!s.dev || !s.dev->isConnected()) {
        dropConnection(s, now, "link down");
        break;
      }
      // Staleness beats the BLE supervision timeout when the scale falls
      // asleep: "connected" with no notifications is not a working scale.
      // Signed: last_sample_ms is written from the BLE task with its own
      // millis(), which can be newer than this `now` — unsigned, that wrapped
      // and dropped a healthy scale as stale (review, 2026-11-15).
      if ((int32_t)(now - s.last_sample_ms) > (int32_t)STALE_MS) {
        dropConnection(s, now, "stale (no samples)");
        break;
      }
      s.dev->update();   // lib heartbeat, self-throttled to 2 s
      break;
    }
    case SlotState::UNBOUND:
    case SlotState::SCANNING:
    case SlotState::LOST: {
      if ((int32_t)(now - s.next_retry_ms) < 0) break;
      if (anyConnecting()) break;          // one radio, one connect at a time
      if (!startConnect(s, now)) {
        s.next_retry_ms = now + s.backoff_ms;
        s.backoff_ms = min(s.backoff_ms * 2, BACKOFF_MAX_MS);
        updateScanState();
      }
      break;
    }
    case SlotState::CONNECTING: {
      // Polled, never waited on: connectTask owns `dev` until this changes.
      uint8_t r = s.connect_result.load(std::memory_order_acquire);
      if (r == CONNECT_PENDING) {
        if (!s.connect_slow_logged && now - s.connect_started_ms > CONNECT_SLOW_MS) {
          s.connect_slow_logged = true;
          Serial.printf("# scale[%s] connect still pending after %lu s — the other scale "
                        "cannot connect meanwhile\n",
                        s.role == ScaleRole::YIELD ? "yield" : "dose",
                        (unsigned long)((now - s.connect_started_ms) / 1000));
        }
        break;
      }
      s.connect_result.store(CONNECT_IDLE, std::memory_order_relaxed);
      if (r == CONNECT_OK) {
        s.state = SlotState::CONNECTED;
        // The connect-failure backoff restarts: the scale is awake. Whether
        // a DROP backs off is link_backoff_ms's business (dropConnection).
        s.backoff_ms = BACKOFF_MIN_MS;
        s.connected_at_ms = now;
        // MUST STAY HERE, AFTER the acquire load above: it overwrites the
        // stamp of the lib's injected setWeight(0) — which ran inside
        // connect(), before connectTask's release store — so dropConnection
        // can tell a real notification by a stamp after connected_at_ms.
        s.last_sample_ms = now;
        Serial.printf("# scale[%s] connected: %s (%lu ms, off the loop)\n",
                      s.role == ScaleRole::YIELD ? "yield" : "dose",
                      s.bound_mac.c_str(), (unsigned long)(now - s.connect_started_ms));
      } else {
        s.dev.reset();
        s.state = SlotState::LOST;
        s.next_retry_ms = now + s.backoff_ms;
        s.backoff_ms = min(s.backoff_ms * 2, BACKOFF_MAX_MS);
      }
      updateScanState();
      break;
    }
  }
}

void ScaleManager::dropConnection(Slot& s, uint32_t now, const char* why) {
  Serial.printf("# scale[%s] lost: %s\n",
                s.role == ScaleRole::YIELD ? "yield" : "dose", why);
  s.dev.reset();   // destroys the NimBLE client; no sample lands after this
  s.state = SlotState::LOST;
  // Did this link deliver a real sample? Decided here, from the stamps,
  // rather than by a flag set on a later CONNECTED tick, which a loop()
  // stall could skip (review 3, 2026-11-15). One gap remains: samples that
  // arrive between connect() returning and loop() seeing CONNECT_OK are
  // overwritten there with the injected 0 g's stamp, so a link that
  // delivers only in that window and then drops reads as silent. See
  // CONNECT_OK on why the lib's injected 0 g does not count.
  const bool delivered = (int32_t)(s.last_sample_ms - s.connected_at_ms) > 0;
  // Two drop backoffs (reviews 2026-11-15):
  //  - silent links (connect, then nothing — what an asleep scale is
  //    inferred to do) grow link_backoff_ms to BACKOFF_MAX_MS, so they no
  //    longer retry every second;
  //  - delivering links grow flap_backoff_ms only to FLAP_BACKOFF_MAX_MS: a
  //    hiccup just after a wake never waits 30 s (a whole pour), and a scale
  //    flapping at the edge of range does not hold the radio every second.
  //    It restarts after a link of LONG_LINK_MS.
  // Failed connects after the drop grow from the drop's delay.
  uint32_t delay;
  if (delivered) {
    s.link_backoff_ms = BACKOFF_MIN_MS;
    if (now - s.connected_at_ms >= LONG_LINK_MS) s.flap_backoff_ms = BACKOFF_MIN_MS;
    delay = s.flap_backoff_ms;
    s.flap_backoff_ms = min(s.flap_backoff_ms * 2, FLAP_BACKOFF_MAX_MS);
  } else {
    delay = s.link_backoff_ms;
    s.link_backoff_ms = min(s.link_backoff_ms * 2, BACKOFF_MAX_MS);
  }
  s.next_retry_ms = now + delay;
  s.backoff_ms = min(delay * 2, BACKOFF_MAX_MS);
  updateScanState();
}

bool ScaleManager::anyConnecting() const {
  for (const Slot& s : slots)
    if (s.state == SlotState::CONNECTING) return true;
  return false;
}

// Runs on its own task. Waits for a slot whose `dev` loop() has built and
// handed over, makes the blocking connect, and publishes the outcome.
void ScaleManager::connectTask(void* arg) {
  ScaleManager* self = static_cast<ScaleManager*>(arg);
  Slot* s = nullptr;
  for (;;) {
    if (xQueueReceive(self->connect_queue, &s, portMAX_DELAY) != pdTRUE || !s) continue;
    bool ok = s->dev && s->dev->connect();
    s->connect_result.store(ok ? CONNECT_OK : CONNECT_FAIL, std::memory_order_release);
  }
}

// Picks a candidate and hands the blocking connect to connectTask. Returns
// false if there is nothing to connect to, or it could not be handed over;
// true means CONNECTING, and tickSlot() collects the result.
bool ScaleManager::startConnect(Slot& s, uint32_t now) {
  // Pick a candidate from the scanner: the bound MAC if we have one,
  // otherwise (bootstrap) the first Bookoo seen, which is then persisted.
  DiscoveredDevice const* match = nullptr;
  auto discovered = scanner->getDiscoveredScales();
  for (const auto& d : discovered) {
    String mac = String(d.getAddress().toString().c_str());
    mac.toLowerCase();
    if (s.bound_mac.isEmpty() || mac == s.bound_mac) { match = &d; break; }
  }
  if (!match) return false;

  if (s.bound_mac.isEmpty()) {
    s.bound_mac = String(match->getAddress().toString().c_str());
    s.bound_mac.toLowerCase();
    prefs.putString(nvsKey(s.role), s.bound_mac);
    Serial.printf("# scale[%s] bound to %s (persisted)\n",
                  s.role == ScaleRole::YIELD ? "yield" : "dose",
                  s.bound_mac.c_str());
  }

  // Scan and connect don't coexist reliably on one radio.
  scanner->stopAsyncScan();

  s.dev = RemoteScalesFactory::getInstance()->create(*match);
  if (!s.dev) { s.state = SlotState::LOST; return false; }
  s.dev->setLogCallback(&ScaleManager::onLog);
  s.dev->setWeightUpdatedCallback(
      s.role == ScaleRole::YIELD ? &ScaleManager::onWeightYield
                                 : &ScaleManager::onWeightDose,
      false);
  s.connect_result.store(CONNECT_PENDING, std::memory_order_relaxed);
  s.connect_started_ms = now;
  s.connect_slow_logged = false;
  Slot* p = &s;
  if (!connect_queue || xQueueSend(connect_queue, &p, 0) != pdTRUE) {
    s.connect_result.store(CONNECT_IDLE, std::memory_order_relaxed);
    s.dev.reset();
    s.state = SlotState::LOST;
    return false;
  }
  s.state = SlotState::CONNECTING;
  return true;
}

void ScaleManager::updateScanState() {
  // Scan and connect don't coexist on one radio: leave the scan alone while
  // connectTask is connecting; the CONNECTING case calls this again after.
  if (anyConnecting()) return;
  bool need_scan = false;
  for (Slot& s : slots) {
    if (s.enabled && s.state != SlotState::CONNECTED) need_scan = true;
  }
  if (need_scan && !scanner->isScanRunning()) scanner->restartAsyncScan();
  if (!need_scan && scanner->isScanRunning()) scanner->stopAsyncScan();
}

bool ScaleManager::connected(ScaleRole role) const {
  return slots[(uint8_t)role].state == SlotState::CONNECTED;
}

ScaleManager::SlotState ScaleManager::slotState(ScaleRole role) const {
  return slots[(uint8_t)role].state;
}

String ScaleManager::slotMac(ScaleRole role) const {
  return slots[(uint8_t)role].bound_mac;
}

void ScaleManager::listDiscovered(Stream& out) {
  auto discovered = scanner->getDiscoveredScales();
  if (discovered.empty()) {
    out.printf("# no Bookoos discovered%s\n",
               scanner->isScanRunning() ? " yet (scanning)" : " (scan idle — all bound slots connected)");
    return;
  }
  for (const auto& d : discovered) {
    String mac = String(d.getAddress().toString().c_str());
    mac.toLowerCase();
    const char* role = "";
    if (mac == slots[0].bound_mac) role = "  [yield]";
    else if (mac == slots[1].bound_mac) role = "  [dose]";
    out.printf("# %s  %s%s\n", mac.c_str(), d.getName().c_str(), role);
  }
}

bool ScaleManager::assign(ScaleRole role, const String& mac_or_last, Stream& out) {
  String mac = mac_or_last;
  mac.toLowerCase();
  if (mac == "last") {
    auto discovered = scanner->getDiscoveredScales();
    if (discovered.empty()) {
      out.println("# nothing discovered to assign ('scales' lists candidates)");
      return false;
    }
    mac = String(discovered.back().getAddress().toString().c_str());
    mac.toLowerCase();
  }
  if (mac.length() != 17) {
    out.printf("# not a MAC: %s\n", mac.c_str());
    return false;
  }
  Slot& s = slots[(uint8_t)role];
  // connectTask owns `dev` while CONNECTING; destroying it now would pull
  // the client out from under a connect in progress.
  if (s.state == SlotState::CONNECTING) {
    out.println("# that scale is mid-connect — try again in a few seconds");
    return false;
  }
  if (s.dev) dropConnection(s, millis(), "reassigned");
  s.bound_mac = mac;
  s.enabled = true;
  s.state = SlotState::SCANNING;
  s.next_retry_ms = 0;
  s.backoff_ms = BACKOFF_MIN_MS;
  s.link_backoff_ms = BACKOFF_MIN_MS;
  s.flap_backoff_ms = BACKOFF_MIN_MS;
  prefs.putString(nvsKey(role), mac);
  updateScanState();
  out.printf("# %s scale bound to %s (persisted)\n",
             role == ScaleRole::YIELD ? "yield" : "dose", mac.c_str());
  return true;
}

bool ScaleManager::tare(ScaleRole role) {
  Slot& s = slots[(uint8_t)role];
  if (s.state != SlotState::CONNECTED || !s.dev) return false;
  return s.dev->tare();
}

// ---- BLE-task side ---------------------------------------------------------
// The lib's weight callback is a bare void(*)(float) with no context, hence
// one trampoline per role.

void ScaleManager::enqueue(ScaleRole role, float grams) {
  Slot& s = slots[(uint8_t)role];
  ScaleSample smp = { millis(), (int32_t)lroundf(grams * 1000.0f), (uint8_t)role };
  s.last_sample_ms = smp.t_ms;
  xQueueSend(queue, &smp, 0);   // full queue drops the sample; consumer is far faster
}

void ScaleManager::onWeightYield(float grams) {
  if (self) self->enqueue(ScaleRole::YIELD, grams);
}

void ScaleManager::onWeightDose(float grams) {
  if (self) self->enqueue(ScaleRole::DOSE, grams);
}

void ScaleManager::onLog(std::string msg) {
  if (self && self->verbose) Serial.printf("# lib: %s", msg.c_str());
}
