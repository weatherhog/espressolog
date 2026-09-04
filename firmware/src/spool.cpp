#include "spool.h"
#include <LittleFS.h>
#include <esp_random.h>
#include <esp_rom_crc.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

extern const char* FIRMWARE_VERSION;
extern const char* DETECTOR_VERSION;

static const char* DIR = "/spool";

bool Spool::begin() {
  boot_id = esp_random();
  seq = 0;
  if (!LittleFS.exists(DIR) && !LittleFS.mkdir(DIR)) return false;
  return true;
}

bool Spool::nearlyFull() {
  return LittleFS.usedBytes() * 10 > LittleFS.totalBytes() * 9;
}

size_t Spool::pendingCount() {
  size_t n = 0;
  File d = LittleFS.open(DIR);
  for (File f = d.openNextFile(); f; f = d.openNextFile()) n++;
  return n;
}

bool Spool::macToBytes(const String& mac, uint8_t out[6]) {
  memset(out, 0, 6);
  if (mac.length() != 17) return false;
  for (int i = 0; i < 6; i++) {
    out[i] = (uint8_t)strtoul(mac.substring(i * 3, i * 3 + 2).c_str(), nullptr, 16);
  }
  return true;
}

void Spool::fillCommon(spool_header_t& h, uint8_t record_type, uint32_t started_at_ms,
                       uint8_t role, const String& scale_mac) {
  h.magic = 0x4C505345;
  h.format_version = 1;
  h.record_type = record_type;
  h.header_len = sizeof(h);
  h.boot_id = boot_id;
  h.seq = seq++;
  h.started_at_millis = started_at_ms;
  // Wall clock, if SNTP has synced this boot. The event started in the past,
  // so anchor: now_unix − (millis-now − millis-at-start).
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  if (tv.tv_sec > 1600000000) {
    uint64_t now_ms = (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    h.started_at_unix_ms = now_ms - (millis() - started_at_ms);
    h.time_valid = 1;
    struct tm g;
    time_t t = tv.tv_sec;
    gmtime_r(&t, &g);
    g.tm_isdst = -1;
    h.tz_offset_min = (int16_t)((t - mktime(&g)) / 60);
  }
  h.scale_role = role;
  macToBytes(scale_mac, h.scale_mac);
  strlcpy(h.detector_version, DETECTOR_VERSION, sizeof(h.detector_version));
  strlcpy(h.firmware_version, FIRMWARE_VERSION, sizeof(h.firmware_version));
}

String Spool::writeFile(char kind, const spool_header_t& h, const sample_t* samples) {
  char name[48];
  snprintf(name, sizeof(name), "%s/%c-%08lx-%04lu.bin", DIR, kind,
           (unsigned long)h.boot_id, (unsigned long)h.seq);
  File f = LittleFS.open(name, "w");
  if (!f) { Serial.printf("# spool: open failed: %s\n", name); return ""; }
  size_t want = sizeof(h) + (size_t)h.sample_count * sizeof(sample_t);
  size_t ok = f.write((const uint8_t*)&h, sizeof(h));
  if (h.sample_count) ok += f.write((const uint8_t*)samples, (size_t)h.sample_count * sizeof(sample_t));
  f.close();
  if (ok != want) {
    Serial.printf("# spool: short write, removing %s\n", name);
    LittleFS.remove(name);
    return "";
  }
  return String(name);
}

String Spool::writeShot(const ShotResult& r, const String& scale_mac) {
  if (nearlyFull()) {
    // Refusing is deliberate: dropping the oldest silently is also losing a
    // shot. ~90 % full means months of failed uploads — that needs a human.
    Serial.println("# SPOOL >90% FULL — record NOT written. Fix the uploads.");
    return "";
  }

  spool_header_t h = {};
  fillCommon(h, 1, r.started_at_ms, 0, scale_mac);
  h.flags = (r.fault ? SPOOL_FLAG_FAULT : 0) | (r.truncated ? SPOOL_FLAG_TRUNCATED : 0)
          | ((!r.valid && !r.fault) ? SPOOL_FLAG_REJECT : 0);
  h.stop_ms = r.stop_ms;
  h.yield_at_stop_mg = r.yield_at_stop_mg;
  h.yield_final_mg = r.yield_final_mg;
  h.settle_offset_mg = r.settle_offset_mg;
  h.peak_flow_mgps = r.peak_flow_mgps;
  h.mean_flow_mgps = r.mean_flow_mgps;
  h.flow_win_start_ms = r.flow_win_start_ms;
  h.flow_win_end_ms = r.flow_win_end_ms;
  h.sample_count = r.sample_count;

  h.crc32 = 0;
  uint32_t crc = esp_rom_crc32_le(0, (const uint8_t*)&h, sizeof(h));
  crc = esp_rom_crc32_le(crc, (const uint8_t*)r.samples, (size_t)r.sample_count * sizeof(sample_t));
  h.crc32 = crc;

  return writeFile('s', h, r.samples);
}

String Spool::writeWeighing(uint8_t role, const WeighingDetector::Event& ev, const String& scale_mac) {
  if (nearlyFull()) {
    Serial.println("# SPOOL >90% FULL — weighing NOT written.");
    return "";
  }
  spool_header_t h = {};
  fillCommon(h, 2, ev.started_at_ms, role, scale_mac);
  h.grams_mg = ev.grams_mg;
  h.stable_ms = ev.stable_ms;
  h.sample_count = 0;

  h.crc32 = 0;
  h.crc32 = esp_rom_crc32_le(0, (const uint8_t*)&h, sizeof(h));

  return writeFile('w', h, nullptr);
}

bool Spool::remove(const String& name) {
  String path = name.startsWith("/") ? name : String(DIR) + "/" + name;
  return LittleFS.remove(path);
}

String Spool::oldest() {
  // Lexicographic min works within a boot; across boots order is arbitrary
  // but every file still gets its turn (stage 4 uploads until empty).
  String best;
  File d = LittleFS.open(DIR);
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    String n = String(f.name());
    if (best.isEmpty() || n < best) best = n;
  }
  return best.isEmpty() ? best : String(DIR) + "/" + best;
}

void Spool::list(Stream& out) {
  size_t n = 0, bytes = 0;
  File d = LittleFS.open(DIR);
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    out.printf("# %s  %u bytes\n", f.name(), (unsigned)f.size());
    n++; bytes += f.size();
  }
  out.printf("# %u record(s), %u bytes; fs %u/%u KB\n", (unsigned)n, (unsigned)bytes,
             (unsigned)(LittleFS.usedBytes() / 1024), (unsigned)(LittleFS.totalBytes() / 1024));
}

bool Spool::dump(const String& name, Stream& out) {
  String path = name.startsWith("/") ? name : String(DIR) + "/" + name;
  File f = LittleFS.open(path, "r");
  if (!f) { out.printf("# no such record: %s\n", path.c_str()); return false; }

  spool_header_t h;
  if (f.read((uint8_t*)&h, sizeof(h)) != sizeof(h) || h.magic != 0x4C505345) {
    out.println("# bad header");
    f.close();
    return false;
  }
  out.printf("# %s: type=%u boot=%08lx seq=%lu flags=%02x det=%s fw=%s\n",
             path.c_str(), h.record_type, (unsigned long)h.boot_id,
             (unsigned long)h.seq, h.flags, h.detector_version, h.firmware_version);
  out.printf("# stop_ms=%lu at_stop=%ld final=%ld settle=%ld peak=%ld mean=%ld samples=%u\n",
             (unsigned long)h.stop_ms, (long)h.yield_at_stop_mg, (long)h.yield_final_mg,
             (long)h.settle_offset_mg, (long)h.peak_flow_mgps, (long)h.mean_flow_mgps,
             h.sample_count);
  out.println("# t_ms,weight_mg");
  uint32_t crc = 0;
  { spool_header_t hz = h; hz.crc32 = 0; crc = esp_rom_crc32_le(0, (const uint8_t*)&hz, sizeof(hz)); }
  for (uint16_t i = 0; i < h.sample_count; i++) {
    sample_t s;
    if (f.read((uint8_t*)&s, sizeof(s)) != sizeof(s)) { out.println("# TRUNCATED FILE"); break; }
    crc = esp_rom_crc32_le(crc, (const uint8_t*)&s, sizeof(s));
    out.printf("%u,%ld\n", s.t_ms, (long)s.weight_mg);
  }
  f.close();
  out.printf("# crc %s\n", crc == h.crc32 ? "ok" : "MISMATCH");
  return true;
}
