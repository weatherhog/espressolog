import { useEffect, useMemo, useState } from "react";
import { T, MONO, SANS, paperBg, SERIES } from "./tokens.js";
import { Eyebrow, Readout, Panel, Tag } from "./components.jsx";
import { Spark, Overlay } from "./Chart.jsx";
import { fetchShots, fetchCurve, fetchBeans, health } from "./api.js";
import Capture from "./Capture.jsx";
import Beans from "./Beans.jsx";

function fmtDate(iso) {
  const d = new Date(iso);
  return {
    day: d.toLocaleDateString(undefined, { month: "2-digit", day: "2-digit" }),
    time: d.toLocaleTimeString(undefined, { hour: "2-digit", minute: "2-digit" }),
  };
}

function ShotRow({ shot, curve, selected, tone, onClick }) {
  const { day, time } = fmtDate(shot.started_at);
  const excluded = !!shot.excluded;
  return (
    <button onClick={onClick} style={{
      width: "100%", display: "flex", alignItems: "center", gap: 20,
      padding: "14px 18px", textAlign: "left", cursor: "pointer",
      background: selected ? T.paper : "transparent",
      border: "none", borderTop: `1px solid ${T.hair}`,
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
    </button>
  );
}

function loadActiveBean() {
  try { return JSON.parse(localStorage.getItem("activeBeanId")) ?? null; } catch { return null; }
}

export default function App() {
  const [tab, setTab] = useState("shots");
  const [shots, setShots] = useState(null);
  const [beans, setBeans] = useState(null);
  const [error, setError] = useState(null);
  const [curves, setCurves] = useState({});      // id -> [{t,w}]
  const [selected, setSelected] = useState([]);  // [{id, tone}]
  const [showExcluded, setShowExcluded] = useState(false);
  const [up, setUp] = useState(null);
  const [activeBeanId, setActiveBeanIdState] = useState(loadActiveBean);

  const setActiveBeanId = (id) => {
    setActiveBeanIdState(id);
    try { localStorage.setItem("activeBeanId", JSON.stringify(id)); } catch {}
  };

  const refresh = () => {
    fetchShots().then(setShots).catch((e) => setError(String(e)));
    fetchBeans().then(setBeans).catch(() => {});
  };

  useEffect(() => {
    refresh();
    health().then(setUp);
    const t = setInterval(() => {
      health().then(setUp);
      refresh();   // new shots appear on their own; the device pushes when it pushes
    }, 30000);
    return () => clearInterval(t);
  }, []);

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
        {[["shots", "Shots"], ["capture", "Capture"], ["beans", "Beans"]].map(([k, label]) => (
          <button key={k} onClick={() => setTab(k)} style={{
            padding: "12px 20px", fontFamily: MONO, fontSize: 11, letterSpacing: "0.14em",
            textTransform: "uppercase", background: "transparent", border: "none",
            cursor: "pointer", color: tab === k ? T.ink : T.inkSoft,
            borderBottom: `2px solid ${tab === k ? T.trace : "transparent"}`, marginBottom: -1,
          }}>
            {label}
            {k === "capture" && (shots || []).some((s) => !s.excluded && s.overall == null) &&
              <span style={{ color: T.alert }}> ●</span>}
          </button>
        ))}
      </div>

      {tab === "capture" && (
        <Capture
          key={(shots || []).find((s) => !s.excluded && s.overall == null)?.id ?? "none"}
          shots={shots} beans={beans} activeBeanId={activeBeanId}
          onSaved={() => { refresh(); setTab("shots"); }} />
      )}
      {tab === "beans" && (
        <Beans beans={beans} shots={shots} activeBeanId={activeBeanId}
          setActiveBeanId={setActiveBeanId} onChanged={refresh} />
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
              tone={toneOf(s.id)} onClick={() => toggle(s.id)} />
          ))}
          {shots && visible.length === 0 && (
            <div style={{ padding: 32, fontFamily: SANS, fontSize: 14, color: T.inkSoft }}>
              No shots yet. Pull one — it shows up here on its own.
            </div>
          )}
        </Panel>

        <Panel style={{ flex: "1 1 400px", padding: 10, minWidth: 0, ...paperBg }}>
          <Eyebrow style={{ padding: 8 }}>
            {overlaySeries.length
              ? `Overlay — ${overlaySeries.length} selected${overlaySeries.length === 1 ? " · flow in olive" : ""}`
              : "Tap shots to overlay their curves"}
          </Eyebrow>
          <Overlay series={overlaySeries} />
        </Panel>
      </div>
    </div>
  );
}
