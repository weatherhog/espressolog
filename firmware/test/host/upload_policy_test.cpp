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

  // 6. THE FULL TABLE, every cell. Test 1 only asserts that exactly one cell
  //    QUARANTINES, which leaves the other three verdicts unpinned — widening
  //    the success window (code < 300 -> code <= 300) survived it. Assert the
  //    whole mapping so no cell can drift.
  {
    for (int code = -20; code <= 599; code++)
      for (int m = 0; m < 2; m++) {
        UploadVerdict want;
        if (code >= 200 && code < 300) want = m ? UploadVerdict::Delete
                                                : UploadVerdict::KeepAndWarn;
        else if (code == 422 && m)     want = UploadVerdict::Quarantine;
        else                           want = UploadVerdict::Retry;
        assert(classifyResponse(code, m) == want);
      }
    printf("6. every (code, marker) cell maps to the expected verdict OK\n");
  }

  // 7. 413 IS PERMANENT AND MUST SAY SO. net.h names it as the case the stuck
  //    counter exists to surface — a record the server will never accept,
  //    stalling everything behind it until the spool fills. A first version
  //    classified blame from the verdict alone, so 413 (which retries) and
  //    every other permanent 4xx reported "the server or network is
  //    unreachable" and told the operator to wait out a condition that never
  //    clears.
  {
    for (int code : {400, 401, 403, 404, 413, 422}) {
      assert(classifyResponse(code, false) == UploadVerdict::Retry);
      assert(failureIsPermanent(code, UploadVerdict::Retry));
    }
    printf("7. permanent 4xx retry but DO blame the record (413 included) OK\n");
  }

  // 8. ...and the genuinely transient ones must not blame.
  {
    for (int code : {408, 429, 500, 502, 503, -1, -11})
      assert(!failureIsPermanent(code, UploadVerdict::Retry));
    printf("8. 408/429/5xx/negative codes retry WITHOUT blaming the record OK\n");
  }

  // 9. ONLY A VERDICT ABOUT THE RECORD BLAMES THE RECORD. A draft announced
  //    every consecutive failure — including a server simply being down — as
  //    "this record is blocking every record behind it. Inspect it with dump",
  //    whose plausible next step is `rm`.
  {
    assert(failureIsPermanent(422, UploadVerdict::Quarantine));
    assert(failureIsPermanent(200, UploadVerdict::KeepAndWarn));
    assert(!failureIsPermanent(201, UploadVerdict::Delete));
    printf("9. Quarantine and KeepAndWarn always blame; Delete never does OK\n");
  }

  printf("\nall upload policy tests passed\n");
  return 0;
}
