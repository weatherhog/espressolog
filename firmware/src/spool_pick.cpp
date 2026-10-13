#include "spool_pick.h"
#include <string.h>

void OldestPicker::consider(const char* name, bool is_dir) {
  if (!name || !*name) return;
  if (is_dir) return;          // /spool/bad — the entry that wedged the queue
  if (strlen(name) >= MAX_NAME) return;   // not a name this spool writes

  n_records++;
  if (!has_best || strcmp(name, best_name) < 0) {
    strncpy(best_name, name, MAX_NAME - 1);
    best_name[MAX_NAME - 1] = '\0';
    has_best = true;
  }
}
