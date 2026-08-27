#include "spool.h"
#include <LittleFS.h>
#include <esp_random.h>
#include <esp_rom_crc.h>
#include <string.h>

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

String Spool::writeShot(const ShotResult& r, const String& scale_mac) {
  if (nearlyFull()) {
    // Refusing is deliberate: dropping the oldest silently is also losing a
    // shot. ~90 % full means months of failed uploads — that needs a human.
    Serial.println("# SPOOL >90% FULL — record NOT written. Fix the uploads.");
    return "";
  }

  spool_header_t h = {};
  h.magic = 0x4C505345;
  h.format_version = 1;
  h.record_type = 1;
  h.header_len = sizeof(h);
  h.boot_id = boot_id;
  h.seq = seq++;
  h.started_at_unix_ms = 0;   // SNTP arrives in stage 4
  h.started_at_millis = r.started_at_ms;
  h.tz_offset_min = 0;
  h.time_valid = 0;
  h.scale_role = 0;
  macToBytes(scale_mac, h.scale_mac);
  h.flags = (r.fault ? SPOOL_FLAG_FAULT : 0) | (r.truncated ? SPOOL_FLAG_TRUNCATED : 0);
  strlcpy(h.detector_version, DETECTOR_VERSION, sizeof(h.detector_version));
  strlcpy(h.firmware_version, FIRMWARE_VERSION, sizeof(h.firmware_version));
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

  char name[48];
  snprintf(name, sizeof(name), "%s/s-%08lx-%04lu.bin", DIR,
           (unsigned long)boot_id, (unsigned long)h.seq);
  File f = LittleFS.open(name, "w");
  if (!f) { Serial.printf("# spool: open failed: %s\n", name); return ""; }
  size_t ok = f.write((const uint8_t*)&h, sizeof(h));
  ok += f.write((const uint8_t*)r.samples, (size_t)r.sample_count * sizeof(sample_t));
  f.close();
  if (ok != sizeof(h) + (size_t)r.sample_count * sizeof(sample_t)) {
    Serial.printf("# spool: short write, removing %s\n", name);
    LittleFS.remove(name);
    return "";
  }
  return String(name);
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
