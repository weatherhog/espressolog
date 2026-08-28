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
      if (now - s.last_sample_ms > STALE_MS) {
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
      if (tryConnect(s, now)) {
        s.state = SlotState::CONNECTED;
        s.backoff_ms = BACKOFF_MIN_MS;
        s.last_sample_ms = now;
        Serial.printf("# scale[%s] connected: %s\n",
                      s.role == ScaleRole::YIELD ? "yield" : "dose",
                      s.bound_mac.c_str());
      } else {
        s.next_retry_ms = now + s.backoff_ms;
        s.backoff_ms = min(s.backoff_ms * 2, BACKOFF_MAX_MS);
      }
      updateScanState();
      break;
    }
    case SlotState::CONNECTING:
      break;   // tryConnect is synchronous; never observed here
  }
}

void ScaleManager::dropConnection(Slot& s, uint32_t now, const char* why) {
  Serial.printf("# scale[%s] lost: %s\n",
                s.role == ScaleRole::YIELD ? "yield" : "dose", why);
  s.dev.reset();   // destroys the NimBLE client
  s.state = SlotState::LOST;
  s.next_retry_ms = now + BACKOFF_MIN_MS;
  s.backoff_ms = BACKOFF_MIN_MS;
  updateScanState();
}

bool ScaleManager::tryConnect(Slot& s, uint32_t now) {
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

  s.state = SlotState::CONNECTING;
  s.dev = RemoteScalesFactory::getInstance()->create(*match);
  if (!s.dev) { s.state = SlotState::LOST; return false; }
  s.dev->setLogCallback(&ScaleManager::onLog);
  s.dev->setWeightUpdatedCallback(
      s.role == ScaleRole::YIELD ? &ScaleManager::onWeightYield
                                 : &ScaleManager::onWeightDose,
      false);
  if (!s.dev->connect()) {
    s.dev.reset();
    s.state = SlotState::LOST;
    return false;
  }
  return true;
}

void ScaleManager::updateScanState() {
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
  if (s.dev) dropConnection(s, millis(), "reassigned");
  s.bound_mac = mac;
  s.enabled = true;
  s.state = SlotState::SCANNING;
  s.next_retry_ms = 0;
  s.backoff_ms = BACKOFF_MIN_MS;
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
