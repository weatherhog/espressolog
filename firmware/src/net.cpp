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

  // Validate from the prelude, NOT by reading sizeof(spool_header_t). A
  // record written by an older firmware is shorter than the current struct
  // and must still upload — the server decodes every version, and the file
  // is POSTed verbatim either way.
  spool_prelude_t p;
  if (f.read((uint8_t*)&p, sizeof(p)) != sizeof(p) || p.magic != SPOOL_MAGIC ||
      p.header_len < SPOOL_HEADER_LEN_V1 || f.size() < p.header_len) {
    f.close();
    Serial.printf("# net: %s is not a valid record, quarantining\n", path.c_str());
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
  out.printf("# net: wifi=%s ip=%s rssi=%d time=%s live=%s endpoint=%s\n",
             wifiUp() ? "up" : "down",
             wifiUp() ? WiFi.localIP().toString().c_str() : "-",
             wifiUp() ? WiFi.RSSI() : 0,
             timeValid() ? "synced" : "not synced",
             ws_up ? "connected" : "down",
             endpoint.c_str());
}
