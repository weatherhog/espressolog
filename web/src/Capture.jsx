import { useMemo, useState } from "react";
import { T, MONO, SANS } from "./tokens.js";
import { Eyebrow, Readout, Panel, Chip, Stepper } from "./components.jsx";
import { patchShot, postTasting } from "./api.js";

// The capture screen: everything pre-filled, nothing required. Machine
// settings (grind, dose, preinfusion, temp) are sticky — seeded from the
// most recent shot that has them, because they only change when you change
// the machine. Never a tax paid while holding a portafilter.
export default function Capture({ shots, beans, justSaved, onSaved }) {
  // Pending = never captured at all. A tasting row, even one saved without
  // a rating, counts as captured — otherwise saving twice was invited.
  const pending = useMemo(
    () => (shots || []).find((s) => !s.excluded && s.tasting_id == null),
    [shots]
  );
  const previous = useMemo(
    () => (shots || []).find((s) => !s.excluded && s.id !== pending?.id && s.grind_dial != null),
    [shots, pending]
  );

  // The dose prefers the shot's own value — stage 5 attributes it from the
  // grinder scale automatically, so it's usually already right.
  //
  // Seeding a field and then sending it unconditionally records a value
  // nobody typed as though a human confirmed it — see the bean comment in `save`, which
  // is the same lesson learned a release earlier and not applied here.
  // Measured 2026-10-12: 18 of 80 shots carried dose_source='manual' with no
  // weighing behind them, 17 of them the identical value 18.109 written in
  // one batch-tagging session on 2026-09-28, and shot 3 carrying the literal
  // defaults below (dial 2.4, temp 93) when the real settings were 7.9 and
  // 95. Comparing state against the seed is what makes "the user touched
  // this" expressible at all.
  const [grind, setGrind] = useState(pending?.grind_dial ?? previous?.grind_dial ?? 2.4);
  const [dose, setDose] = useState(pending?.dose_ground_g ?? previous?.dose_ground_g ?? 18.0);
  const [preinf, setPreinf] = useState(pending?.preinfusion_s ?? previous?.preinfusion_s ?? 0);
  const [temp, setTemp] = useState(pending?.brew_temp_c ?? previous?.brew_temp_c ?? 93);

  // WHICH FIELDS THE OPERATOR ACTUALLY TOUCHED. Set in onChange and nowhere
  // else, so it answers that question directly and cannot be wrong.
  //
  // A first attempt compared state against named seed consts instead, and it
  // did not work: those consts recompute on every render while useState
  // captures only on mount, and App keys this component on the shot's `id`,
  // so a 30 s refresh that returns the SAME shot with CHANGED fields moves
  // the seed and leaves the state. That is the normal case, not a corner —
  // Spool::oldest() is a lexicographic minimum and "s-" sorts before "w-",
  // so within a boot every shot record uploads before every weighing record.
  // The sequence was: open Capture on a dose-less shot (seed = previous
  // shot's dose) -> the grinder weighing arrives and the server sets the real
  // measured dose -> refresh, no remount -> seed is now the measured value,
  // state is still the guess -> save writes the guess over the measurement
  // and stamps dose_source='manual'. The fix reintroduced the bug it was
  // written to remove, one layer down.
  const [touched, setTouched] = useState({});
  // Only count it when the value actually MOVES. Stepper clamps at its floor
  // (`+Math.max(min, v).toFixed(2)`), so pressing "–" on a Preinfusion already
  // at 0 calls onChange(0) — an identical value that would otherwise mark the
  // field touched and PATCH it.
  //
  // WHAT IT DOES NOT CLOSE, and a draft claimed it did: "+ then –" back to the
  // starting value. The second press moves relative to the CURRENT value, so
  // `touched` stays set and the save still sends the field — on the Dose that
  // flips a measured value to dose_source='manual' without changing the
  // number, because UpdateShot stamps 'manual' on the mere PRESENCE of the
  // key. Catching that needs a comparison against the SEED, which this
  // deliberately abandoned because the seed moves. Only the clamped no-op is
  // closed here.
  const mark = (k, set, cur) => (v) => {
    if (v !== cur) setTouched((t) => (t[k] ? t : { ...t, [k]: true }));
    set(v);
  };
  const [balance, setBalance] = useState(null);
  const [rating, setRating] = useState(null);
  const [notes, setNotes] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState(null);
  // undefined = leave whatever ingest attributed; null = deliberately clear;
  // a number = set. Three states, because "no change" and "clear it" are
  // different intentions and collapsing them made the clear option dead.
  // Nothing resets this on a new shot, and nothing needs to: App keys this
  // component on the same predicate `pending` uses, so a different pending
  // shot remounts the whole screen and every useState above re-seeds from
  // the new shot. An effect resetting beanPick alone used to sit here; it
  // could only ever fire on mount, and it hid the fact that grind/dose/temp
  // depend on that key for the same thing.
  const [beanPick, setBeanPick] = useState(undefined);

  // Saving used to jump to the Shots tab, which doubled as the "it worked"
  // signal. Staying put is better — with several shots pending you can tag
  // them one after another — but it means the confirmation has to be said
  // out loud instead of implied by the screen changing.
  const banner = justSaved && (
    <div style={{
      padding: "10px 14px", marginBottom: 16, fontFamily: MONO, fontSize: 11,
      letterSpacing: "0.08em", color: justSaved.discarded ? T.inkSoft : T.flow,
      border: `1px solid ${T.hair}`, borderRadius: 2,
    }}>
      {justSaved.discarded
        ? `Shot #${justSaved.id} discarded`
        : `Shot #${justSaved.id} saved ✓`}
    </div>
  );

  // EVERY hook must sit above this early return. React counts hooks per
  // render, so declaring one below it changes the count the moment a shot
  // arrives — "Rendered more hooks than during the previous render", and
  // with no error boundary in this app the whole root unmounts. The empty
  // state is the screen's advertised resting place, so this fires on the
  // ordinary path: sit on Capture with nothing pending, pull a shot, the
  // 30 s refresh delivers it, white screen.
  if (!pending) {
    return (
      <>
      {banner}
      <Panel style={{ padding: 40, textAlign: "center" }}>
        <Eyebrow>No shot waiting</Eyebrow>
        <div style={{ marginTop: 12, fontFamily: SANS, fontSize: 15, color: T.ink }}>
          Pull a shot and it lands here for tasting notes.
        </div>
      </Panel>
      </>
    );
  }

  // The bean THIS SHOT was pulled on — no falling back to the hopper. The
  // fallback rendered a coffee the save would not write, which is the same
  // lie in the other direction: a shot ingested before any bag was loaded
  // would show today's bag while staying NULL in the database.
  const bean = beans?.find((b) => b.id === pending?.bean_id);

  const save = async (discard) => {
    setBusy(true);
    setError(null);
    try {
      if (discard) {
        await patchShot(pending.id, { excluded: 1, exclude_reason: "discarded at capture" });
      } else {
        // SEND ONLY WHAT THE USER TOUCHED. Each of these was sent
        // unconditionally until 2026-10-12, so opening this screen stamped
        // the seeded value onto the shot — and for a shot with no weighing
        // the server reads a dose arriving here as dose_source='manual',
        // i.e. "a human confirmed this". Leaving a field alone must leave
        // the column alone; an unconfirmed guess belongs nowhere, and NULL
        // is the honest value for "not recorded".
        const patch = {};
        if (touched.grind)  patch.grind_dial = grind;
        if (touched.preinf) patch.preinfusion_s = preinf;
        if (touched.temp)   patch.brew_temp_c = temp;
        // Sent only when the user actually picked a bean, exactly as
        // dose_ground_g is handled below. Sending the hopper's value
        // unconditionally was wrong three ways: with no bag loaded it sent
        // null and ERASED a correct inference; when it matched it promoted
        // bean_source 'loaded' -> 'user', claiming a confirmation nobody
        // made; and on a shot pulled before a bag change it overwrote the
        // bag actually used. A deliberate pick is none of those things — it
        // is the only thing on this screen that earns bean_source='user'.
        if (beanPick !== undefined && beanPick !== pending.bean_id) patch.bean_id = beanPick;
        // Only send the dose when the user actually corrected it — an
        // untouched scale-attributed value must keep dose_source='measured'.
        //
        // This compared against `pending.dose_ground_g` until 2026-10-12,
        // which looks right and is not: when the shot has no weighing that
        // field is null, the seeded `dose` is a number, and `dose !== null`
        // is true on every save. The comparison that was meant to suppress
        // the write guaranteed it. Compare against the seed.
        if (touched.dose) patch.dose_ground_g = dose;
        // A PATCH with no recognised fields currently 500s server-side, and
        // an all-untouched save is now the common case.
        if (Object.keys(patch).length) await patchShot(pending.id, patch);
        const tasting = { notes: notes || null };
        if (balance) tasting.balance = balance;
        if (rating) tasting.overall = rating;
        await postTasting(pending.id, tasting);
      }
      setBalance(null); setRating(null); setNotes("");
      onSaved({ id: pending.id, discarded: discard });
    } catch (e) {
      // Only re-enable on failure. On success the buttons stay disabled
      // until refresh() delivers the next pending shot, which remounts this
      // component and resets busy — because the shot is still on screen in
      // the meantime and the tab switch that used to hide it is gone. If
      // refresh never lands they stay disabled, which is the safe direction:
      // a second press would write a second tasting row for a shot the
      // banner already says is saved.
      setError(String(e));
      setBusy(false);
    }
  };

  return (
    <>
    {banner}
    <div style={{ display: "flex", flexWrap: "wrap", gap: 16, alignItems: "flex-start" }}>
      <Panel style={{ padding: 24, flex: "1 1 380px", minWidth: 0 }}>
        <Eyebrow>Measured — shot #{pending.id} · {new Date(pending.started_at).toLocaleString()}</Eyebrow>
        <div style={{ display: "grid", gridTemplateColumns: "repeat(3, 1fr)", gap: 16, margin: "16px 0 32px" }}>
          <Readout value={pending.total_s?.toFixed(1) ?? "—"} unit="s" label="Time" size={30} />
          <Readout value={pending.yield_final_g?.toFixed(1) ?? "—"} unit="g" label="Yield" size={30} />
          <Readout
            value={pending.settle_offset_g != null
              ? (pending.settle_offset_g >= 0 ? `+${pending.settle_offset_g.toFixed(1)}` : pending.settle_offset_g.toFixed(1))
              : "—"}
            unit="g" label="Settled" size={30} color={T.flow} />
        </div>

        <Eyebrow>Confirm</Eyebrow>
        <div style={{ display: "grid", gridTemplateColumns: "repeat(auto-fit, minmax(150px, 1fr))", gap: 20, marginTop: 16 }}>
          {/* Every field renders the SERVER's value until the operator touches
              it, and says so when the server has none.

              Two bugs motivate this. The seeds are captured on mount and App
              keys this component on the shot id, so a refresh that updates
              `pending` without remounting left the stepper showing a stale
              guess — under a badge reading "from scale", in the dose's case.
              The save was already correct, but an operator seeing a wrong
              number labelled measured will nudge it to fix it, which writes
              the guess and stamps it manual: the original bug, by hand.

              And when the column is genuinely NULL — a new database, a
              14-day gap, a shot after a recorded setpoint edit — the stepper
              falls back to a hard-coded literal (grind 2.4, temp 93,
              preinfusion 0) and renders it under a heading that says
              Confirm. Those literals are exactly what migration 009 exists
              to erase. Nothing is written untouched, so this is now a
              labelling problem rather than a data one, but presenting a
              made-up number as the current setting is how it became a data
              problem the first time.

              AND NOTE WHAT THIS STILL CANNOT SAY. Since 2026-10-12 the server
              carries brew_temp_c and preinfusion_s forward at ingest, so `col`
              is normally non-null and `unrecorded` is false — meaning a SERVER
              INFERENCE renders here with no badge at all, under a heading that
              says Confirm. There is no brew_temp_source column to distinguish
              it from a value a human typed. That is the same provenance
              problem migration 009 exists to undo, moved from this screen to
              the server rather than removed. Fixing it properly needs the
              source columns; see defaultRecipe in server/internal/store. */}
          {/* FINDING: once `touched[k]` is set this ignores every later server
              value for that field, so the lost-update window is narrowed, not
              closed. Tap the dose once on a dose-less shot, let the grinder
              weighing land, save — and the guess still overwrites the
              measurement. `moved` surfaces that rather than silently losing
              it; closing it properly means deciding whose value wins, which
              is a product question, not a bug fix. */}
          {[
            { k: "grind",  label: "Grind",       v: grind,  set: setGrind,  col: pending.grind_dial,    step: 0.1, unit: undefined },
            { k: "dose",   label: "Dose",        v: dose,   set: setDose,   col: pending.dose_ground_g, step: 0.1, unit: "g" },
            { k: "preinf", label: "Preinfusion", v: preinf, set: setPreinf, col: pending.preinfusion_s, step: 0.5, unit: "s" },
            { k: "temp",   label: "Brew temp",   v: temp,   set: setTemp,   col: pending.brew_temp_c,   step: 0.5, unit: "\u00b0C" },
          ].map(({ k, label, v, set, col, step, unit }) => {
            const shown = touched[k] ? v : (col ?? v);
            const unrecorded = !touched[k] && col == null;
            // The server wrote a different value while this field was being
            // edited — almost always a grinder weighing landing on the dose.
            const moved = touched[k] && col != null && col !== v;
            return (
              <div key={k}>
                <Eyebrow style={{ marginBottom: 8 }}>
                  {label}
                  {k === "dose" && pending.dose_source === "measured" &&
                    <span style={{ color: T.flow }}> &middot; from scale</span>}
                  {unrecorded &&
                    <span style={{ color: T.inkSoft }}> &middot; not recorded</span>}
                  {moved &&
                    <span style={{ color: T.flow }}> &middot; scale says {col}</span>}
                </Eyebrow>
                <Stepper value={shown} onChange={mark(k, set, shown)} step={step} unit={unit} />
              </div>
            );
          })}
        </div>

        <div style={{ marginTop: 24, paddingTop: 18, borderTop: `1px solid ${T.hair}` }}>
          <Eyebrow>Bean</Eyebrow>
          <select
            value={(beanPick === undefined ? pending?.bean_id : beanPick) ?? ""}
            onChange={(e) => setBeanPick(e.target.value === "" ? null : Number(e.target.value))}
            style={{
              marginTop: 8, width: "100%", padding: "6px 4px",
              fontFamily: SANS, fontSize: 14,
              color: (beanPick === undefined ? bean : beanPick) ? T.ink : T.inkSoft,
              background: "transparent", border: `1px solid ${T.hair}`, borderRadius: 2,
            }}>
            <option value="">not recorded</option>
            {(beans || []).map((b) => (
              <option key={b.id} value={b.id}>{b.roaster} — {b.name}</option>
            ))}
          </select>
          <div style={{ marginTop: 6, fontFamily: MONO, fontSize: 9.5, color: T.inkSoft, letterSpacing: "0.06em" }}>
            {pending?.bean_source === "loaded" ? "from the loaded bag — change only if wrong"
              : pending?.bean_source === "user" ? "confirmed"
              : "no bag was loaded when this was pulled"}
          </div>
        </div>
      </Panel>

      <Panel style={{ padding: 24, flex: "1 1 300px", minWidth: 0 }}>
        <Eyebrow>Taste</Eyebrow>
        <div style={{ display: "flex", gap: 8, marginTop: 12 }}>
          <Chip active={balance === "under"} onClick={() => setBalance("under")} tone={T.alert}>Under</Chip>
          <Chip active={balance === "balanced"} onClick={() => setBalance("balanced")} tone={T.flow}>Balanced</Chip>
          <Chip active={balance === "over"} onClick={() => setBalance("over")} tone={T.alert}>Over</Chip>
        </div>

        <Eyebrow style={{ marginTop: 22 }}>Rating</Eyebrow>
        <div style={{ display: "flex", gap: 4, marginTop: 12 }}>
          {Array.from({ length: 10 }, (_, i) => i + 1).map((n) => (
            <button key={n} onClick={() => setRating(n)} style={{
              flex: 1, padding: "12px 0", fontFamily: MONO, fontSize: 12, cursor: "pointer",
              borderRadius: 2, border: `1px solid ${rating === n ? T.trace : T.hair}`,
              background: rating === n ? T.trace : "transparent",
              color: rating === n ? T.panel : T.inkSoft,
            }}>{n}</button>
          ))}
        </div>

        <Eyebrow style={{ marginTop: 22 }}>Notes</Eyebrow>
        <textarea value={notes} onChange={(e) => setNotes(e.target.value)} rows={4}
          placeholder="What it actually tasted like."
          style={{
            width: "100%", marginTop: 12, padding: 12, fontFamily: SANS, fontSize: 14,
            color: T.ink, background: T.paper, border: `1px solid ${T.hair}`,
            borderRadius: 2, resize: "none", outline: "none",
          }} />

        {error && (
          <div style={{ marginTop: 12, fontFamily: MONO, fontSize: 11, color: T.alert }}>{error}</div>
        )}

        <button disabled={busy} onClick={() => save(false)} style={{
          width: "100%", padding: "16px 0", marginTop: 16,
          fontFamily: MONO, fontSize: 13, letterSpacing: "0.16em", textTransform: "uppercase",
          background: T.ink, color: T.paper, border: "none", borderRadius: 3,
          cursor: "pointer", opacity: busy ? 0.5 : 1,
        }}>Save shot</button>
        <button disabled={busy} onClick={() => save(true)} style={{
          width: "100%", padding: "12px 0", marginTop: 8,
          fontFamily: MONO, fontSize: 11, letterSpacing: "0.14em", textTransform: "uppercase",
          background: "transparent", color: T.alert, border: `1px solid ${T.hair}`,
          borderRadius: 3, cursor: "pointer", opacity: busy ? 0.5 : 1,
        }}>Discard — channelled / test</button>
      </Panel>
    </div>
    </>
  );
}
