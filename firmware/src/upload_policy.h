#pragma once
#include <stdint.h>

// What to do with an upload response. Pure logic, no Arduino — net.cpp owns
// HTTPClient and LittleFS, this owns the decision.
//
// Split out because this four-branch table is responsible for four findings
// across the 2026-10-12 reviews, three of which lost or nearly lost a shot:
//
//   * every 4xx quarantined, so a 404 after a redeploy or a 429 binned good
//     records into /spool/bad, which nothing ever re-uploads;
//   * 400 was treated as "malformed record" when the server returns it for a
//     DROPPED CONNECTION, so one WiFi hiccup beside a vibratory pump
//     permanently destroyed a shot;
//   * any 2xx meant "stored", but the server's SPA fallback answers 200 with
//     index.html for an unmatched path — so a typo in the endpoint string
//     deleted the entire spool, one record every couple of seconds, silently;
//   * a bare 422 was trusted as a verdict even from something that is not the
//     ingest handler.
//
// Every one of those was a claim about what a status code MEANS, made without
// reading what produces it. A table is cheap to test; prose is not.
enum class UploadVerdict : uint8_t {
  Delete,       // stored, confirmed by this server's ingest handler
  Quarantine,   // this server refused THIS record's content; retrying is futile
  KeepAndWarn,  // 2xx from something that is not the ingest endpoint
  Retry,        // transient, or about the path/proxy rather than the record
};

// `from_ingest` is whether the response carried the ingest handler's marker
// header. A 2xx alone does not prove the record reached that handler.
UploadVerdict classifyResponse(int code, bool from_ingest);

// Is this failure PERMANENT — about the record, the route or the proxy —
// rather than the server or network being down? Only a permanent failure
// justifies telling the operator a record is blocking the queue behind it: a
// diagnostic that says "inspect it with dump" invites `rm`, so pointing it at
// a blameless shot during an ordinary outage can cause the data loss it exists
// to prevent.
//
// IT NEEDS THE CODE, NOT JUST THE VERDICT, and a first version took only the
// verdict. That made every permanent 4xx — 401, 403, 404 and crucially 413 —
// report "the server or the network is unreachable", because they all land in
// Retry. net.h names 413 BY NAME as the case this counter exists to surface,
// so the one failure it was built for was the one it blamed on the weather,
// telling the operator to wait out a condition that never clears.
//
// The server emits 413 for a record over 64 KB and does NOT mark it, and a
// reverse proxy in front can emit 413 at a lower limit of its own.
bool failureIsPermanent(int code, UploadVerdict v);
