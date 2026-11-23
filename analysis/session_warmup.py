#!/usr/bin/env python3
"""
First shot of a session runs slower -- is it real, and how big?

    cd analysis && python3 session_warmup.py                  # local espressolog.db
    python3 session_warmup.py --api http://espressolog.lan:8080   # live server, read-only
    python3 session_warmup.py --api ... --split 2026-09-30        # before / from a date

Stdlib only. Reads the same rows either way: the SQLite file through v_shot,
or GET /api/v1/shots (capped at 1000 rows by the server).

WHAT A SESSION IS

  Consecutive records at most GAP_MIN apart. Every record counts towards
  the gap -- excluded ones too, since a reject or a split shot still means
  the machine and grinder were just used -- but only CLEAN shots get a
  position.

  A session whose FIRST RECORD is not a clean shot -- a drip under 5 g, a
  reject, a shot from another stratum or detector, a shot flagged
  disturbed -- is reported on its own line and
  left out of the main result: its first clean shot was not the first time
  the machine ran. Even so, "first" can only mean "first shot the scale saw":
  a flush with no cup on the scale, or a ground and dumped dose, leaves no
  record and is invisible here.

FINDINGS (2026-11-20, against the LIVE SERVER at user_version 12 -- not the
2026-09-29 snapshot that "80 shots" means elsewhere in this repo. That day
the stratum held 80 clean shots in 39 sessions, 2026-09-04 to 2026-11-20,
all Moonwalker / dial 7.9 / epoch 1 / detector 0a.6. 7 of the 39 sessions
were opened by another record, leaving 69 shots in 32 sessions.)

  Of those 7 live openers, six are drips of 0.3-3.3 g, which look like a
  flush or a cancelled press. The seventh is shot 182, excluded by hand as
  "first shot of the day, dial-in (choked, 16.5 g in 45 s)" -- a first shot
  the slowdown may itself have spoiled. On the snapshot one opener is a full
  38.3 g shot with no bean recorded (id 80), so this line is not only
  flushes; read it as "the machine ran before the first clean shot". The
  live server has since recorded its bean and dial (Moonwalker, 7.9), so
  there it is clean and no longer an opener -- which is why the live
  "before" period (20 sessions, 18 with 2+) is not the snapshot's (19, 17).

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

  SIZE, printed by the script, main sessions: mean flow sigma 0.216 g/s,
  0.202 within first/later: the split explains 12 % of the variance, and
  separating first from later shots would cut sigma by ~6 %. (The offset
  itself, +0.146 g/s, is about two thirds of sigma -- a large shift on a
  minority of shots, not a small one.)
  Real, but a small part of the spread. Nothing here says anything about
  0f's 0.21 g/s, which comes from the 2026-09-29 snapshot: on that snapshot
  this script finds +0.06 g/s, 11 of 17, p ~0.39 -- no significant effect,
  which is weak evidence on 17 sessions, not a shown absence.

  IT IS CONCENTRATED AFTER 2026-09-30. `--split 2026-09-30`: before,
  +0.065 g/s, 12 of 18, p ~0.34; from that date, +0.267 g/s, 11 of 12,
  p ~0.0026. Each of those tests a period against zero, which does not show
  the periods differ. The script's between-period test on the paired means
  gives p ~0.048 -- borderline, from one test with a split date chosen
  after looking (the 0f snapshot boundary), and a Fisher test on the sign
  counts gives ~0.19. So "it grew" is a candidate, not a finding. If it
  holds, a colder kitchen in October and November would fit a machine
  warm-up cause, and so would anything else seasonal; the archive recorded
  no ambient temperature then (ambient_c NULL throughout, 2026-11-20). From
  2026-11-22 the server fills it from a room thermometer in Home Assistant
  (CLAUDE.md, Architecture). A first look that day, from the thermometer's
  hourly means read straight from Home Assistant's statistics (not the
  server's stored values, which did not exist yet), over the 28 main
  sessions with two shots or more as this script found them on 2026-11-22
  (0a.6 only, after the exclusions of 2026-11-20 to 22 -- not the 30 of the
  2026-11-20 run), by an ad-hoc query not kept in the repo: the first-shot
  gap was 0.178 g/s in the colder half (19.7 C up to the median) against
  0.129 in the warmer (up to 24.1 C), r = -0.18 -- the direction a warm-up
  cause predicts, and well within chance; humidity r = -0.11.

  ONE CONFOUND NOT RULED OUT: first shots average 0.2 g more ground dose
  (18.13 vs 17.93 g). Whether that moves flow measurably is untested.

  WHAT IT DOES NOT SAY IS WHY. Grinder (first grind after sitting: retention,
  cold burrs) and machine (group and water path not at equilibrium although
  the display reads 95) both fit. The 3rd-shot row cannot say whether it
  keeps fading past shot 2: it is unpaired, and its 7 sessions are a
  different mix from rows 1 and 2.
  Separating them needs an intervention, not more of the same data. The
  zero-waste one is the WARM-UP TIME: the machine is switched off between
  sessions and normally warmed up a couple of minutes, so first shots after
  ~2 min on some mornings and after 10 min or more on others test the
  machine directly. A blank flush is weaker -- one flush may not bring the
  group to equilibrium, so a slow shot after it does not rule the machine
  out. A ground and dumped dose is the only direct grinder test; the
  operator declined it as wasteful (2026-11-22).

UPDATE 2026-11-22: detectors 0a.6 AND 0a.7 pooled (see DETECTORS), disturbed
shots left out. Since the 2026-11-20 run: 200/201 were excluded that day,
81 and 205 on 2026-11-21, 65 on 2026-11-22, and shots 206-208 were pulled.
Live server: 67 clean shots in 32 sessions, ONE of them 0a.7 (207), later
faster in 22 of 28 (was 23 of 30), +0.153 g/s, p ~0.010 (was ~0.0075);
13.8 % of the variance (was 12.1 %), sigma cut 7.2 % (was 6.3 %). Split:
before +0.068 (11 of 16, p ~0.37), from 2026-09-30 +0.267 (11 of 12,
p ~0.0026), periods differ p ~0.067 (was ~0.048) -- "it grew" is weaker
still. Shot 208 (0a.7, disturbed after its stop, so its flow is in fact
sound) is left out because the row alone cannot say which phase it was
disturbed in; that leaves its session's first shot, 207, unpaired.

POOLING IS NOT EXACT. 0a.7 also differs on LIFT-TERMINATED shots it does
not flag: while settling the peak follows only readings under 0.5 g above
it, so a grip press of 0.5-5 g before a lift stays out of yield_final_g --
and the flow window ends at 80 % of that yield, so mean_flow_gps moves too
(replayed shot 62: 1.932 -> 1.888 g/s). The 0a.6 rows keep the grip. On
2026-11-22 it does not matter: 0a.6 alone gives the same +0.153 g/s, 22 of
28, and dropping the four clean shots whose yield 0a.7 changes (17, 62,
192, 197) gives +0.158 g/s, 20 of 25, p ~0.011. Re-check as 0a.7 shots
accumulate; the header line prints how many are in.

NO ID EXCLUSIONS, unlike overshoot.py. 25/26, 200/201 (2026-11-20) and 65,
81, 205 (2026-11-21/22) are excluded = 1 in the database. Shot 37 is kept: its knock (19.44 s) falls in the stop
window and its slow lift in the settle window, which pulls yield_final_g to
33.8 g from ~35.9 g, the settle peak. The flow window ends at 80 % of yield_final_g
(detector.cpp computeFlowWindow), so its mean_flow_gps reads ~0.02 g/s low
(1.996 against ~2.019, recomputed from its samples) -- far below the
effect measured here.
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

# One stratum, as CLAUDE.md 0f asks: one bean, one dial, one epoch. Detector
# versions are pooled below, which CLAUDE.md warns against by default.
STRATUM = {"bean_name": "Moonwalker", "grind_dial": 7.9, "grind_epoch_id": 1}
# Detector versions pooled: 0a.6 and 0a.7 run the same pour logic. They
# differ while settling, which reaches mean_flow_gps through yield_final_g:
# on a shot 0a.7 flags disturbed (left out here -- and only 0a.7 can set the
# flag, so it filters nothing on 0a.6 rows), and on an unflagged
# lift-terminated shot whose grip 0a.7 keeps out of the yield. The docstring's
# 2026-11-22 update shows the latter does not move the result yet. Anything
# older than 0a.6 changed the stop rule and stays out. 0a.8 (2026-11-23) is
# 0a.7 except while settling: a lift keeps stop_ms and measures
# settle_offset_g, only a drop held 2 s is a lift, and a quiet scale ends
# settling on the clock. It sets the disturbed flag exactly as 0a.7 does.
# yield_final_g, and with it mean_flow_gps (its 80 % point), come out the
# same EXCEPT in two cases where 0a.7 stored the peak and 0a.8 the last
# weight: settling saw a dip that recovered (a reconnect's 0 g, a bump, a
# lift and put-back within 2 s -- 0a.7 ended the shot at the dip, 0a.8
# settles on), or the scale went quiet while settling (0a.7 ended at the
# reconnect's 0 g, 0a.8's tick() finishes on the last weight). And a pour
# whose scale was lost while the weight still rose is a fault on 0a.8
# (excluded; one lost after a lift or a flat stop is a shot), where 0a.7 held it
# open and stored a shot once the scale returned -- or lost it if it never
# did. excluded = 0 drops it on 0a.8 only. Rare, and pooled on the
# assumption that it does not move the result -- re-check as 0a.8 shots
# accumulate, as for 0a.7. BUT total_s IS stop_ms / 1000, so 0a.8 changes it: on 0a.6/0a.7
# a shot lifted while settling stores stop_ms up to ~7 s late, on 0a.8 the
# real stop. Mean flow pools across the change; shot time does not (OLD_STOP).
DETECTORS = {"0a.6", "0a.7", "0a.8"}
# Detectors whose stop_ms a lift while settling overwrote. total_s is paired
# only within a session whose clean shots are all on one side of 0a.8, and
# the per-position table reports it for these only -- the archive before
# 0a.8, where the comparison was made.
OLD_STOP = {"0a.6", "0a.7"}
MIN_YIELD_G = 15

FIELDS = ["id", "started_at", "excluded", "bean_name", "grind_dial",
          "grind_epoch_id", "detector_version", "yield_final_g", "total_s",
          "mean_flow_gps", "dose_ground_g", "disturbed"]


def load_db(path):
    conn = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
    conn.row_factory = sqlite3.Row
    have = {r[1] for r in conn.execute("PRAGMA table_info(v_shot)")}
    # A database older than a column (disturbed: migration 013) reads NULL.
    cols = [c if c in have else f"NULL AS {c}" for c in FIELDS]
    return [dict(r) for r in conn.execute(f"SELECT {', '.join(cols)} FROM v_shot")]


def load_api(base):
    with urllib.request.urlopen(base.rstrip("/") + "/api/v1/shots?limit=1000",
                                timeout=15) as r:
        rows = json.load(r) or []   # the server encodes zero rows as null
    if len(rows) >= 1000:
        print("WARNING: 1000 rows is the server's cap; older shots may be missing")
    return [{k: row.get(k) for k in FIELDS} for row in rows]


def ts(row):
    return dt.datetime.fromisoformat(row["started_at"].replace("Z", "+00:00"))


def is_clean(row):
    return (not row["excluded"]
            and not row["disturbed"]
            and row["detector_version"] in DETECTORS
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


def one_stop_era(s):
    """All of a session's shots measure stop_ms (total_s) the same way."""
    return len({r["detector_version"] in OLD_STOP for r in s}) == 1


def paired(sess, key, sign=1):
    """Per multi-shot session: sign * (mean of later shots - first shot).
    total_s only within a session on one side of 0a.8 (OLD_STOP)."""
    return [sign * (st.mean(r[key] for r in s[1:]) - s[0][key])
            for s in sess
            if len(s) >= 2 and (key != "total_s" or one_stop_era(s))]


def sign_flip_p(diffs):
    """Two-sided sign-flip permutation p. Its own seeded generator per call,
    so the same question always prints the same p."""
    if not diffs:
        return float("nan")
    rng = random.Random(SEED)
    obs = abs(st.mean(diffs))
    hits = sum(1 for _ in range(PERMUTATIONS)
               if abs(st.mean([d * rng.choice((1, -1)) for d in diffs])) >= obs)
    return (hits + 1) / (PERMUTATIONS + 1)


def between_p(a, b):
    """Two-sided permutation p that two sets of paired differences share one
    mean -- the test the split needs before calling the periods different."""
    if len(a) < 2 or len(b) < 2:
        return float("nan")
    rng = random.Random(SEED)
    obs = abs(st.mean(a) - st.mean(b))
    pool, hits = a + b, 0
    for _ in range(PERMUTATIONS):
        rng.shuffle(pool)
        if abs(st.mean(pool[:len(a)]) - st.mean(pool[len(a):])) >= obs:
            hits += 1
    return (hits + 1) / (PERMUTATIONS + 1)


def variance_share(sess):
    """How much of the mean-flow spread the first/later split explains, on
    the shots of these sessions: (sigma, sigma within groups, share of
    variance)."""
    first = [s[0]["mean_flow_gps"] for s in sess]
    later = [r["mean_flow_gps"] for s in sess for r in s[1:]]
    allv = first + later
    if len(first) < 2 or len(later) < 2:
        return None
    m1, m2 = st.mean(first), st.mean(later)
    within = st.pvariance([x - m1 for x in first] + [x - m2 for x in later])
    total = st.pvariance(allv)
    return total ** 0.5, within ** 0.5, 1 - within / total


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
          + (f"{sum(d > 0 for d in time)}/{len(time)}, {st.mean(time):+.2f} s" if time
             # every multi-shot session straddles 0a.8: no like-for-like time
             else "- (no session on one side of 0a.8)"))


def table(sess):
    by_pos = {}
    for s in sess:
        for i, r in enumerate(s):
            by_pos.setdefault(min(i + 1, 4), []).append(r)
    print("position   n   mean flow   total_s*  dose_ground")
    for pos in sorted(by_pos):
        rs = by_pos[pos]
        doses = [r["dose_ground_g"] for r in rs if r["dose_ground_g"]]
        dose = f"{st.mean(doses):6.2f} g" if doses else "     -"
        times = [r["total_s"] for r in rs if r["detector_version"] in OLD_STOP]
        time = f"{st.mean(times):6.2f} s" if times else "     - "
        print(f"{'4+' if pos == 4 else pos:>8} {len(rs):>3}   "
              f"{st.mean(r['mean_flow_gps'] for r in rs):5.2f} g/s  "
              f"{time}  {dose}")
    print(f"* total_s over {sorted(OLD_STOP)} shots only: 0a.8 stores a lifted "
          "shot's real stop, the older detectors up to ~7 s late")


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
    by_det = {}
    for s in main_sess:
        for r in s:
            by_det[r["detector_version"]] = by_det.get(r["detector_version"], 0) + 1
    print(f"session gap > {args.gap_min} min, stratum {STRATUM}, "
          f"detectors {sorted(DETECTORS)}, disturbed shots left out")
    print(f"main-session shots by detector: {dict(sorted(by_det.items()))}\n")
    table(main_sess)
    print()
    line("main (sessions opened by a clean shot)", main_sess)
    line("opened by another record            ", opened_other)
    vs = variance_share(main_sess)
    if vs:
        print(f"\nmean flow sigma {vs[0]:.3f} g/s, {vs[1]:.3f} within first/later: "
              f"{100 * vs[2]:.1f} % of the variance, "
              f"separating them cuts sigma by {100 * (1 - vs[1] / vs[0]):.1f} %")

    if args.split:
        cut = dt.datetime.fromisoformat(args.split).replace(tzinfo=dt.timezone.utc)
        print(f"\nsplit at {args.split}, main sessions only:")
        before = [s for s in main_sess if ts(s[0]) < cut]
        after = [s for s in main_sess if ts(s[0]) >= cut]
        line("  before", before)
        line("  from  ", after)
        print(f"  periods differ? p={between_p(paired(before, 'mean_flow_gps'), paired(after, 'mean_flow_gps')):.4f}")

    print("\nsensitivity to the session gap, main sessions only:")
    for g in SENSITIVITY_GAPS:
        line(f"  gap > {g:>3} min", [s for ok, s in sessions(rows, g) if ok])


if __name__ == "__main__":
    main()
