#include "net.h"
#include "spool.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebSocketsClient.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <time.h>

#if __has_include("secrets.h")
  #include "secrets.h"
#endif

static Preferences net_prefs;
static WebSocketsClient ws;
static bool ws_up = false;

// Pull "espressolog.lan" and 8080 out of "http://espressolog.lan:8080".
static bool parseEndpoint(const String& url, String& host, uint16_t& port) {
  String s = url;
  if (s.startsWith("http://")) s = s.substring(7);
  else if (s.startsWith("https://")) return false;   // ESP side stays plain HTTP/WS
  int slash = s.indexOf('/');
  if (slash >= 0) s = s.substring(0, slash);
  int colon = s.indexOf(':');
  port = 80;
  if (colon >= 0) {
    port = (uint16_t)s.substring(colon + 1).toInt();
    s = s.substring(0, colon);
  }
  host = s;
  return !host.isEmpty();
}

void Net::begin(Spool* sp) {
  spool = sp;

  net_prefs.begin("espl", false);
  String ssid = net_prefs.getString("wifi_ssid", "");
  String pass = net_prefs.getString("wifi_pass", "");
  endpoint    = net_prefs.getString("endpoint", "");
#ifdef WIFI_SSID
  if (ssid.isEmpty()) { ssid = WIFI_SSID; net_prefs.putString("wifi_ssid", ssid); }
#endif
#ifdef WIFI_PASS
  if (pass.isEmpty()) { pass = WIFI_PASS; net_prefs.putString("wifi_pass", pass); }
#endif
#ifdef ENDPOINT_URL
  if (endpoint.isEmpty()) { endpoint = ENDPOINT_URL; net_prefs.putString("endpoint", endpoint); }
#endif

  have_creds = !ssid.isEmpty();
  if (!have_creds) {
    Serial.println("# net: no WiFi credentials (set wifi_ssid via CLI or secrets.h)");
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.printf("# net: connecting to '%s', uploads -> %s\n", ssid.c_str(), endpoint.c_str());

  String host;
  uint16_t port;
  if (parseEndpoint(endpoint, host, port)) {
    ws.begin(host, port, "/api/v1/live/device");
    ws.setReconnectInterval(5000);
    ws.onEvent([](WStype_t type, uint8_t*, size_t) {
      if (type == WStype_CONNECTED) { ws_up = true; Serial.println("# net: live stream connected"); }
      else if (type == WStype_DISCONNECTED) ws_up = false;
    });
  }
}

bool Net::wifiUp() const { return WiFi.status() == WL_CONNECTED; }

bool Net::timeValid() const { return time(nullptr) > 1600000000; }

void Net::sendLive(const char* json) {
  if (ws_up) ws.sendTXT(json);
}

bool Net::liveUp() const { return ws_up; }

void Net::tick(uint32_t now, bool detector_quiet) {
  if (!have_creds) return;
  // The live socket runs always — streaming during the pour is its purpose,
  // and it's radio-only, no flash. Everything below stays gated on quiet.
  ws.loop();
  if (!detector_quiet) return;

  if (wifiUp() && !tz_configured) {
    // Berlin with DST rules; tz_offset_min in spool headers derives from this.
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.cloudflare.com");
    tz_configured = true;
    Serial.printf("# net: WiFi up, %s — SNTP started\n", WiFi.localIP().toString().c_str());
  }

  if (!wifiUp() || endpoint.isEmpty()) return;
  if ((int32_t)(now - next_upload_ms) < 0) return;

  if (spool->oldest().isEmpty()) {
    // Nothing pending is the normal state, not a failure — check again soon
    // and don't let it inflate the failure backoff.
    //
    // CLEAR THE STUCK STATE HERE TOO. tryUploadOldest() is the only other
    // place that clears it, and this branch returns before reaching it — so a
    // head record that was quarantined, `rm`'d or formatted away left
    // `net status` reporting a file that no longer exists as blocking an
    // empty queue, for ever.
    stuck_n = 0;
    stuck_path = "";
    backoff_ms = BACKOFF_MIN_MS;
    next_upload_ms = now + 2000;
    return;
  }
  if (tryUploadOldest()) {
    stuck_n = 0;
    stuck_path = "";
    backoff_ms = BACKOFF_MIN_MS;
    next_upload_ms = now;            // more pending? next pass takes it
  } else {
    next_upload_ms = now + backoff_ms;
    backoff_ms = min(backoff_ms * 2, BACKOFF_MAX_MS);
    // ONLY BLAME THE RECORD WHEN THE FAILURE WAS ABOUT THE RECORD.
    //
    // stuck_n counts consecutive failures of any kind — a 503, a server that
    // is simply down, LittleFS.open failing. (Not WiFi being down: tick()
    // returns above when !wifiUp(), so those never reach the counter.) A draft announced all
    // of them as "%s is blocking every record behind it. Inspect it with
    // 'dump %s'", so during an ordinary two-hour outage the operator was told
    // a blameless shot was poisoning the queue. The plausible next action is
    // `rm`, which means a diagnostic that can cause the data loss it exists
    // to prevent.
    if (stuck_path.length() && ++stuck_n % 20 == 0) {
      if (last_failure_was_about_the_record)
        Serial.printf("# net: STUCK — %s has failed %lu times in a row and the"
                      " server keeps refusing THIS record, so it is blocking the"
                      " %u behind it. Inspect it with 'dump %s'.\n",
                      stuck_path.c_str(), (unsigned long)stuck_n,
                      (unsigned)(spool ? spool->pendingCount() : 0),
                      stuck_path.c_str());
      else
        Serial.printf("# net: %u record(s) pending, %lu consecutive upload"
                      " failures — still retrying. Nothing is wrong with %s;"
                      " the server or the network is unreachable.\n",
                      (unsigned)(spool ? spool->pendingCount() : 0),
                      (unsigned long)stuck_n, stuck_path.c_str());
    }
  }
}

bool Net::tryUploadOldest() {
  String path = spool->oldest();
  // Head-of-line tracking. (A draft claimed this "rides on the oldest() call
  // we already make"; it does not — Net::tick calls oldest() separately for
  // its empty check, so there are still two directory walks per tick. The
  // placement is fine, the justification was wrong.)
  if (path != stuck_path) { stuck_path = path; stuck_n = 0; }
  last_failure_was_about_the_record = false;
  if (path.isEmpty()) return false;

  File f = LittleFS.open(path, "r");
  if (!f) return false;

  // Validate from the prelude, NOT by reading sizeof(spool_header_t). A
  // record written by an older firmware is shorter than the current struct
  // and must still upload — the server decodes every version, and the file
  // is POSTed verbatim either way.
  spool_prelude_t p;
  if (f.read((uint8_t*)&p, sizeof(p)) != sizeof(p) || p.magic != SPOOL_MAGIC ||
      p.header_len < SPOOL_HEADER_LEN_V1 || f.size() < p.header_len) {
    f.close();
    Serial.printf("# net: %s is not a valid record, quarantining\n", path.c_str());
    last_failure_was_about_the_record = true;
    LittleFS.mkdir("/spool/bad");
    LittleFS.rename(path, "/spool/bad/" + path.substring(path.lastIndexOf('/') + 1));
    return false;
  }
  // boot_id and seq sit at offsets 8 and 12, below every field any format
  // bump has inserted, so they are safe to read from any version.
  uint32_t boot_id = 0, seq = 0;
  f.seek(8);  f.read((uint8_t*)&boot_id, 4);
  f.seek(12); f.read((uint8_t*)&seq, 4);
  f.seek(0);

  char record_id[32];
  snprintf(record_id, sizeof(record_id), "%08lx-%lu",
           (unsigned long)boot_id, (unsigned long)seq);

  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(5000);
  http.begin(endpoint + "/api/v1/ingest");
  http.addHeader("Content-Type", "application/octet-stream");
  http.addHeader("X-Device-Id", WiFi.macAddress());
  http.addHeader("X-Record", record_id);
  // A 2xx ALONE DOES NOT PROVE THE RECORD WAS STORED, so collect the marker
  // the ingest handler sets and require it before deleting anything. The
  // server's SPA fallback answers ANY unmatched path with 200 and
  // index.html, so an endpoint with a typo, a stale prefix, or no path at
  // all reads as success — and this function's next line is the only copy of
  // the shot being erased, at one record per retry until the spool is empty.
  // Probed on a running server 2026-10-12: POST to a wrong /api path
  // returned 200 with an HTML body. The server now 404s unknown /api paths
  // too; this covers an endpoint pointed at the site root, which that cannot.
  static const char* kIngestMarker = "X-Espressolog-Ingest";
  const char* collect[] = { kIngestMarker };
  http.collectHeaders(collect, 1);
  int code = http.sendRequest("POST", &f, f.size());
  bool from_ingest = http.header(kIngestMarker).length() > 0;
  http.end();
  f.close();

  if (code >= 200 && code < 300 && !from_ingest) {
    // Looks like success and is not. Keep the record and say so loudly —
    // this is a configuration error, not a transient one, and retrying will
    // not fix it, but deleting would be unrecoverable.
    Serial.printf("# net: %s got %d from something that is NOT the ingest"
                  " endpoint — check `endpoint`. Record KEPT.\n",
                  path.c_str(), code);
    last_failure_was_about_the_record = true;   // configuration, not weather
    return false;
  }

  // CLASSIFY THE FAILURE BEFORE REPORTING IT. A draft set this on exactly one
  // path (2xx-without-marker), so 400, 403, 413 and a bare 422 all reported
  // "the server or the network is unreachable" — and net.h names 413
  // specifically as the case that stalls the queue until the spool fills. The
  // one failure the counter exists to surface was the one it blamed on the
  // weather, telling the operator to wait out a condition that never clears.
  //
  // 408 and 429 are the genuinely transient 4xx; everything else at or above
  // 400 is about this request or this record and will not fix itself.
  if (code >= 400 && code != 408 && code != 429)
    last_failure_was_about_the_record = true;

  if (code >= 200 && code < 300) {
    // Delete only now: a lost 200 means a retry, and the server dedupes on
    // (device, boot_id, seq). Never the other way around.
    LittleFS.remove(path);
    Serial.printf("# net: uploaded %s (%s) -> %d, deleted\n", path.c_str(), record_id, code);
    return true;
  }
  // QUARANTINE ONLY ON A DELIBERATE REJECTION OF THIS RECORD'S CONTENT.
  //
  // This was `400 <= code < 500` until 2026-10-12, which is far too wide.
  // 401 and 403 are an auth proxy, 404 is a wrong route after a redeploy,
  // 408 is a timeout, 413 is a body limit and 429 is rate limiting — every
  // one of them transient or a configuration slip, and every one of them
  // moved a perfectly good record into /spool/bad, where NOTHING ever
  // re-uploads it. One wrong endpoint could silently bin a whole backlog.
  //
  // **422 ONLY.** That is the single status the ingest handler uses as a
  // verdict on this record's content ("decode: …"). A draft of this also
  // quarantined 400 on the stated grounds that it meant "malformed request";
  // it does not — the server returned 400 when io.ReadAll failed, i.e. when
  // the connection dropped mid-POST, which is a WiFi hiccup and succeeds on
  // retry. Quarantining it binned a good shot on an ordinary glitch, which
  // is the failure class this whole change exists to close. The server now
  // answers 503 there, and this no longer trusts 400 either.
  //
  // Everything else 4xx is about the path, the proxy or the moment: retry.
  // Require the marker here too. Quarantine is a one-way door — nothing
  // re-uploads from /spool/bad and `format` destroys it — so it must be as
  // well guarded as deletion is. A 422 from anything that is not this
  // server's ingest handler is not a verdict on this record.
  if (code == 422 && from_ingest) {
    Serial.printf("# net: %s rejected with %d, quarantining\n", path.c_str(), code);
    LittleFS.mkdir("/spool/bad");
    LittleFS.rename(path, "/spool/bad/" + path.substring(path.lastIndexOf('/') + 1));
    return false;
  }
  Serial.printf("# net: upload %s failed (%d), will retry\n", path.c_str(), code);
  return false;
}

void Net::status(Stream& out) {
  out.printf("# net: wifi=%s ip=%s rssi=%d time=%s live=%s endpoint=%s\n",
             wifiUp() ? "up" : "down",
             wifiUp() ? WiFi.localIP().toString().c_str() : "-",
             wifiUp() ? WiFi.RSSI() : 0,
             timeValid() ? "synced" : "not synced",
             ws_up ? "connected" : "down",
             endpoint.c_str());
  // Surface the head-of-line condition HERE, not only as a periodic Serial
  // line. The 0d experiment plan calls for running with the console detached,
  // and a stalled queue is invisible otherwise — the device looks healthy and
  // simply stops delivering shots.
  // A lower floor than the periodic serial line on purpose. That one speaks
  // at 20 consecutive failures, which at BACKOFF_MAX_MS is ~19 minutes; this
  // is a command the operator typed, so answer it sooner. (A draft claimed
  // the two had "the same floor" — they do not, and should not.)
  if (stuck_n >= 5)
    out.printf("# net: %s has failed %lu times in a row — %s\n",
               stuck_path.c_str(), (unsigned long)stuck_n,
               last_failure_was_about_the_record
                 ? "the server keeps refusing THIS record; it is blocking the queue"
                 : "still retrying; the server or network is unreachable");
}
