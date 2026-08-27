#include "net.h"
#include "spool.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <time.h>

#if __has_include("secrets.h")
  #include "secrets.h"
#endif

static Preferences net_prefs;

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
}

bool Net::wifiUp() const { return WiFi.status() == WL_CONNECTED; }

bool Net::timeValid() const { return time(nullptr) > 1600000000; }

void Net::tick(uint32_t now, bool detector_quiet) {
  if (!have_creds || !detector_quiet) return;

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
    backoff_ms = BACKOFF_MIN_MS;
    next_upload_ms = now + 2000;
    return;
  }
  if (tryUploadOldest()) {
    backoff_ms = BACKOFF_MIN_MS;
    next_upload_ms = now;            // more pending? next pass takes it
  } else {
    next_upload_ms = now + backoff_ms;
    backoff_ms = min(backoff_ms * 2, BACKOFF_MAX_MS);
  }
}

bool Net::tryUploadOldest() {
  String path = spool->oldest();
  if (path.isEmpty()) return false;

  File f = LittleFS.open(path, "r");
  if (!f) return false;

  spool_header_t h;
  if (f.read((uint8_t*)&h, sizeof(h)) != sizeof(h) || h.magic != 0x4C505345) {
    f.close();
    Serial.printf("# net: %s is not a valid record, quarantining\n", path.c_str());
    LittleFS.mkdir("/spool/bad");
    LittleFS.rename(path, "/spool/bad/" + path.substring(path.lastIndexOf('/') + 1));
    return false;
  }
  f.seek(0);

  char record_id[32];
  snprintf(record_id, sizeof(record_id), "%08lx-%lu",
           (unsigned long)h.boot_id, (unsigned long)h.seq);

  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(5000);
  http.begin(endpoint + "/api/v1/ingest");
  http.addHeader("Content-Type", "application/octet-stream");
  http.addHeader("X-Device-Id", WiFi.macAddress());
  http.addHeader("X-Record", record_id);
  int code = http.sendRequest("POST", &f, f.size());
  http.end();
  f.close();

  if (code >= 200 && code < 300) {
    // Delete only now: a lost 200 means a retry, and the server dedupes on
    // (device, boot_id, seq). Never the other way around.
    LittleFS.remove(path);
    Serial.printf("# net: uploaded %s (%s) -> %d, deleted\n", path.c_str(), record_id, code);
    return true;
  }
  if (code >= 400 && code < 500) {
    // The server understood us and said no — retrying forever won't help,
    // but the record may still matter. Quarantine for a human.
    Serial.printf("# net: %s rejected with %d, quarantining\n", path.c_str(), code);
    LittleFS.mkdir("/spool/bad");
    LittleFS.rename(path, "/spool/bad/" + path.substring(path.lastIndexOf('/') + 1));
    return false;
  }
  Serial.printf("# net: upload %s failed (%d), will retry\n", path.c_str(), code);
  return false;
}

void Net::status(Stream& out) {
  out.printf("# net: wifi=%s ip=%s rssi=%d time=%s endpoint=%s\n",
             wifiUp() ? "up" : "down",
             wifiUp() ? WiFi.localIP().toString().c_str() : "-",
             wifiUp() ? WiFi.RSSI() : 0,
             timeValid() ? "synced" : "not synced",
             endpoint.c_str());
}
