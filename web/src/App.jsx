import { useCallback, useEffect, useMemo, useState } from "react";
import { T, MONO, SANS, paperBg, SERIES } from "./tokens.js";
import { Eyebrow, Readout, Panel, Tag } from "./components.jsx";
import { Spark, Overlay } from "./Chart.jsx";
import { fetchShots, fetchCurve, fetchBeans, deleteShot, health, loadBean } from "./api.js";
import Capture from "./Capture.jsx";
import Beans from "./Beans.jsx";
import Pull from "./Pull.jsx";

function fmtDate(iso) {
  const d = new Date(iso);
  return {
    day: d.toLocaleDateString(undefined, { month: "2-digit", day: "2-digit" }),
    time: d.toLocaleTimeString(undefined, { hour: "2-digit", minute: "2-digit" }),
  };
}

function ShotRow({ shot, curve, selected, tone, onClick, onDelete }) {
  const { day, time } = fmtDate(shot.started_at);
  const excluded = !!shot.excluded;
  return (
    <div onClick={onClick} role="button" style={{
      width: "100%", display: "flex", alignItems: "center", gap: 20,
      padding: "14px 18px", textAlign: "left", cursor: "pointer",
      background: selected ? T.paper : "transparent",
      borderTop: `1px solid ${T.hair}`,
      opacity: excluded ? 0.45 : 1,
    }}>
      <div style={{ width: 66, flexShrink: 0 }}>
        <div style={{ fontFamily: MONO, fontSize: 12, color: T.ink }}>{day}</div>
        <div style={{ fontFamily: MONO, fontSize: 10, color: T.inkSoft }}>{time}</div>
      </div>
      <Spark curve={curve} tone={tone || T.trace} />
      <div style={{
        flex: 1, display: "grid", gap: 12,
        gridTemplateColumns: "repeat(auto-fit, minmax(52px, 1fr))",
      }}>
        <Readout value={shot.yield_final_g?.toFixed(1) ?? "—"} unit="g" label="Yield" size={17} />
        <Readout value={shot.total_s?.toFixed(1) ?? "—"} unit="s" label="Time" size={17} />
        <Readout value={shot.mean_flow_gps?.toFixed(2) ?? "—"} label="g/s" size={17} color={T.flow} />
        <Readout value={shot.ratio ? `1:${shot.ratio.toFixed(1)}` : "—"} label="Ratio" size={17} />
      </div>
      <div style={{ width: 130, flexShrink: 0, display: "flex", gap: 4, flexWrap: "wrap", justifyContent: "flex-end" }}>
        {excluded && <Tag tone={T.alert}>{(shot.exclude_reason || "excluded").split(":")[0]}</Tag>}
        {shot.detector_version && <Tag>{shot.detector_version}</Tag>}
      </div>
      <button
        onClick={(e) => { e.stopPropagation(); onDelete(); }}
        title="Delete shot (the raw device record is kept)"
        style={{
          flexShrink: 0, width: 28, height: 28, borderRadius: 2, cursor: "pointer",
          fontFamily: MONO, fontSize: 13, lineHeight: 1,
          background: "transparent", color: T.inkSoft, border: `1px solid ${T.hair}`,
        }}>×</button>
    </div>
  );
}

// Which bag is in the hopper is SERVER state (bean.loaded_at), not a
// per-browser preference. It used to live in localStorage, which meant the
// server never learned it — so a shot could only get a bean if a human
// opened the Capture screen, and opening the app on another device showed
// an empty hopper. Ingest now attributes the loaded bag to every shot, so
// this has to be the same answer everywhere.
function loadedBeanId(beans) {
  const loaded = (beans || []).filter((b) => b.loaded_at);
  if (!loaded.length) return null;
  // Tiebreak on id exactly as the server does (ORDER BY loaded_at DESC,
  // id DESC). loaded_at has second resolution, so two loads in the same
  // second would otherwise let the UI and the server disagree about which
  // bag is in the hopper.
  loaded.sort((a, b) =>
    a.loaded_at === b.loaded_at ? b.id - a.id : (a.loaded_at < b.loaded_at ? 1 : -1));
  return loaded[0].id;
}

export default function App() {
  const [tab, setTab] = useState("pull");
  const [shots, setShots] = useState(null);
  const [beans, setBeans] = useState(null);
  const [error, setError] = useState(null);
  const [curves, setCurves] = useState({});      // id -> [{t,w}]
  const [selected, setSelected] = useState([]);  // [{id, tone}]
  const [showExcluded, setShowExcluded] = useState(false);
  const [up, setUp] = useState(null);
  // What the last save did, so Capture can confirm it. This has to live here
  // rather than in Capture: Capture is keyed on the pending shot, so saving
  // one remounts it and any state it owned would go with it.
  const [justSaved, setJustSaved] = useState(null);
  const activeBeanId = loadedBeanId(beans);

  // The empty dep array is load-bearing: this is passed to Pull as
  // onIngested, and Pull's WebSocket effect is keyed on it. Give this a
  // dependency and the socket tears down and reconnects on every App render,
  // which drops live frames mid-shot.
  const refresh = useCallback(() => {
    fetchShots().then(setShots).catch((e) => setError(String(e)));
    fetchBeans().then(setBeans).catch(() => {});
  }, []);

  useEffect(() => {
    refresh();
    health().then(setUp);
    const t = setInterval(() => {
      health().then(setUp);
      refresh();   // new shots appear on their own; the device pushes when it pushes
    }, 30000);
    return () => clearInterval(t);
  }, [refresh]);

  // The confirmation is a flash, not a status. Left up it would still be
  // sitting there next time you opened the tab, claiming a save you made
  // twenty minutes ago.
  useEffect(() => {
    if (!justSaved) return;
    const t = setTimeout(() => setJustSaved(null), 5000);
    return () => clearTimeout(t);
  }, [justSaved]);

  // sparklines load lazily, oldest request wins are fine — shots are immutable
  useEffect(() => {
    if (!shots) return;
    shots.forEach((s) => {
      fetchCurve(s.id)
        .then((c) => setCurves((prev) => (prev[s.id] ? prev : { ...prev, [s.id]: c })))
        .catch(() => {});
    });
  }, [shots]);

  const toggle = (id) => {
    setSelected((sel) => {
      if (sel.some((s) => s.id === id)) return sel.filter((s) => s.id !== id);
      if (sel.length >= SERIES.length) return sel;   // four traces max — beyond that it's noise
      const used = new Set(sel.map((s) => s.tone));
      const tone = SERIES.find((c) => !used.has(c));
      return [...sel, { id, tone }];
    });
  };

  const visible = useMemo(
    () => (shots || []).filter((s) => showExcluded || !s.excluded),
    [shots, showExcluded]
  );
  const overlaySeries = selected
    .filter((s) => curves[s.id])
    .map((s) => ({ id: s.id, tone: s.tone, curve: curves[s.id] }));
  const toneOf = (id) => selected.find((s) => s.id === id)?.tone;

  return (
    <div style={{ background: T.paper, minHeight: "100vh", padding: 20, fontFamily: SANS }}>
      <div style={{ display: "flex", flexWrap: "wrap", alignItems: "baseline", justifyContent: "space-between", gap: 12, marginBottom: 18 }}>
        <div>
          <div style={{ fontFamily: MONO, fontSize: 20, letterSpacing: "0.02em", color: T.ink }}>
            SHOT<span style={{ color: T.trace }}>LOG</span>
          </div>
          <Eyebrow style={{ marginTop: 4 }}>Ascaso Dream PID · Mignon Single Dose · Themis</Eyebrow>
        </div>
        <div style={{ display: "flex", alignItems: "center", gap: 8 }}>
          <span style={{
            width: 7, height: 7, borderRadius: 99, display: "inline-block",
            background: up === null ? T.inkSoft : up ? T.flow : T.alert,
          }} />
          <Eyebrow>{up === null ? "checking…" : up ? "server up" : "server unreachable"}</Eyebrow>
        </div>
      </div>

      {error && (
        <Panel style={{ padding: 24, marginBottom: 16 }}>
          <Eyebrow style={{ color: T.alert }}>Could not load shots</Eyebrow>
          <div style={{ fontFamily: MONO, fontSize: 12, color: T.inkSoft, marginTop: 8 }}>{error}</div>
        </Panel>
      )}

      <div style={{ display: "flex", gap: 2, marginBottom: 18, borderBottom: `1px solid ${T.hair}` }}>
        {[["pull", "Pull"], ["shots", "Shots"], ["capture", "Capture"], ["beans", "Beans"]].map(([k, label]) => (
          <button key={k} onClick={() => setTab(k)} style={{
            padding: "12px 20px", fontFamily: MONO, fontSize: 11, letterSpacing: "0.14em",
            textTransform: "uppercase", background: "transparent", border: "none",
            cursor: "pointer", color: tab === k ? T.ink : T.inkSoft,
            borderBottom: `2px solid ${tab === k ? T.trace : "transparent"}`, marginBottom: -1,
          }}>
            {label}
            {k === "capture" && (shots || []).some((s) => !s.excluded && s.tasting_id == null) &&
              <span style={{ color: T.alert }}> ●</span>}
          </button>
        ))}
      </div>

      {tab === "pull" && (
        <Pull shots={shots} beans={beans} activeBeanId={activeBeanId} onIngested={refresh} />
      )}
      {tab === "capture" && (
        <Capture
          // LOAD-BEARING, not a reconciliation hint. This is the same
          // predicate Capture uses for `pending`, so a different shot
          // becoming pending remounts the screen and re-seeds every field
          // from it. Drop the key and one shot's grind, dose, temperature
          // and bean choice bleed onto the next.
          key={(shots || []).find((s) => !s.excluded && s.tasting_id == null)?.id ?? "none"}
          shots={shots} beans={beans} justSaved={justSaved}
          // Deliberately does NOT switch tabs. Jumping to Shots on every
          // save cost a tab-switch back for the common case of tagging
          // several pending shots in a row.
          onSaved={(info) => { setJustSaved(info); refresh(); }} />
      )}
      {tab === "beans" && (
        <Beans beans={beans} shots={shots} activeBeanId={activeBeanId}
          setActiveBeanId={async (id) => {
            // Without this the button fails silently on an unhandled
            // rejection, and the hopper is the one piece of state every
            // later shot inherits.
            try { await loadBean(id); refresh(); }
            catch (e) { setError(`could not load that bag: ${e}`); }
          }}
          onChanged={refresh} />
      )}

      <div style={{ display: tab === "shots" ? "flex" : "none", flexWrap: "wrap", gap: 16, alignItems: "flex-start" }}>
        <Panel style={{ flex: "1 1 460px", overflow: "hidden", minWidth: 0 }}>
          <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", padding: "12px 18px" }}>
            <Eyebrow>{shots ? `${visible.length} shots` : "loading…"}</Eyebrow>
            <button onClick={() => setShowExcluded((v) => !v)} style={{
              fontFamily: MONO, fontSize: 10, letterSpacing: "0.1em", textTransform: "uppercase",
              background: "transparent", border: `1px solid ${T.hair}`, borderRadius: 2,
              color: showExcluded ? T.ink : T.inkSoft, padding: "4px 8px", cursor: "pointer",
            }}>
              {showExcluded ? "hiding nothing" : "show excluded"}
            </button>
          </div>
          {visible.map((s) => (
            <ShotRow key={s.id} shot={s} curve={curves[s.id]}
              selected={selected.some((x) => x.id === s.id)}
              tone={toneOf(s.id)} onClick={() => toggle(s.id)}
              onDelete={async () => {
                const d = new Date(s.started_at).toLocaleString();
                if (!window.confirm(`Delete shot #${s.id} (${d}, ${s.yield_final_g ?? "?"} g)?\nThe raw device record stays archived.`)) return;
                try {
                  await deleteShot(s.id);
                  setSelected((sel) => sel.filter((x) => x.id !== s.id));
                  refresh();
                } catch (e) {
                  setError(String(e));
                }
              }} />
          ))}
          {shots && visible.length === 0 && (
            <div style={{ padding: 32, fontFamily: SANS, fontSize: 14, color: T.inkSoft }}>
              No shots yet. Pull one — it shows up here on its own.
            </div>
          )}
        </Panel>

        <div style={{ flex: "1 1 400px", minWidth: 0, display: "flex", flexDirection: "column", gap: 12 }}>
          <Panel style={{ padding: 10, ...paperBg }}>
            <Eyebrow style={{ padding: 8 }}>
              {overlaySeries.length
                ? `Overlay — ${overlaySeries.length} selected${overlaySeries.length === 1 ? " · flow in olive" : ""}`
                : "Tap shots to overlay their curves"}
            </Eyebrow>
            <Overlay series={overlaySeries} />
          </Panel>
          {selected.map(({ id, tone }) => {
            const s = (shots || []).find((x) => x.id === id);
            if (!s) return null;
            const facts = [
              ["Bean", s.bean_name ? `${s.roaster} ${s.bean_name}` : null],
              ["Grind", s.grind_dial?.toFixed(1)],
              ["Dose", s.dose_ground_g != null ? `${s.dose_ground_g.toFixed(1)} g${s.dose_source === "measured" ? " ·scale" : ""}` : null],
              ["Preinf", s.preinfusion_s != null ? `${s.preinfusion_s} s` : null],
              ["Temp", s.brew_temp_c != null ? `${s.brew_temp_c} °C` : null],
              ["Ratio", s.ratio ? `1:${s.ratio.toFixed(2)}` : null],
              ["Settle", s.settle_offset_g != null ? `+${s.settle_offset_g.toFixed(1)} g` : null],
              ["Rating", s.overall != null ? `${s.overall}/10` : null],
              ["Balance", s.balance],
            ].filter(([, v]) => v != null);
            return (
              <Panel key={id} style={{ padding: "14px 16px", borderLeft: `3px solid ${tone}` }}>
                <Eyebrow>Shot #{s.id} · {new Date(s.started_at).toLocaleString()}</Eyebrow>
                <div style={{ display: "flex", flexWrap: "wrap", gap: "4px 14px", marginTop: 8 }}>
                  {facts.map(([k, v]) => (
                    <span key={k} style={{ fontFamily: MONO, fontSize: 11.5, color: T.ink }}>
                      <span style={{ color: T.inkSoft }}>{k}·</span>{v}
                    </span>
                  ))}
                </div>
                {s.taste_notes && (
                  <div style={{ marginTop: 8, fontFamily: SANS, fontSize: 13.5, color: T.ink, lineHeight: 1.5 }}>
                    “{s.taste_notes}”
                  </div>
                )}
                {s.notes && (
                  <div style={{ marginTop: 4, fontFamily: SANS, fontSize: 12, color: T.inkSoft }}>{s.notes}</div>
                )}
              </Panel>
            );
          })}
        </div>
      </div>
    </div>
  );
}
