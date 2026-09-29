import { useMemo, useState } from "react";
import { T, MONO, SANS } from "./tokens.js";
import { Eyebrow, Readout, Panel, Chip, Stepper } from "./components.jsx";
import { patchShot, postTasting } from "./api.js";

// The capture screen: everything pre-filled, nothing required. Machine
// settings (grind, dose, preinfusion, temp) are sticky — seeded from the
// most recent shot that has them, because they only change when you change
// the machine. Never a tax paid while holding a portafilter.
export default function Capture({ shots, beans, onSaved }) {
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
  const [grind, setGrind] = useState(pending?.grind_dial ?? previous?.grind_dial ?? 2.4);
  const [dose, setDose] = useState(pending?.dose_ground_g ?? previous?.dose_ground_g ?? 18.0);
  const [preinf, setPreinf] = useState(pending?.preinfusion_s ?? previous?.preinfusion_s ?? 0);
  const [temp, setTemp] = useState(pending?.brew_temp_c ?? previous?.brew_temp_c ?? 93);
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

  // EVERY hook must sit above this early return. React counts hooks per
  // render, so declaring one below it changes the count the moment a shot
  // arrives — "Rendered more hooks than during the previous render", and
  // with no error boundary in this app the whole root unmounts. The empty
  // state is the screen's advertised resting place, so this fires on the
  // ordinary path: sit on Capture with nothing pending, pull a shot, the
  // 30 s refresh delivers it, white screen.
  if (!pending) {
    return (
      <Panel style={{ padding: 40, textAlign: "center" }}>
        <Eyebrow>No shot waiting</Eyebrow>
        <div style={{ marginTop: 12, fontFamily: SANS, fontSize: 15, color: T.ink }}>
          Pull a shot and it lands here for tasting notes.
        </div>
      </Panel>
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
        const patch = {
          grind_dial: grind,
          preinfusion_s: preinf,
          brew_temp_c: temp,
        };
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
        if (dose !== pending.dose_ground_g) patch.dose_ground_g = dose;
        await patchShot(pending.id, patch);
        const tasting = { notes: notes || null };
        if (balance) tasting.balance = balance;
        if (rating) tasting.overall = rating;
        await postTasting(pending.id, tasting);
      }
      setBalance(null); setRating(null); setNotes("");
      onSaved();
    } catch (e) {
      setError(String(e));
    } finally {
      setBusy(false);
    }
  };

  return (
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
          <div><Eyebrow style={{ marginBottom: 8 }}>Grind</Eyebrow>
            <Stepper value={grind} onChange={setGrind} step={0.1} /></div>
          <div>
            <Eyebrow style={{ marginBottom: 8 }}>
              Dose{pending.dose_source === "measured" && <span style={{ color: T.flow }}> · from scale</span>}
            </Eyebrow>
            <Stepper value={dose} onChange={setDose} step={0.1} unit="g" />
          </div>
          <div><Eyebrow style={{ marginBottom: 8 }}>Preinfusion</Eyebrow>
            <Stepper value={preinf} onChange={setPreinf} step={0.5} unit="s" /></div>
          <div><Eyebrow style={{ marginBottom: 8 }}>Brew temp</Eyebrow>
            <Stepper value={temp} onChange={setTemp} step={0.5} unit="°C" /></div>
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
  );
}
