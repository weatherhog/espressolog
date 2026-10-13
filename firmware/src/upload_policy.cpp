#include "upload_policy.h"

UploadVerdict classifyResponse(int code, bool from_ingest) {
  // 2xx WITHOUT THE MARKER IS NOT SUCCESS. The server's SPA fallback answers
  // any unmatched path with index.html and 200, so this is what a misdirected
  // endpoint looks like. Deleting here is unrecoverable; keep and complain.
  if (code >= 200 && code < 300)
    return from_ingest ? UploadVerdict::Delete : UploadVerdict::KeepAndWarn;

  // 422 IS THE ONLY VERDICT ON CONTENT the ingest handler issues, and
  // quarantine is a one-way door — nothing re-uploads from /spool/bad and
  // `format` destroys it. So it must be as well guarded as deletion: a 422
  // from a foreign service or a future proxy is not a statement about this
  // record.
  if (code == 422) return from_ingest ? UploadVerdict::Quarantine
                                      : UploadVerdict::Retry;

  // EVERYTHING ELSE RETRIES. Notably 400: an earlier version of this comment
  // said the server returns it when io.ReadAll fails, which WAS true and was
  // the bug — quarantining it binned a good shot on a dropped connection. The
  // server answers 503 there now (api.go says so in its own comment), and
  // this retries 400 regardless rather than trusting either reading. Also
  // 401/403 (an auth proxy), 404 (a wrong route), 408, 413, 429 and every
  // 5xx. Keeping a good shot behind a loud, visible stall beats
  // one-way-dooring it.
  return UploadVerdict::Retry;
}

bool failureIsPermanent(int code, UploadVerdict v) {
  // A refusal of this record's content, or a 2xx from the wrong endpoint:
  // both are configuration or content, neither clears on its own.
  if (v == UploadVerdict::Quarantine || v == UploadVerdict::KeepAndWarn)
    return true;
  if (v == UploadVerdict::Delete) return false;   // not a failure at all

  // Retrying. 408 (timeout) and 429 (rate limit) really are transient, and so
  // is every 5xx — including the 503 this server returns for a dropped
  // connection, which an earlier predicate wrongly blamed. Every other 4xx is
  // permanent for this request: 401/403 an auth proxy, 404 a wrong route,
  // 413 a body limit, a bare 422 a foreign service.
  return code >= 400 && code < 500 && code != 408 && code != 429;
}
