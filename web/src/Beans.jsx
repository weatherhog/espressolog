import { useState } from "react";
import { T, MONO, SANS } from "./tokens.js";
import { Eyebrow, Readout, Panel } from "./components.jsx";
import { postBean } from "./api.js";

const daysSince = (iso) => (iso ? Math.round((Date.now() - new Date(iso)) / 864e5) : null);

function BeanCard({ bean, active, dosesUsed, onSelect }) {
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
        {bean.frozen_at && <Eyebrow style={{ color: T.trace }}>❄ frozen</Eyebrow>}
      </div>
      <div style={{ marginTop: 8, fontFamily: SANS, fontSize: 19, color: T.ink }}>{bean.name}</div>
      <div style={{ marginTop: 4, fontFamily: MONO, fontSize: 11, color: T.inkSoft }}>
        {[bean.origin, bean.process, bean.roast_level].filter(Boolean).join(" · ") || "—"}
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

function AddBean({ onAdded }) {
  const [open, setOpen] = useState(false);
  const [f, setF] = useState({ roaster: "", name: "", roast_date: "", process: "", portion_target_g: "", dose_count: "", frozen: false });
  const [error, setError] = useState(null);

  const input = (key, placeholder, type = "text") => (
    <input type={type} value={f[key]} placeholder={placeholder}
      onChange={(e) => setF({ ...f, [key]: e.target.value })}
      style={{
        width: "100%", padding: 10, fontFamily: SANS, fontSize: 14, color: T.ink,
        background: T.paper, border: `1px solid ${T.hair}`, borderRadius: 2, outline: "none",
      }} />
  );

  const submit = async () => {
    setError(null);
    try {
      const body = { roaster: f.roaster, name: f.name };
      if (f.roast_date) body.roast_date = f.roast_date;
      if (f.process) body.process = f.process;
      if (f.portion_target_g) body.portion_target_g = +f.portion_target_g;
      if (f.dose_count) body.dose_count = +f.dose_count;
      if (f.frozen) body.frozen_at = new Date().toISOString().slice(0, 10);
      await postBean(body);
      setF({ roaster: "", name: "", roast_date: "", process: "", portion_target_g: "", dose_count: "", frozen: false });
      setOpen(false);
      onAdded();
    } catch (e) {
      setError(String(e));
    }
  };

  if (!open) {
    return (
      <Panel style={{ padding: 20, display: "flex", alignItems: "center", justifyContent: "center" }}>
        <button onClick={() => setOpen(true)} style={{
          padding: "14px 28px", fontFamily: MONO, fontSize: 12, letterSpacing: "0.14em",
          textTransform: "uppercase", background: "transparent", color: T.ink,
          border: `1px dashed ${T.inkSoft}`, borderRadius: 2, cursor: "pointer",
        }}>+ New bag</button>
      </Panel>
    );
  }

  return (
    <Panel style={{ padding: 20, display: "flex", flexDirection: "column", gap: 10 }}>
      <Eyebrow>New bag</Eyebrow>
      {input("roaster", "Roaster *")}
      {input("name", "Name *")}
      {input("roast_date", "Roast date", "date")}
      {input("process", "Process (washed / natural / …)")}
      <div style={{ display: "flex", gap: 10 }}>
        {input("portion_target_g", "Portion g", "number")}
        {input("dose_count", "Doses", "number")}
      </div>
      <label style={{ fontFamily: MONO, fontSize: 11, color: T.inkSoft, display: "flex", gap: 8, alignItems: "center" }}>
        <input type="checkbox" checked={f.frozen} onChange={(e) => setF({ ...f, frozen: e.target.checked })} />
        PORTIONED INTO THE FREEZER TODAY
      </label>
      {error && <div style={{ fontFamily: MONO, fontSize: 11, color: T.alert }}>{error}</div>}
      <div style={{ display: "flex", gap: 8 }}>
        <button onClick={submit} disabled={!f.roaster || !f.name} style={{
          flex: 1, padding: "12px 0", fontFamily: MONO, fontSize: 12, letterSpacing: "0.14em",
          textTransform: "uppercase", background: T.ink, color: T.paper, border: "none",
          borderRadius: 2, cursor: "pointer", opacity: !f.roaster || !f.name ? 0.4 : 1,
        }}>Save</button>
        <button onClick={() => setOpen(false)} style={{
          padding: "12px 18px", fontFamily: MONO, fontSize: 12, background: "transparent",
          color: T.inkSoft, border: `1px solid ${T.hair}`, borderRadius: 2, cursor: "pointer",
        }}>Cancel</button>
      </div>
    </Panel>
  );
}

export default function Beans({ beans, shots, activeBeanId, setActiveBeanId, onChanged }) {
  const dosesUsed = (beanId) =>
    (shots || []).filter((s) => s.bean_id === beanId && !s.excluded).length;
  return (
    <div style={{ display: "grid", gap: 16, gridTemplateColumns: "repeat(auto-fill, minmax(280px, 1fr))" }}>
      {(beans || []).map((b) => (
        <BeanCard key={b.id} bean={b} active={b.id === activeBeanId}
          dosesUsed={dosesUsed(b.id)}
          onSelect={() => setActiveBeanId(b.id)} />
      ))}
      <AddBean onAdded={onChanged} />
    </div>
  );
}
