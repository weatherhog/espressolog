import { useState } from "react";
import { T, MONO, SANS } from "./tokens.js";
import { Eyebrow, Readout, Panel } from "./components.jsx";
import { postBean, patchBean } from "./api.js";

const daysSince = (iso) => (iso ? Math.round((Date.now() - new Date(iso)) / 864e5) : null);
const today = () => new Date().toISOString().slice(0, 10);

// One form for both "new bag" and "edit bag". On edit, every field is sent
// (nulls included) so clearing a value actually clears it. `prefill` seeds a
// NEW bag from an existing bean (the re-buy workflow: same coffee, new
// roast date); `all` feeds the autocomplete datalists.
function BeanForm({ bean, prefill, all, onDone, onCancel }) {
  const seed = bean ?? prefill;
  const [f, setF] = useState({
    roaster: seed?.roaster ?? "",
    name: seed?.name ?? "",
    origin: seed?.origin ?? "",
    region: seed?.region ?? "",
    producer: seed?.producer ?? "",
    varietal: seed?.varietal ?? "",
    altitude: seed?.altitude ?? "",
    process: seed?.process ?? "",
    roast_level: seed?.roast_level ?? "",
    roast_date: bean?.roast_date ?? "",           // never inherited: a new bag has its own roast
    bag_size_g: seed?.bag_size_g ?? "",
    url: seed?.url ?? "",
    portion_target_g: seed?.portion_target_g ?? "",
    dose_count: bean?.dose_count ?? "",
    notes: bean?.notes ?? "",
    frozen: !!bean?.frozen_at,
    finished: !!bean?.finished_at,
  });
  const [error, setError] = useState(null);
  const [busy, setBusy] = useState(false);

  // Autocomplete from what's already in the database. Names narrow to the
  // typed roaster once one matches.
  const uniq = (vals) => [...new Set(vals.filter(Boolean))];
  const lists = {
    roaster: uniq((all || []).map((b) => b.roaster)),
    name: uniq((all || []).filter((b) => !f.roaster || b.roaster === f.roaster).map((b) => b.name)),
    origin: uniq((all || []).map((b) => b.origin)),
    process: uniq((all || []).map((b) => b.process)),
    varietal: uniq((all || []).map((b) => b.varietal)),
    producer: uniq((all || []).map((b) => b.producer)),
  };

  const input = (key, placeholder, type = "text") => (
    <input type={type} value={f[key]} placeholder={placeholder}
      list={lists[key] ? `dl-${key}` : undefined}
      onChange={(e) => setF({ ...f, [key]: e.target.value })}
      style={{
        width: "100%", padding: 10, fontFamily: SANS, fontSize: 14, color: T.ink,
        background: T.paper, border: `1px solid ${T.hair}`, borderRadius: 2, outline: "none",
      }} />
  );

  const check = (key, label) => (
    <label style={{ fontFamily: MONO, fontSize: 11, color: T.inkSoft, display: "flex", gap: 8, alignItems: "center" }}>
      <input type="checkbox" checked={f[key]} onChange={(e) => setF({ ...f, [key]: e.target.checked })} />
      {label}
    </label>
  );

  const submit = async () => {
    setError(null);
    setBusy(true);
    try {
      const body = {
        roaster: f.roaster,
        name: f.name,
        origin: f.origin || null,
        region: f.region || null,
        producer: f.producer || null,
        varietal: f.varietal || null,
        altitude: f.altitude || null,
        process: f.process || null,
        roast_level: f.roast_level || null,
        roast_date: f.roast_date || null,
        bag_size_g: f.bag_size_g === "" ? null : +f.bag_size_g,
        url: f.url || null,
        portion_target_g: f.portion_target_g === "" ? null : +f.portion_target_g,
        dose_count: f.dose_count === "" ? null : +f.dose_count,
        notes: f.notes || null,
        // keep an existing date when the box stays ticked; stamp today when
        // it turns on; clear when it turns off
        frozen_at: f.frozen ? (bean?.frozen_at ?? today()) : null,
        finished_at: f.finished ? (bean?.finished_at ?? today()) : null,
      };
      if (bean) await patchBean(bean.id, body);
      else await postBean(body);
      onDone();
    } catch (e) {
      setError(String(e));
    } finally {
      setBusy(false);
    }
  };

  return (
    <Panel style={{ padding: 20, display: "flex", flexDirection: "column", gap: 10 }}>
      <Eyebrow>{bean ? `Edit — ${bean.roaster} ${bean.name}` : prefill ? `New bag — same as ${prefill.name}` : "New bag"}</Eyebrow>
      {Object.entries(lists).map(([k, vals]) => (
        <datalist id={`dl-${k}`} key={k}>
          {vals.map((v) => <option key={v} value={v} />)}
        </datalist>
      ))}
      {input("roaster", "Roaster *")}
      {input("name", "Name *")}
      {input("origin", "Origin — e.g. Brazil, Guatemala")}
      {input("region", "Region — e.g. Cerrado Mineiro / Huehuetenango")}
      {input("producer", "Producer")}
      {input("varietal", "Variety — e.g. Mundo Novo, Red Catuai, Bourbon")}
      <div style={{ display: "flex", gap: 10 }}>
        {input("altitude", "Altitude — e.g. 1300m")}
        {input("process", "Processing — washed / natural")}
      </div>
      <div style={{ display: "flex", gap: 10 }}>
        <select value={f.roast_level} onChange={(e) => setF({ ...f, roast_level: e.target.value })}
          style={{
            flex: 1, padding: 10, fontFamily: SANS, fontSize: 14,
            color: f.roast_level ? T.ink : T.inkSoft, background: T.paper,
            border: `1px solid ${T.hair}`, borderRadius: 2, outline: "none",
          }}>
          <option value="">Roast level…</option>
          {["light", "medium-light", "medium", "medium-dark", "dark"].map((l) => (
            <option key={l} value={l}>{l}</option>
          ))}
        </select>
        {input("roast_date", "Roast date", "date")}
      </div>
      <div style={{ display: "flex", gap: 10 }}>
        {input("bag_size_g", "Bag g", "number")}
        {input("portion_target_g", "Portion g", "number")}
        {input("dose_count", "Doses", "number")}
      </div>
      {input("url", "URL")}
      {input("notes", "Notes")}
      {check("frozen", "PORTIONED INTO THE FREEZER")}
      {check("finished", "BAG FINISHED")}
      {error && <div style={{ fontFamily: MONO, fontSize: 11, color: T.alert }}>{error}</div>}
      <div style={{ display: "flex", gap: 8 }}>
        <button onClick={submit} disabled={busy || !f.roaster || !f.name} style={{
          flex: 1, padding: "12px 0", fontFamily: MONO, fontSize: 12, letterSpacing: "0.14em",
          textTransform: "uppercase", background: T.ink, color: T.paper, border: "none",
          borderRadius: 2, cursor: "pointer", opacity: busy || !f.roaster || !f.name ? 0.4 : 1,
        }}>Save</button>
        <button onClick={onCancel} style={{
          padding: "12px 18px", fontFamily: MONO, fontSize: 12, background: "transparent",
          color: T.inkSoft, border: `1px solid ${T.hair}`, borderRadius: 2, cursor: "pointer",
        }}>Cancel</button>
      </div>
    </Panel>
  );
}

function BeanCard({ bean, active, dosesUsed, onSelect, onEdit, onDuplicate }) {
  const offRoast = daysSince(bean.roast_date);
  const dosesLeft = bean.dose_count != null ? bean.dose_count - dosesUsed : null;
  return (
    <Panel style={{
      padding: 20,
      borderColor: active ? T.trace : T.hair,
      borderWidth: active ? 2 : 1,
      opacity: bean.finished_at ? 0.5 : 1,
    }}>
      <div style={{ display: "flex", justifyContent: "space-between", alignItems: "flex-start" }}>
        <Eyebrow>{bean.roaster}</Eyebrow>
        <div style={{ display: "flex", gap: 6, alignItems: "center" }}>
          {bean.frozen_at && <Eyebrow style={{ color: T.trace }}>❄ frozen</Eyebrow>}
          <button onClick={onDuplicate} title="New bag of this coffee" style={{
            fontFamily: MONO, fontSize: 9, letterSpacing: "0.1em", textTransform: "uppercase",
            background: "transparent", color: T.inkSoft, border: `1px solid ${T.hair}`,
            borderRadius: 2, padding: "3px 8px", cursor: "pointer",
          }}>Same again</button>
          <button onClick={onEdit} style={{
            fontFamily: MONO, fontSize: 9, letterSpacing: "0.1em", textTransform: "uppercase",
            background: "transparent", color: T.inkSoft, border: `1px solid ${T.hair}`,
            borderRadius: 2, padding: "3px 8px", cursor: "pointer",
          }}>Edit</button>
        </div>
      </div>
      <div style={{ marginTop: 8, fontFamily: SANS, fontSize: 19, color: T.ink }}>
        {bean.url
          ? <a href={bean.url} target="_blank" rel="noreferrer" style={{ color: T.ink, textDecorationColor: T.hair }}>{bean.name}</a>
          : bean.name}
      </div>
      <div style={{ marginTop: 10, display: "grid", gridTemplateColumns: "auto 1fr", columnGap: 10, rowGap: 3 }}>
        {[
          ["Origin", bean.origin],
          ["Region", bean.region],
          ["Producer", bean.producer],
          ["Variety", bean.varietal],
          ["Altitude", bean.altitude],
          ["Processing", bean.process],
          ["Roast", bean.roast_level],
        ].filter(([, v]) => v).map(([k, v]) => (
          <div key={k} style={{ display: "contents" }}>
            <span style={{ fontFamily: MONO, fontSize: 10, letterSpacing: "0.1em", textTransform: "uppercase", color: T.inkSoft, paddingTop: 1 }}>{k}</span>
            <span style={{ fontFamily: SANS, fontSize: 12.5, color: T.ink }}>{v}</span>
          </div>
        ))}
      </div>
      <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 12, marginTop: 20 }}>
        <Readout value={dosesLeft ?? "—"} label="Doses left" size={22} />
        <Readout value={offRoast ?? "—"} unit="d" label="Off roast" size={22} />
      </div>
      <button onClick={onSelect} style={{
        width: "100%", padding: "12px 0", marginTop: 20,
        fontFamily: MONO, fontSize: 11, letterSpacing: "0.14em", textTransform: "uppercase",
        background: active ? T.trace : "transparent",
        color: active ? T.panel : T.ink,
        border: `1px solid ${active ? T.trace : T.hair}`,
        borderRadius: 2, cursor: "pointer",
      }}>
        {active ? "In the hopper" : "Load this bag"}
      </button>
    </Panel>
  );
}

export default function Beans({ beans, shots, activeBeanId, setActiveBeanId, onChanged }) {
  const [editing, setEditing] = useState(null);   // bean id | null
  const [adding, setAdding] = useState(null);     // false-y | {prefill: bean|null}
  const dosesUsed = (beanId) =>
    (shots || []).filter((s) => s.bean_id === beanId && !s.excluded).length;
  const done = () => { setEditing(null); setAdding(null); onChanged(); };

  return (
    <div style={{ display: "grid", gap: 16, gridTemplateColumns: "repeat(auto-fill, minmax(280px, 1fr))" }}>
      {(beans || []).map((b) =>
        editing === b.id ? (
          <BeanForm key={b.id} bean={b} all={beans}
            onDone={done} onCancel={() => setEditing(null)} />
        ) : (
          <BeanCard key={b.id} bean={b} active={b.id === activeBeanId}
            dosesUsed={dosesUsed(b.id)}
            onSelect={() => setActiveBeanId(b.id)}
            onEdit={() => setEditing(b.id)}
            onDuplicate={() => setAdding({ prefill: b })} />
        )
      )}
      {adding ? (
        <BeanForm prefill={adding.prefill} all={beans}
          onDone={done} onCancel={() => setAdding(null)} />
      ) : (
        <Panel style={{ padding: 20, display: "flex", alignItems: "center", justifyContent: "center", minHeight: 120 }}>
          <button onClick={() => setAdding({ prefill: null })} style={{
            padding: "14px 28px", fontFamily: MONO, fontSize: 12, letterSpacing: "0.14em",
            textTransform: "uppercase", background: "transparent", color: T.ink,
            border: `1px dashed ${T.inkSoft}`, borderRadius: 2, cursor: "pointer",
          }}>+ New bag</button>
        </Panel>
      )}
    </div>
  );
}
