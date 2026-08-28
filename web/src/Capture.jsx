import { useMemo, useState } from "react";
import { T, MONO, SANS } from "./tokens.js";
import { Eyebrow, Readout, Panel, Chip, Stepper } from "./components.jsx";
import { patchShot, postTasting } from "./api.js";

// The capture screen: everything pre-filled, nothing required. Machine
// settings (grind, dose, preinfusion, temp) are sticky — seeded from the
// most recent shot that has them, because they only change when you change
// the machine. Never a tax paid while holding a portafilter.
export default function Capture({ shots, beans, activeBeanId, onSaved }) {
  const pending = useMemo(
    () => (shots || []).find((s) => !s.excluded && s.overall == null),
    [shots]
  );
  const previous = useMemo(
    () => (shots || []).find((s) => !s.excluded && s.id !== pending?.id && s.grind_dial != null),
    [shots, pending]
  );

  const [grind, setGrind] = useState(previous?.grind_dial ?? 2.4);
  const [dose, setDose] = useState(previous?.dose_ground_g ?? 18.0);
  const [preinf, setPreinf] = useState(previous?.preinfusion_s ?? 0);
  const [temp, setTemp] = useState(previous?.brew_temp_c ?? 93);
  const [balance, setBalance] = useState(null);
  const [rating, setRating] = useState(null);
  const [notes, setNotes] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState(null);

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

  const bean = beans?.find((b) => b.id === activeBeanId);

  const save = async (discard) => {
    setBusy(true);
    setError(null);
    try {
      if (discard) {
        await patchShot(pending.id, { excluded: 1, exclude_reason: "discarded at capture" });
      } else {
        await patchShot(pending.id, {
          bean_id: activeBeanId ?? null,
          grind_dial: grind,
          dose_ground_g: dose,
          preinfusion_s: preinf,
          brew_temp_c: temp,
        });
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
          <div><Eyebrow style={{ marginBottom: 8 }}>Dose</Eyebrow>
            <Stepper value={dose} onChange={setDose} step={0.1} unit="g" /></div>
          <div><Eyebrow style={{ marginBottom: 8 }}>Preinfusion</Eyebrow>
            <Stepper value={preinf} onChange={setPreinf} step={0.5} unit="s" /></div>
          <div><Eyebrow style={{ marginBottom: 8 }}>Brew temp</Eyebrow>
            <Stepper value={temp} onChange={setTemp} step={0.5} unit="°C" /></div>
        </div>

        <div style={{ marginTop: 24, paddingTop: 18, borderTop: `1px solid ${T.hair}` }}>
          <Eyebrow>Bean</Eyebrow>
          <div style={{ marginTop: 8, fontFamily: SANS, fontSize: 14, color: bean ? T.ink : T.inkSoft }}>
            {bean ? `${bean.roaster} — ${bean.name}` : "none selected — pick one under Beans"}
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
