#pragma once
#include <Arduino.h>
#include "detector.h"

// On-flash / on-wire record header. The upload (stage 4) POSTs the spool file
// verbatim, so this struct IS the wire format — self-describing, versioned,
// CRC'd. Fields for stages 4–5 (wall clock, weighing records) exist now so
// the format never changes underneath already-spooled records.
//
// Derived shot fields here don't violate invariant 3 (store raw, not
// derived): the raw samples travel in the same record and detector_version
// makes them recomputable — they exist so the server can populate `shot`
// without re-running a detector on day one.
typedef struct __attribute__((packed)) {
  uint32_t magic;              // 'ESPL' = 0x4C505345
  uint8_t  format_version;     // 1
  uint8_t  record_type;        // 1 = shot, 2 = weighing
  uint16_t header_len;         // sizeof(spool_header_t); forward-compat skip
  uint32_t boot_id;            // + seq = idempotency key
  uint32_t seq;
  uint64_t started_at_unix_ms; // 0 until SNTP (stage 4)
  uint32_t started_at_millis;  // always valid; relative ordering within boot
  int16_t  tz_offset_min;
  uint8_t  time_valid;         // 1 = unix_ms trustworthy
  uint8_t  scale_role;         // 0 yield, 1 dose
  uint8_t  scale_mac[6];       // → equipment.ble_address lookup
  uint8_t  flags;              // bit0 FAULT, bit1 TRUNCATED
  char     detector_version[8];
  char     firmware_version[16];
  // shot-only (zero for weighing records):
  uint32_t stop_ms;
  int32_t  yield_at_stop_mg;
  int32_t  yield_final_mg;
  int32_t  settle_offset_mg;
  int32_t  peak_flow_mgps;
  int32_t  mean_flow_mgps;
  uint32_t flow_win_start_ms;
  uint32_t flow_win_end_ms;
  // weighing-only (zero for shots):
  int32_t  grams_mg;
  uint32_t stable_ms;
  uint16_t sample_count;       // sample_t records following (0 for weighing)
  uint32_t crc32;              // over header (this field zeroed) + samples
} spool_header_t;

#define SPOOL_FLAG_FAULT     0x01
#define SPOOL_FLAG_TRUNCATED 0x02

// LittleFS spool: one file per record under /spool, written only when no
// shot is in progress (invariant 2), deleted only on confirmed upload
// (invariant 6, stage 4). At >90 % full it refuses new writes and screams —
// silently dropping the oldest record is also "losing a shot".
class Spool {
public:
  bool begin();
  // Both return the written path, or "" on refusal/failure.
  String writeShot(const ShotResult& r, const String& scale_mac);
  String writeWeighing(uint8_t role, const WeighingDetector::Event& ev, const String& scale_mac);
  bool remove(const String& name);
  void list(Stream& out);
  bool dump(const String& name, Stream& out);   // header summary + sample CSV
  String oldest();                              // "" if empty (stage 4 upload order)
  size_t pendingCount();
  bool nearlyFull();

private:
  uint32_t boot_id = 0;
  uint32_t seq = 0;
  bool macToBytes(const String& mac, uint8_t out[6]);
  void fillCommon(spool_header_t& h, uint8_t record_type, uint32_t started_at_ms,
                  uint8_t role, const String& scale_mac);
  String writeFile(char kind, const spool_header_t& h, const sample_t* samples);
};
