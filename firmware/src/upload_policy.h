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

// Does this verdict mean the failure is about the RECORD, rather than the
// network or the server being down? Only that justifies telling the operator
// a record is blocking the queue behind it — a diagnostic that says "inspect
// it with dump" invites `rm`, so pointing it at a blameless shot during an
// ordinary outage can cause the data loss it exists to prevent.
bool verdictBlamesTheRecord(UploadVerdict v);
