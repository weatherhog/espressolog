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

  // Live stream: fire-and-forget frames over WebSocket to the server's fan-
  // out. Drops silently when disconnected — nothing may ever depend on it;
  // the spool is the record, this is only the picture on the wall.
  void sendLive(const char* json);
  bool liveUp() const;

private:
  Spool* spool = nullptr;
  String endpoint;
  bool have_creds = false;
  bool tz_configured = false;
  uint32_t next_upload_ms = 0;
  uint32_t backoff_ms = 5000;
  // Consecutive failures on the SAME record. Narrowing quarantine to 422
  // alone (2026-10-12) was the right call for the data — a 404 or a 503 now
  // keeps the shot instead of binning it — but it removed the only escape
  // from head-of-line blocking: Spool::oldest() returns the same record
  // forever, so one the server will never accept (a 413, or a 2xx without
  // the ingest marker) stalls every record behind it until the spool fills
  // and writeShot starts refusing new shots at 90 %. That is invariant 6
  // again, just slower and quieter. This counter exists so the condition is
  // at least VISIBLE; it does not skip ahead, because skipping risks
  // uploading out of order and the right answer is for a human to look.
  uint32_t stuck_n = 0;
  String   stuck_path;

  static constexpr uint32_t BACKOFF_MIN_MS = 5000;
  static constexpr uint32_t BACKOFF_MAX_MS = 60000;

  // One attempt on the oldest spooled record. true = uploaded (try the next
  // one immediately), false = nothing to do or failed (back off).
  bool tryUploadOldest();
};
