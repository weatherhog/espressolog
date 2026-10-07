#pragma once
#include <Arduino.h>
#include "detector.h"
#include "machine_context.h"

// On-flash / on-wire record header. The upload (stage 4) POSTs the spool file
// verbatim, so this struct IS the wire format — self-describing, versioned,
// CRC'd. Fields for stages 4–5 (wall clock, weighing records) exist now so
// the format never changes underneath already-spooled records.
//
// Derived shot fields here don't violate invariant 3 (store raw, not
// derived): the raw samples travel in the same record and detector_version
// makes them recomputable — they exist so the server can populate `shot`
// without re-running a detector on day one.
//
// FORMAT VERSION 2 (2026-10-03) added the two machine-sourced fields below
// and one flag bit. Version 1 records still exist, both spooled and as
// fixtures, so the server decodes BOTH — see server/internal/record. The
// original intent was that this struct would never change; it changed
// because 0e produced facts the machine knows and the logger did not.
typedef struct __attribute__((packed)) {
  uint32_t magic;              // 'ESPL' = 0x4C505345
  uint8_t  format_version;     // 2 — see the version note above
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
  uint8_t  flags;              // bit0 FAULT, bit1 TRUNCATED, bit2 REJECT, bit3 SETPOINT_TOUCHED
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
  // --- format v2: read off the machine's own front display (0e) ---
  // The boiler temperature the machine was showing immediately before the
  // pump started. INT16_MIN = not available: the display bus was off, the
  // tap was not wired, or the reading was stale. NOT the same thing as
  // brew_temp_c, which is the PID setpoint someone typed in.
  int16_t  boiler_temp_start_dc;
  // The machine's OWN shot timer, in tenths of a second — its pump-on to
  // pump-off measurement, which is truer than the scale's plateau. 0 = not
  // available. Note it does NOT say which switch direction ran: 1Cup and
  // 2Cup are byte-identical on the display bus, so that still needs 0c.
  uint16_t machine_timer_dl;
  // weighing-only (zero for shots):
  int32_t  grams_mg;
  uint32_t stable_ms;
  uint16_t sample_count;       // sample_t records following (0 for weighing)
  uint32_t crc32;              // over header (this field zeroed) + samples
} spool_header_t;

// The first 8 bytes are identical in every format version, by design —
// header_len's own comment has always promised "forward-compat skip" and
// until 2026-10-03 nothing implemented it. Read THIS first, then decide.
//
// This matters: a v1 weighing record is a bare 109-byte header, so a
// reader doing `f.read(&h, sizeof(h))` against the 113-byte v2 struct gets
// a short read, calls the record invalid and quarantines it. That is
// losing a shot's dose, which invariant 6 forbids.
typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint8_t  format_version;
  uint8_t  record_type;
  uint16_t header_len;
} spool_prelude_t;

#define SPOOL_MAGIC          0x4C505345
#define SPOOL_HEADER_LEN_V1  109
#define SPOOL_HEADER_LEN_V2  113   // == sizeof(spool_header_t)

#define SPOOL_FLAG_FAULT     0x01
#define SPOOL_FLAG_TRUNCATED 0x02
#define SPOOL_FLAG_REJECT    0x04   // detected but <5 g: kept for visibility, excluded server-side
// v2. Someone entered temperature programming or edited a value while this
// shot was being recorded. A setpoint change silently invalidates a
// noise-floor run, and it is over long before anyone reads a level — so it
// is latched, not sampled.
#define SPOOL_FLAG_SETPOINT_TOUCHED 0x08
// v2 (0d). The flowmeter tap was live while this shot was recorded, so each
// sample's inlet_pulses is a MEASUREMENT. Without it the field is the
// placeholder 0 it has carried since 0a, and a reader cannot tell "no water
// moved" from "nothing was counting" — the same distinction INT16_MIN draws
// for temperature.
//
// A flag rather than a format bump on purpose: the field has existed in
// sample_t since 0a precisely so the flash layout would not change when 0d
// arrived, and it did not. What changed is whether the bytes mean anything,
// which is exactly what a flag is for. Format v2 records written by firmware
// 0.8.x carry placeholder zeros and no flag, so they read correctly as
// "the machine did not say".
#define SPOOL_FLAG_FLOW_TAP 0x10


// LittleFS spool: one file per record under /spool, written only when no
// shot is in progress (invariant 2), deleted only on confirmed upload
// (invariant 6, stage 4). At >90 % full it refuses new writes and screams —
// silently dropping the oldest record is also "losing a shot".
class Spool {
public:
  bool begin();
  // Both return the written path, or "" on refusal/failure.
  String writeShot(const ShotResult& r, const String& scale_mac,
                   const MachineContext& mc = MachineContext{},
                   bool flow_tap = false);
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
