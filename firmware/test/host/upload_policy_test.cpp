#undef NDEBUG   // these suites assert with bare assert(); see the Makefile
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "../../src/upload_policy.h"

int main() {
  // 1. EXACTLY ONE CELL IN THE WHOLE TABLE QUARANTINES. Sweep every status
  //    this device could see, both marker states. A draft quarantined all of
  //    4xx; another added 400. Both binned shots that would have uploaded.
  {
    int quarantining = 0;
    for (int code = 100; code <= 599; code++)
      for (int m = 0; m < 2; m++)
        if (classifyResponse(code, m) == UploadVerdict::Quarantine) {
          quarantining++;
          assert(code == 422 && m == 1);
        }
    assert(quarantining == 1);
    printf("1. exactly one of 1000 (code, marker) cells quarantines: 422+marker OK\n");
  }

  // 2. 400 RETRIES. The server returns it when the request body read fails —
  //    a dropped TCP connection, not a verdict. Quarantining it destroyed a
  //    good shot on an ordinary WiFi hiccup.
  {
    assert(classifyResponse(400, true) == UploadVerdict::Retry);
    assert(classifyResponse(400, false) == UploadVerdict::Retry);
    printf("2. 400 retries, with or without the marker OK\n");
  }

  // 3. A 2xx WITHOUT THE MARKER NEVER DELETES. This is the live bug: a
  //    misdirected endpoint reaches the SPA fallback, which answers 200 with
  //    HTML, and the device deleted its only copy of each shot in turn.
  {
    for (int code : {200, 201, 202, 204, 299}) {
      assert(classifyResponse(code, false) == UploadVerdict::KeepAndWarn);
      assert(classifyResponse(code, true) == UploadVerdict::Delete);
    }
    printf("3. 2xx deletes only with the marker; without it, keep and warn OK\n");
  }

  // 4. The transient and path-level codes all retry rather than bin.
  {
    for (int code : {401, 403, 404, 408, 413, 429, 500, 502, 503})
      assert(classifyResponse(code, false) == UploadVerdict::Retry);
    printf("4. 401/403/404/408/413/429/5xx all retry OK\n");
  }

  // 5. Negative HTTPClient codes (connection refused, timeout) retry.
  {
    for (int code : {-1, -11, -4})
      assert(classifyResponse(code, false) == UploadVerdict::Retry);
    printf("5. negative HTTPClient codes retry OK\n");
  }

  // 6. ONLY A VERDICT ABOUT THE RECORD BLAMES THE RECORD. A draft announced
  //    every consecutive failure — including a server simply being down — as
  //    "this record is blocking every record behind it. Inspect it with dump",
  //    whose plausible next step is `rm`.
  {
    assert(verdictBlamesTheRecord(UploadVerdict::Quarantine));
    assert(verdictBlamesTheRecord(UploadVerdict::KeepAndWarn));
    assert(!verdictBlamesTheRecord(UploadVerdict::Retry));
    assert(!verdictBlamesTheRecord(UploadVerdict::Delete));
    printf("6. only Quarantine and KeepAndWarn blame the record OK\n");
  }

  printf("\nall upload policy tests passed\n");
  return 0;
}
