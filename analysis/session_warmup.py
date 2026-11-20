#!/usr/bin/env python3
"""
First shot of a session runs slower -- is it real, and how big?

    cd analysis && python3 session_warmup.py                  # local espressolog.db
    python3 session_warmup.py --api http://espressolog.lan:8080   # live server, read-only

Stdlib only. Reads the same rows either way: the SQLite file through v_shot,
or GET /api/v1/shots (capped at 1000 rows by the server).

WHAT A SESSION IS

  Consecutive records less than GAP_MIN apart. Every record counts towards
  the gap -- excluded ones too, since a reject or a split shot still means
  the machine and grinder were just used -- but only CLEAN shots get a
  position. No log of grinder or machine use exists outside the shot
  records, so "first of the session" means "first shot since the scale last
  saw one", not "first time the machine ran". A blank flush or a ground and
  dumped dose between sessions is invisible here.

FINDINGS (2026-11-20, server at user_version 12, 80 clean shots in 39
sessions, 2026-09-04 to 2026-11-20, all Moonwalker / dial 7.9 / epoch 1 /
detector 0a.6)

  The first shot of a session flows ~9 % slower:

    position   n    mean flow
    1st       39    1.66 g/s
    2nd       34    1.80 g/s
    3rd        7    1.89 g/s

  Paired within session (first against the mean of the rest of the same
  session, which removes drift across weeks): later shots ran faster in 27
  of 34 multi-shot sessions, +0.16 g/s, sign-flip permutation p ~0.0015.
  Identical at GAP_MIN 30, 60, 120 and 240 minutes. Shot time is weaker --
  first shots 1.4 s longer, slower in 21 of 34 -- because the operator stops
  by weight, so total_s carries yield as well as flow.

  Roughly 15 % of 0f's sigma of 0.21 g/s: a +-0.08 g/s offset against a
  0.044 (g/s)^2 variance. So 0f's noise floor is partly a structured,
  removable factor, not all puck-prep noise.

  IT GREW. Split at the 0f snapshot: to 2026-09-29, +0.10 g/s, faster in
  14 of 20 sessions, p ~0.14 -- not distinguishable from chance, which is
  why the 46-shot 0f run never saw it (against that snapshot this script
  reports +0.10 g/s, 13 of 19, p ~0.17 -- the rows differ slightly from
  the live server's, so the split is not identical). From 2026-09-30, +0.25 g/s, faster in 13 of 14,
  p ~0.0015. Something changed between the two halves. A colder kitchen
  in October and November would fit a machine warm-up cause, and so would
  anything else seasonal; the archive records no ambient temperature
  (ambient_c is NULL throughout), so this is a hint, not a test.

  ONE CONFOUND NOT RULED OUT: first shots average 0.2 g more ground dose
  (18.13 vs 17.93 g). About 1 % more coffee is unlikely to slow flow 9 %,
  but nothing here tests it.

  WHAT IT DOES NOT SAY IS WHY. Grinder (first grind after sitting: retention,
  cold burrs) and machine (group and water path not at equilibrium although
  the display reads 95) both fit. The 3rd-shot row, n=7, hints the effect
  keeps fading past shot 2, which would fit warm-up -- too few to lean on.
  Separating them needs an intervention, not more of the same data: a blank
  flush before the first shot on some days, a ground and dumped dose on
  others.

NO ID EXCLUSIONS, unlike overshoot.py: shot 37's knock is late in its
settle window and does not touch mean_flow_gps, and 25/26 and 200/201 are
excluded = 1 in the database.
"""
import argparse
import datetime as dt
import json
import random
import sqlite3
import statistics as st
import urllib.request

DB = "espressolog.db"
GAP_MIN = 60
SENSITIVITY_GAPS = [30, 60, 120, 240]
PERMUTATIONS = 20000
SEED = 1

# One stratum, as CLAUDE.md 0f asks: one bean, one dial, one epoch, one
# detector version. Pooling detector versions inflates sigma for no reason.
STRATUM = {"bean_name": "Moonwalker", "grind_dial": 7.9, "grind_epoch_id": 1,
           "detector_version": "0a.6"}
MIN_YIELD_G = 15

FIELDS = ["id", "started_at", "excluded", "bean_name", "grind_dial",
          "grind_epoch_id", "detector_version", "yield_final_g", "total_s",
          "mean_flow_gps", "dose_ground_g"]


def load_db(path):
    conn = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
    conn.row_factory = sqlite3.Row
    return [dict(r) for r in conn.execute(
        f"SELECT {', '.join(FIELDS)} FROM v_shot")]


def load_api(base):
    with urllib.request.urlopen(base.rstrip("/") + "/api/v1/shots?limit=1000",
                                timeout=15) as r:
        rows = json.load(r)
    if len(rows) >= 1000:
        print("WARNING: the server caps the list at 1000 rows; older shots are missing")
    return [{k: row.get(k) for k in FIELDS} for row in rows]


def ts(row):
    return dt.datetime.fromisoformat(row["started_at"].replace("Z", "+00:00"))


def is_clean(row):
    return (not row["excluded"]
            and all(row[k] == v for k, v in STRATUM.items())
            and (row["yield_final_g"] or 0) >= MIN_YIELD_G
            and row["total_s"] and row["mean_flow_gps"])


def sessions(rows, gap_min):
    """Clean shots grouped by session, in time order. Every record counts
    towards the gap, clean or not."""
    out, cur, prev = [], [], None
    for row in sorted(rows, key=ts):
        t = ts(row)
        if prev is not None and (t - prev).total_seconds() > gap_min * 60:
            if cur:
                out.append(cur)
            cur = []
        prev = t
        if is_clean(row):
            cur.append(row)
    if cur:
        out.append(cur)
    return out


def paired(sess, key, sign=1):
    """Per multi-shot session: sign * (mean of later shots - first shot)."""
    return [sign * (st.mean(r[key] for r in s[1:]) - s[0][key])
            for s in sess if len(s) >= 2]


def sign_flip_p(diffs, rng):
    obs = abs(st.mean(diffs))
    hits = sum(1 for _ in range(PERMUTATIONS)
               if abs(st.mean([d * rng.choice((1, -1)) for d in diffs])) >= obs)
    return hits / PERMUTATIONS


def report(rows, gap_min, rng, full):
    sess = sessions(rows, gap_min)
    n = sum(len(s) for s in sess)
    flow = paired(sess, "mean_flow_gps")
    time = paired(sess, "total_s", sign=-1)   # positive = first shot slower
    p = sign_flip_p(flow, rng)
    print(f"gap > {gap_min:>3} min: {n} clean shots, {len(sess)} sessions, "
          f"{len(flow)} with 2+ | later faster in {sum(d > 0 for d in flow)}/{len(flow)}, "
          f"{st.mean(flow):+.3f} g/s, p={p:.4f} | first slower in "
          f"{sum(d > 0 for d in time)}/{len(time)}, {st.mean(time):+.2f} s")
    if not full:
        return
    print()
    by_pos = {}
    for s in sess:
        for i, r in enumerate(s):
            by_pos.setdefault(min(i + 1, 4), []).append(r)
    print("position   n   mean flow   total_s   dose_ground")
    for pos in sorted(by_pos):
        rs = by_pos[pos]
        doses = [r["dose_ground_g"] for r in rs if r["dose_ground_g"]]
        dose = f"{st.mean(doses):6.2f} g" if doses else "     -"
        print(f"{'4+' if pos == 4 else pos:>8} {len(rs):>3}   "
              f"{st.mean(r['mean_flow_gps'] for r in rs):5.2f} g/s  "
              f"{st.mean(r['total_s'] for r in rs):6.2f} s  {dose}")
    print()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("--db", default=DB)
    ap.add_argument("--api", help="server base URL; read-only GET, overrides --db")
    ap.add_argument("--gap-min", type=int, default=GAP_MIN)
    args = ap.parse_args()
    rows = load_api(args.api) if args.api else load_db(args.db)
    rng = random.Random(SEED)
    report(rows, args.gap_min, rng, full=True)
    print("sensitivity to the session gap:")
    for g in SENSITIVITY_GAPS:
        report(rows, g, rng, full=False)


if __name__ == "__main__":
    main()
