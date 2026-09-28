#!/usr/bin/env python3
"""
Overshoot after the pump stops — the calibration number Phase 1 needs.

    cd analysis && python3 overshoot.py

CLAUDE.md: "settle_offset_g matters more than it looks: today you stop the
shot by hand when the scale reads target, so the overshoot distribution IS
the calibration data Phase 1 needs. Phase 0 measures it before any wire is
cut." This is that measurement.

FINDINGS (2026-09-28, n=38 after exclusions)

  Overshoot is most predictable ~2 s after the stop: +0.20 g, parametric
  sigma 0.071 g. Trigger 0.2 g below target and read at +2 s and you land
  within +-0.14 g at 2 sigma -- a third of a +-0.5 g tolerance. Stop-at-
  weight is viable on this machine.

  This is a THRESHOLD TRIGGER, not a control loop, so 0f's 12% flow noise
  floor does not gate it. Different problem, much easier.

  Measured later than ~3 s the spread doubles, because the Bookoo's load
  cell drifts under a hot cup (see shot 57: a smooth monotonic -0.8 g over
  the final 5 s with no step anywhere). The detector's SETTLE_MS is 5000 ms,
  i.e. squarely in the drift-contaminated region -- 2500 ms would give a
  tighter yield_final_g. Not a casual change: it redefines yield_final_g
  and breaks comparability with existing shots, so it needs a detector
  version bump.

TWO EXCLUSIONS, both earned rather than assumed

  Lift-terminated shots. Seven of the 46 have NO samples after stop_ms at
  all -- the curve ends exactly there because the cup was lifted and the
  detector finalised at the peak. Their settle_offset_g is 0 by
  construction, not by measurement, and pooling them drags the mean down
  while telling you nothing. Detected here by requiring a >=4 s tail rather
  than by trusting the field.

  Shot 37. A -3.10 g step followed by +2.00 g within 180 ms at t=19.4 s --
  liquid cannot do that, something knocked the cup -- then a steady ~1 g/s
  decline from t=24 s that never reaches the 5 g lift threshold. A cup
  being picked up slowly, caught mid-lift by the settle window.
"""
import sqlite3
import statistics as st

DB = "espressolog.db"
WINDOWS_MS = [500, 1000, 2000, 3000, 5000]
MIN_TAIL_MS = 4000        # below this the settle window was never recorded
DISTURBED = [37]          # knocked cup, see docstring
SPLIT_SHOT = [25, 26]     # one pull the detector split in two

CLEAN = f"""
  SELECT id, stop_ms, settle_offset_g FROM v_shot
   WHERE yield_final_g >= 15 AND COALESCE(excluded, 0) = 0
     AND bean_name = 'Moonwalker' AND grind_dial = 7.9
     AND detector_version = '0a.6'
     AND id NOT IN ({','.join(str(i) for i in SPLIT_SHOT + DISTURBED)})
"""


def curve(conn, shot_id):
    return [(r[0], r[1] / 1000.0) for r in conn.execute(
        "SELECT t_ms, weight_mg FROM shot_sample WHERE shot_id=? ORDER BY t_ms", (shot_id,))]


def weight_at(samples, t_ms):
    """Last sample at or before t_ms — the scale reading a controller would see."""
    seen = [w for t, w in samples if t <= t_ms]
    return seen[-1] if seen else None


def spread(xs):
    med = st.median(xs)
    mad = st.median([abs(x - med) for x in xs])
    return med, mad, 1.4826 * mad, st.mean(xs), st.stdev(xs)


def main():
    conn = sqlite3.connect(DB)
    rows = list(conn.execute(CLEAN))
    acc = {w: [] for w in WINDOWS_MS}
    lifted = 0

    for shot_id, stop_ms, _ in rows:
        s = curve(conn, shot_id)
        if not s:
            continue
        if s[-1][0] - stop_ms < MIN_TAIL_MS:
            lifted += 1                       # lift-terminated: no settle to measure
            continue
        base = weight_at(s, stop_ms)
        for w in WINDOWS_MS:
            v = weight_at(s, stop_ms + w)
            if base is not None and v is not None:
                acc[w].append(v - base)

    n = len(acc[WINDOWS_MS[-1]])
    print(f"shots considered {len(rows)}   lift-terminated (excluded) {lifted}   measured {n}\n")
    print(f"{'window':>8} | {'median':>7} | {'MAD':>6} | {'robust sd':>9} | {'mean':>7} | {'sd':>6}")
    print("-" * 60)
    best = None
    for w in WINDOWS_MS:
        med, mad, rsd, mean, sd = spread(acc[w])
        print(f"{w/1000:6.1f} s | {med:+7.3f} | {mad:6.3f} | {rsd:9.3f} | {mean:+7.3f} | {sd:6.3f}")
        if best is None or sd < best[1]:
            best = (w, sd, med)

    w, sd, med = best
    print(f"\nTightest at {w/1000:.1f} s: overshoot {med:+.2f} g, sigma {sd:.3f} g.")
    print(f"Trigger {abs(med):.2f} g below target, read at +{w/1000:.0f} s "
          f"-> +-{2*sd:.2f} g at 2 sigma.")


if __name__ == "__main__":
    main()
