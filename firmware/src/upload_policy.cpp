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

  // EVERYTHING ELSE RETRIES. Notably 400, which this server returns when
  // io.ReadAll fails — a connection dropped mid-POST, which succeeds on
  // retry. Also 401/403 (an auth proxy), 404 (a wrong route), 408, 413, 429
  // and every 5xx. Keeping a good shot behind a loud, visible stall beats
  // one-way-dooring it.
  return UploadVerdict::Retry;
}

bool verdictBlamesTheRecord(UploadVerdict v) {
  return v == UploadVerdict::Quarantine || v == UploadVerdict::KeepAndWarn;
}
