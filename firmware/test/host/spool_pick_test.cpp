#undef NDEBUG   // these suites assert with bare assert(); see the Makefile
#include <cassert>
#include <cstdio>
#include <cstring>
#include "../../src/spool_pick.h"

struct Entry { const char* name; bool is_dir; };

static OldestPicker walk(const Entry* e, size_t n) {
  OldestPicker p;
  for (size_t i = 0; i < n; i++) p.consider(e[i].name, e[i].is_dir);
  return p;
}

int main() {
  // 1. THE WEDGE. /spool/bad is a directory inside /spool and "bad" sorts
  //    before both "s-" and "w-", so without the is_dir skip the first
  //    quarantine stopped every upload for ever — silently.
  {
    Entry e[] = {{"bad", true}, {"s-feed0001-0001.bin", false}, {"w-feed0001-0002.bin", false}};
    auto p = walk(e, 3);
    assert(p.have());
    assert(strcmp(p.best(), "s-feed0001-0001.bin") == 0);
    printf("1. a quarantine directory never wins the pick OK\n");
  }

  // 2. Lexicographic order among real files is unchanged, whatever order the
  //    filesystem hands them over in.
  {
    Entry e[] = {{"w-feed0001-0009.bin", false}, {"s-feed0001-0003.bin", false},
                 {"s-feed0001-0001.bin", false}};
    assert(strcmp(walk(e, 3).best(), "s-feed0001-0001.bin") == 0);
    Entry r[] = {{"s-feed0001-0001.bin", false}, {"s-feed0001-0003.bin", false},
                 {"w-feed0001-0009.bin", false}};
    assert(strcmp(walk(r, 3).best(), "s-feed0001-0001.bin") == 0);
    printf("2. the lexicographic minimum wins regardless of walk order OK\n");
  }

  // 3. A spool containing ONLY the quarantine directory reads as EMPTY, so
  //    Net::tick takes its nothing-pending branch instead of retrying a
  //    directory for ever — which is exactly what the wedge did.
  {
    Entry e[] = {{"bad", true}};
    auto p = walk(e, 1);
    assert(!p.have());
    assert(p.records() == 0);
    printf("3. a spool holding only /spool/bad is empty, not stuck OK\n");
  }

  // 4. Genuinely empty.
  {
    OldestPicker p;
    assert(!p.have());
    assert(p.records() == 0);
    printf("4. an empty spool returns nothing OK\n");
  }

  // 5. The record count agrees with the pick about what a record is.
  //    `format`'s confirmation prompt is the only thing between the operator
  //    and an irreversible erase, so it must not undercount.
  {
    Entry e[] = {{"bad", true}, {"s-feed0001-0001.bin", false},
                 {"w-feed0001-0002.bin", false}, {"nested", true}};
    assert(walk(e, 4).records() == 2);
    printf("5. the record count and the pick agree on what a record is OK\n");
  }

  // 6. NO CAP ON HOW MANY ENTRIES A PASS CONSIDERS. A first version took an
  //    array with a fixed bound, which silently changed which file uploads
  //    next once the spool held more than that — the spool holds ~490 at 90 %
  //    full. Feed more than any plausible cap and check the true minimum wins.
  {
    OldestPicker p;
    char buf[OldestPicker::MAX_NAME];
    for (int i = 500; i >= 1; i--) {          // worst case: minimum arrives last
      snprintf(buf, sizeof buf, "s-feed0001-%04d.bin", i);
      p.consider(buf, false);
    }
    assert(strcmp(p.best(), "s-feed0001-0001.bin") == 0);
    assert(p.records() == 500);
    printf("6. 500 entries, the true minimum still wins (no cap) OK\n");
  }

  // 7. MAX_NAME MUST ACCOMMODATE THE LONGEST NAME THIS SPOOL ACTUALLY
  //    WRITES. A review found every name in this corpus was 15 characters,
  //    with no `.bin`, so shrinking MAX_NAME to 16 survived the whole suite —
  //    and the header's comment quoted a format string that does not exist.
  //    Pin both ends of the real range: 19 chars minimum, 25 at a full
  //    uint32_t seq. If MAX_NAME were ever tightened to the remembered "21",
  //    every record with seq >= 1000000 would silently stop uploading.
  {
    OldestPicker p;
    p.consider("s-feed0001-4294967295.bin", false);   // 25 chars, the maximum
    assert(p.have());
    assert(p.records() == 1);
    OldestPicker q;
    q.consider("s-feed0001-0001.bin", false);         // 19 chars, the minimum
    assert(q.have());
    printf("7. the longest and shortest real spool names are both accepted OK\n");
  }

  // 8. A name longer than the spool ever writes is ignored rather than
  //    truncated into a wrong comparison.
  {
    Entry e[] = {{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", false},
                 {"s-feed0001-0001.bin", false}};
    assert(strcmp(walk(e, 2).best(), "s-feed0001-0001.bin") == 0);
    printf("8. an over-long name is skipped, not truncated OK\n");
  }

  printf("\nall spool pick tests passed\n");
  return 0;
}
