#!/usr/bin/env python3
"""
First shot of a session runs slower -- is it real, and how big?

    cd analysis && python3 session_warmup.py                  # local espressolog.db
    python3 session_warmup.py --api http://espressolog.lan:8080   # live server, read-only
    python3 session_warmup.py --api ... --split 2026-09-30        # before / from a date

Stdlib only. Reads the same rows either way: the SQLite file through v_shot,
or GET /api/v1/shots (capped at 1000 rows by the server).

WHAT A SESSION IS

  Consecutive records less than GAP_MIN apart. Every record counts towards
  the gap -- excluded ones too, since a reject or a split shot still means
  the machine and grinder were just used -- but only CLEAN shots get a
  position.

  A session whose FIRST RECORD is not a clean shot -- a drip under 5 g, a
  reject, a shot from another stratum -- is reported on its own line and
  left out of the main result: its first clean shot was not the first time
  the machine ran. Most such openers are drips of 0.3-3 g, which look like
  a flush or a cancelled press, so that line is an accidental "flush first"
  comparison. Even so, "first" can only mean "first shot the scale saw":
  a flush with no cup on the scale, or a ground and dumped dose, leaves no
  record and is invisible here.

FINDINGS (2026-11-20, against the LIVE SERVER at user_version 12 -- not the
2026-09-29 snapshot that "80 shots" means elsewhere in this repo. That day
the stratum held 80 clean shots in 39 sessions, 2026-09-04 to 2026-11-20,
all Moonwalker / dial 7.9 / epoch 1 / detector 0a.6. 7 of the 39 sessions
were opened by another record, leaving 69 shots in 32 sessions.)

  The first shot of a session flows ~9 % slower:

    position   n    mean flow
    1st       32    1.66 g/s
    2nd       30    1.80 g/s
    3rd        7    1.89 g/s

  Paired within session (first against the mean of the rest of the same
  session, which removes drift across weeks): later shots ran faster in 23
  of 30 multi-shot sessions, +0.146 g/s, sign-flip permutation p ~0.0075.
  GAP_MIN 120 and 240 merge one pair of sessions (31, 29 with 2+, 22
  faster, p ~0.009) -- the same conclusion, not identical numbers. Shot
  time is weaker -- first shots 1.2 s longer, slower in 17 of 30 -- because
  the operator stops by weight, so total_s carries yield as well as flow.
  The 7 sessions opened by another record point the same way (4 of 4,
  +0.27 g/s, p ~0.12); too few to say more.

  SIZE, on these same rows: mean flow sigma 0.216 g/s. Taking out the
  first/later offset leaves 0.202 g/s -- 12 % of the variance, about 6 % of
  sigma. Real, but a small part of the spread. Nothing here says anything
  about 0f's 0.21 g/s, which comes from the 2026-09-29 snapshot: on that
  snapshot this script finds +0.06 g/s, 11 of 17, p ~0.39 -- no effect.

  IT GREW. `--split 2026-09-30`: before, +0.065 g/s, 12 of 18, p ~0.34;
  from that date, +0.267 g/s, 11 of 12, p ~0.0026. Something changed. A
  colder kitchen in October and November would fit a machine warm-up
  cause, and so would anything else seasonal; the archive records no
  ambient temperature (ambient_c is NULL throughout), so this is a hint,
  not a test.

  ONE CONFOUND NOT RULED OUT: first shots average 0.2 g more ground dose
  (18.13 vs 17.93 g). Whether that moves flow measurably is untested.

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
    """Sessions in time order, each (opened_clean, clean shots). Every record
    counts towards the gap, clean or not; only clean shots are kept.
    opened_clean is False when the session's first record was not a clean
    shot -- a drip, a reject, another stratum -- so its first CLEAN shot was
    not the first time the machine ran."""
    out, cur, opened_clean, prev = [], None, True, None
    for row in sorted(rows, key=ts):
        t = ts(row)
        if prev is None or (t - prev).total_seconds() > gap_min * 60:
            if cur:
                out.append((opened_clean, cur))
            cur, opened_clean = [], is_clean(row)
        prev = t
        if is_clean(row):
            cur.append(row)
    if cur:
        out.append((opened_clean, cur))
    return out


def paired(sess, key, sign=1):
    """Per multi-shot session: sign * (mean of later shots - first shot)."""
    return [sign * (st.mean(r[key] for r in s[1:]) - s[0][key])
            for s in sess if len(s) >= 2]


def sign_flip_p(diffs):
    """Two-sided sign-flip permutation p. Its own seeded generator per call,
    so the same question always prints the same p."""
    if not diffs:
        return float("nan")
    rng = random.Random(SEED)
    obs = abs(st.mean(diffs))
    hits = sum(1 for _ in range(PERMUTATIONS)
               if abs(st.mean([d * rng.choice((1, -1)) for d in diffs])) >= obs)
    return hits / PERMUTATIONS


def line(label, sess):
    flow = paired(sess, "mean_flow_gps")
    time = paired(sess, "total_s", sign=-1)   # positive = first shot slower
    n = sum(len(s) for s in sess)
    if not flow:
        print(f"{label}: {n} clean shots, {len(sess)} sessions, none with 2+")
        return
    print(f"{label}: {n} clean shots, {len(sess)} sessions, {len(flow)} with 2+ | "
          f"later faster in {sum(d > 0 for d in flow)}/{len(flow)}, "
          f"{st.mean(flow):+.3f} g/s, p={sign_flip_p(flow):.4f} | first slower in "
          f"{sum(d > 0 for d in time)}/{len(time)}, {st.mean(time):+.2f} s")


def table(sess):
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


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("--db", default=DB)
    ap.add_argument("--api", help="server base URL; read-only GET, overrides --db")
    ap.add_argument("--gap-min", type=int, default=GAP_MIN)
    ap.add_argument("--split", metavar="YYYY-MM-DD",
                    help="also report sessions starting before and from this date (UTC)")
    args = ap.parse_args()
    rows = load_api(args.api) if args.api else load_db(args.db)

    allsess = sessions(rows, args.gap_min)
    main_sess = [s for ok, s in allsess if ok]
    opened_other = [s for ok, s in allsess if not ok]
    print(f"session gap > {args.gap_min} min, stratum {STRATUM}\n")
    table(main_sess)
    print()
    line("main (sessions opened by a clean shot)", main_sess)
    line("opened by another record            ", opened_other)

    if args.split:
        cut = dt.datetime.fromisoformat(args.split).replace(tzinfo=dt.timezone.utc)
        print(f"\nsplit at {args.split}, main sessions only:")
        line(f"  before", [s for s in main_sess if ts(s[0]) < cut])
        line(f"  from  ", [s for s in main_sess if ts(s[0]) >= cut])

    print("\nsensitivity to the session gap, main sessions only:")
    for g in SENSITIVITY_GAPS:
        line(f"  gap > {g:>3} min", [s for ok, s in sessions(rows, g) if ok])


if __name__ == "__main__":
    main()
