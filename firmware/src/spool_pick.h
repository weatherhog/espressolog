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
// cap. Streaming keeps the walk unbounded, as it was.
//
// ONE DELIBERATE DIVERGENCE from the loop this replaced: a name of MAX_NAME
// characters or more is skipped. The old loop considered any non-directory
// entry, so a foreign file with a long name became `best`, failed the prelude
// check in tryUploadOldest and was quarantined — i.e. it cleared itself. Now
// it is simply never picked. No record this spool writes can reach that
// length (see MAX_NAME), so no shot is affected — and since Spool::
// pendingCount() now counts through this same object, the pick and the count
// agree about it rather than disagreeing, which they briefly did.
class OldestPicker {
public:
  // MEASURED FROM THE FORMAT STRING THAT ACTUALLY WRITES THEM, which is
  // spool.cpp's `"%s/%c-%08lx-%04lu.bin"`. The basename is therefore
  // "s-feed0001-0001.bin" at 19 characters, and 25 at a full uint32_t seq
  // ("s-feed0001-4294967295.bin"). 32 leaves room without being generous.
  //
  // A draft of this comment quoted "s-%08lx-%lu … at most 21 characters" —
  // a format string that appears nowhere in the code, dropping both `.bin`
  // and the %04 width. Harmless at 32, but tightening MAX_NAME to that
  // remembered 21 would make every record with seq >= 1000000 invisible to
  // oldest() and it would never upload. Silently. Test 7 pins the real
  // lengths so that cannot happen.
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
