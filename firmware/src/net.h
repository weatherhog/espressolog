#pragma once
#include <Arduino.h>

class Spool;

// WiFi + SNTP + spool upload. Everything here is best-effort and non-fatal:
// the spool absorbs any outage, so the radio being down never costs a shot.
// tick() only acts when the caller says the detector is quiet — no network
// (or flash) work during a pour.
class Net {
public:
  void begin(Spool* spool);
  void tick(uint32_t now_ms, bool detector_quiet);
  bool wifiUp() const;
  bool timeValid() const;
  void status(Stream& out);

private:
  Spool* spool = nullptr;
  String endpoint;
  bool have_creds = false;
  bool tz_configured = false;
  uint32_t next_upload_ms = 0;
  uint32_t backoff_ms = 5000;

  static constexpr uint32_t BACKOFF_MIN_MS = 5000;
  static constexpr uint32_t BACKOFF_MAX_MS = 60000;

  // One attempt on the oldest spooled record. true = uploaded (try the next
  // one immediately), false = nothing to do or failed (back off).
  bool tryUploadOldest();
};
