#!/usr/bin/env python3
"""
Backfill bean and grind onto shots that were never tagged.

    python3 backfill_recipe.py espressolog.db            # preview only
    python3 backfill_recipe.py espressolog.db --emit-sql backfill.sql

Why this exists: until migration 006 the loaded bag lived in one browser's
localStorage, so the server never learned it and a shot only got a recipe if
a human opened the Capture screen. Everything pulled and left alone is blank
— not because nothing was known, but because there was nowhere to put it.
New shots are attributed at ingest now; this is for the archive.

THE RULE, and it is deliberately timid:

  A shot is backfilled only when the nearest tagged shot BEFORE it and the
  nearest tagged shot AFTER it name the SAME bean. Two neighbours agreeing
  is strong evidence the bag never changed in between. If they disagree, a
  bag was swapped somewhere in the gap and we cannot tell which side this
  shot fell on, so it is left alone.

  At the ends of the timeline there is only one neighbour. That is used only
  within MAX_GAP_DAYS, because "the nearest tagged shot is three weeks away"
  is not evidence of anything.

Everything written is marked bean_source='backfill', so it is always
separable from what a human confirmed and trivially reversible:

    UPDATE shot SET bean_id=NULL, bean_source=NULL WHERE bean_source='backfill';

(That deliberately leaves grind_dial alone. Some backfilled shots had a
hand-entered dial all along, and clearing it would destroy a real value.)
"""
import sqlite3
import sys
import datetime as dt

MAX_GAP_DAYS = 3


def parse(ts):
    return dt.datetime.fromisoformat(ts.replace("Z", "+00:00"))


def epoch_safe(grind, shot, *sources):
    """A dial must never cross a grind_epoch boundary — hard invariant 5.

    A setting is relative to a zero point that moves whenever the burrs come
    out, so 7.9 from the previous epoch is not stale, it is a number that
    never existed. defaultRecipe on the server is epoch-scoped for exactly
    this reason; the backfill has to be too, and it is the more dangerous of
    the two because v_dialin exposes grind_epoch_id — a fabricated dial would
    sit inside the right epoch group looking measured, and there is no
    grind_source to tell it apart afterwards.
    """
    if grind is None:
        return None
    for src in sources:
        if src["grind_epoch_id"] != shot["grind_epoch_id"]:
            return None
    return grind


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    args = sys.argv[1:]
    emit = None
    if "--emit-sql" in args:
        i = args.index("--emit-sql")
        if i + 1 >= len(args):
            sys.exit("--emit-sql needs a filename")
        emit = args[i + 1]
        del args[i:i + 2]
    if not args:
        sys.exit("give me a database path")
    db = args[0]

    conn = sqlite3.connect(db)
    conn.row_factory = sqlite3.Row
    shots = [dict(r) for r in conn.execute(
        "SELECT id, started_at, bean_id, grind_dial, grind_epoch_id "
        "FROM shot ORDER BY started_at")]

    tagged = [s for s in shots if s["bean_id"] is not None]
    if not tagged:
        print("nothing is tagged — no evidence to backfill from")
        return

    plans, skipped = [], []
    for s in shots:
        if s["bean_id"] is not None:
            continue
        t = parse(s["started_at"])
        before = [x for x in tagged if parse(x["started_at"]) < t]
        after = [x for x in tagged if parse(x["started_at"]) > t]
        prev = before[-1] if before else None
        nxt = after[0] if after else None

        if prev and nxt:
            if prev["bean_id"] != nxt["bean_id"]:
                skipped.append((s, "neighbours disagree on bean — a bag changed in this gap"))
                continue
            src = prev
            # The dial gets its own agreement test. Two neighbours on the
            # same bag says nothing about whether the grinder was adjusted
            # between them, and an invented dial is worse than a missing one:
            # v_dialin does not filter on bean_source, so a guess would flow
            # straight into the Phase-3 fit as if it were measured.
            grind = prev["grind_dial"] if prev["grind_dial"] == nxt["grind_dial"] else None
            grind = epoch_safe(grind, s, prev, nxt)
        else:
            src = prev or nxt
            gap = abs((parse(src["started_at"]) - t).total_seconds()) / 86400
            if gap > MAX_GAP_DAYS:
                skipped.append((s, f"only one neighbour, {gap:.1f} days away"))
                continue
            grind = epoch_safe(src["grind_dial"], s, src)
        plans.append((s, src, grind))

    print(f"{len(shots)} shots, {len(tagged)} already tagged, "
          f"{len(shots)-len(tagged)} blank\n")
    print(f"WOULD BACKFILL {len(plans)}:")
    for s, src, grind in plans[:60]:
        if s["grind_dial"] is not None:
            gtxt = f"grind {s['grind_dial']} KEPT"
        elif grind is not None:
            gtxt = f"grind {grind}"
        else:
            gtxt = "grind NULL (no agreed dial for this epoch)"
        print(f"  shot {s['id']:3d} {s['started_at'][:16]}  <- bean {src['bean_id']} "
              f"{gtxt}  (from shot {src['id']})")
    if len(plans) > 60:
        print(f"  … and {len(plans)-60} more")
    if skipped:
        print(f"\nLEFT ALONE {len(skipped)}:")
        for s, why in skipped:
            print(f"  shot {s['id']:3d} {s['started_at'][:16]}  {why}")

    if emit:
        with open(emit, "w") as f:
            f.write("-- Generated by analysis/backfill_recipe.py. Review before running.\n")
            f.write("BEGIN;\n")
            written = 0
            for s, src, grind in plans:
                written += 1
                f.write(f"UPDATE shot SET bean_id={src['bean_id']}, bean_source='backfill' "
                        f"WHERE id={s['id']} AND bean_id IS NULL;\n")
                # Separate statement, guarded on grind_dial rather than
                # bean_id. A shot can have no bean but a hand-entered dial
                # (Capture sends both, and used to send bean_id:null), and
                # the single combined UPDATE overwrote that dial with the
                # neighbour's. It only ever got away with it because the
                # values happened to match.
                if grind is not None:
                    written += 1
                    f.write(f"UPDATE shot SET grind_dial={grind!r} "
                            f"WHERE id={s['id']} AND grind_dial IS NULL;\n")
            f.write("COMMIT;\n")
        print(f"\nwrote {emit} ({written} statements for {len(plans)} shots)")


if __name__ == "__main__":
    main()
