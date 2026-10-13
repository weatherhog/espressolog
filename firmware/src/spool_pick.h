#pragma once
#include <stddef.h>

// Which spooled file uploads next. Pure logic, no LittleFS — spool.cpp owns
// the directory walk, this owns the choice.
//
// Split out because the choice, not the walk, is where the bug was. Until
// 2026-10-12 oldest() took the lexicographic minimum over everything
// openNextFile() returned, directories included — and the quarantine path
// creates /spool/bad INSIDE /spool. "bad" sorts before both "s-" and "w-", so
// the FIRST quarantine wedged the queue permanently: oldest() returned that
// directory for ever, reading it yielded 0 bytes, which re-entered the
// quarantine branch, the rename failed, and the uploader backed off and
// repeated. Every later shot then sat on flash and never reached the server.
// Invariant 6, with no symptom on the device and nothing in any log.
//
// It survived weeks of use and was found by reading, not by failing.
//
// INCREMENTAL ON PURPOSE. A first version of this took an array and a count,
// which forced a cap on how many entries one pass could consider — and that
// silently changed which file uploads next once the spool held more than the
// cap. Streaming keeps the behaviour identical to the walk it replaced.
class OldestPicker {
public:
  // Spool names are "s-%08lx-%lu" / "w-%08lx-%lu" — at most 21 characters.
  static constexpr size_t MAX_NAME = 32;

  // Offer one directory entry. Directories are skipped; that is the fix.
  void consider(const char* name, bool is_dir);

  bool        have() const { return has_best; }
  const char* best() const { return best_name; }   // valid only while have()
  size_t      records() const { return n_records; }

private:
  char   best_name[MAX_NAME] = {};
  bool   has_best  = false;
  size_t n_records = 0;
};
