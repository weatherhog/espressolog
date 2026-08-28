import { useEffect, useRef, useState } from "react";
import { T, MONO, SANS, paperBg } from "./tokens.js";
import { Eyebrow, Readout, Panel } from "./components.jsx";
import { fetchCurve } from "./api.js";

// Target box: yield range × time range the trace should exit through.
// Two variables, one glance. (Editable targets come later; these bracket
// the current ~36 g / ~27 s recipe.)
const TARGET_YIELD = [34, 38];   // g
const TARGET_TIME = [22, 32];    // s

function LiveChart({ curve, ghost, running }) {
  const CW = 760, CH = 400, PAD = { l: 46, r: 16, t: 16, b: 34 };
  const lastT = curve.length ? curve[curve.length - 1].t : 0;
  const lastW = curve.length ? curve[curve.length - 1].w : 0;
  const maxT = Math.max(38, Math.ceil(lastT / 5) * 5 + 5);
  const maxW = Math.max(46, Math.ceil(lastW / 10) * 10 + 10);
  const xs = (t) => PAD.l + (t / maxT) * (CW - PAD.l - PAD.r);
  const ys = (w) => CH - PAD.b - (w / maxW) * (CH - PAD.t - PAD.b);
  const path = (pts) =>
    pts.length ? pts.map((p, i) => `${i ? "L" : "M"}${xs(p.t).toFixed(1)},${ys(p.w).toFixed(1)}`).join(" ") : "";

  const flowPts = [];
  for (let i = 7; i < curve.length; i++) {
    const a = curve[i - 7], b = curve[i];
    if (b.t > a.t) flowPts.push({ t: b.t, w: ((b.w - a.w) / (b.t - a.t)) * (maxW / 4) });
  }

  return (
    <svg viewBox={`0 0 ${CW} ${CH}`} style={{ display: "block", width: "100%" }}>
      <rect x={xs(TARGET_TIME[0])} y={ys(TARGET_YIELD[1])}
        width={xs(TARGET_TIME[1]) - xs(TARGET_TIME[0])}
        height={ys(TARGET_YIELD[0]) - ys(TARGET_YIELD[1])}
        fill={T.flow} opacity="0.09" stroke={T.flow} strokeOpacity="0.45"
        strokeDasharray="3 3" strokeWidth="1" />
      <text x={xs(TARGET_TIME[1]) - 5} y={ys(TARGET_YIELD[1]) - 7} textAnchor="end"
        style={{ fontFamily: MONO, fontSize: 9, fill: T.flow, letterSpacing: "0.1em" }}>TARGET</text>

      <line x1={PAD.l} y1={CH - PAD.b} x2={CW - PAD.r} y2={CH - PAD.b} stroke={T.ink} strokeWidth="1" />
      <line x1={PAD.l} y1={PAD.t} x2={PAD.l} y2={CH - PAD.b} stroke={T.ink} strokeWidth="1" />
      {Array.from({ length: Math.floor(maxT / 5) + 1 }, (_, i) => i * 5).map((t) => (
        <g key={`t${t}`}>
          <line x1={xs(t)} y1={CH - PAD.b} x2={xs(t)} y2={CH - PAD.b + 4} stroke={T.ink} />
          <text x={xs(t)} y={CH - PAD.b + 18} textAnchor="middle"
            style={{ fontFamily: MONO, fontSize: 10, fill: T.inkSoft }}>{t}</text>
        </g>
      ))}
      {Array.from({ length: Math.floor(maxW / 10) + 1 }, (_, i) => i * 10).map((w) => (
        <g key={`w${w}`}>
          <line x1={PAD.l - 4} y1={ys(w)} x2={PAD.l} y2={ys(w)} stroke={T.ink} />
          <text x={PAD.l - 9} y={ys(w) + 3.5} textAnchor="end"
            style={{ fontFamily: MONO, fontSize: 10, fill: T.inkSoft }}>{w}</text>
        </g>
      ))}
      <text x={PAD.l - 9} y={PAD.t + 4} textAnchor="end"
        style={{ fontFamily: MONO, fontSize: 9, fill: T.inkSoft, letterSpacing: "0.1em" }}>G</text>
      <text x={CW - PAD.r} y={CH - 6} textAnchor="end"
        style={{ fontFamily: MONO, fontSize: 9, fill: T.inkSoft, letterSpacing: "0.1em" }}>SECONDS</text>

      {/* ghost — the last shot on this bean, faint behind the live pen */}
      {ghost && <path d={path(ghost)} fill="none" stroke={T.trace} strokeOpacity="0.3"
        strokeWidth="1.5" strokeDasharray="5 4" />}
      <path d={path(flowPts)} fill="none" stroke={T.flow} strokeWidth="1.2" strokeOpacity="0.65" />
      <path d={path(curve)} fill="none" stroke={T.trace} strokeWidth="2.4"
        strokeLinecap="round" strokeLinejoin="round" />
      {running && curve.length > 0 && (
        <circle cx={xs(lastT)} cy={ys(lastW)} r="4.5" fill={T.trace} />
      )}
    </svg>
  );
}

export default function Pull({ shots, beans, activeBeanId, onIngested }) {
  const [, forceRender] = useState(0);
  const [wsUp, setWsUp] = useState(false);
  const [ghost, setGhost] = useState(null);
  const [lastEvent, setLastEvent] = useState(null);

  // Live buffers in refs: 10 Hz frames shouldn't churn React state objects.
  const live = useRef({
    curve: [], capturing: false, t0: 0,
    yield_mg: 0, state: "—", lastYieldAt: 0,
    dose_mg: 0, lastDoseAt: 0,
  });

  // ghost: last non-excluded shot on the active bean, else last overall
  useEffect(() => {
    const candidates = (shots || []).filter((s) => !s.excluded);
    const g = candidates.find((s) => s.bean_id === activeBeanId) || candidates[0];
    if (g) fetchCurve(g.id).then(setGhost).catch(() => {});
    else setGhost(null);
  }, [shots, activeBeanId]);

  useEffect(() => {
    let sock, closed = false, retry;
    const connect = () => {
      const proto = location.protocol === "https:" ? "wss" : "ws";
      sock = new WebSocket(`${proto}://${location.host}/api/v1/live`);
      sock.onopen = () => setWsUp(true);
      sock.onclose = () => {
        setWsUp(false);
        if (!closed) retry = setTimeout(connect, 3000);
      };
      sock.onmessage = (m) => {
        let f;
        try { f = JSON.parse(m.data); } catch { return; }
        const L = live.current;
        const now = Date.now();
        if (f.ev === "ingested") {
          onIngested();
          if (f.type === 1) setLastEvent("shot stored ✓");
          return;
        }
        if (f.ev === "shot") {
          setLastEvent(f.fault ? "shot faulted (cup removed?)" : f.valid ? "shot complete" : "rejected (<5 g)");
          return;
        }
        if (f.r === 0) {
          L.yield_mg = f.w;
          L.state = f.s;
          L.lastYieldAt = now;
          const active = f.s === "POURING" || f.s === "SETTLING";
          if (active && !L.capturing) {
            L.capturing = true;
            L.t0 = f.t;
            L.curve = [];
            setLastEvent(null);
          }
          if (L.capturing) {
            L.curve.push({ t: (f.t - L.t0) / 1000, w: f.w / 1000 });
            if (!active) L.capturing = false;   // keep the finished curve on screen
          }
        } else if (f.r === 1) {
          L.dose_mg = f.w;
          L.lastDoseAt = now;
        }
        forceRender((n) => n + 1);
      };
    };
    connect();
    return () => { closed = true; clearTimeout(retry); sock?.close(); };
  }, [onIngested]);

  const L = live.current;
  const now = Date.now();
  const yieldFresh = now - L.lastYieldAt < 3000;
  const doseFresh = now - L.lastDoseAt < 5000;
  const bean = (beans || []).find((b) => b.id === activeBeanId);
  const flow = (() => {
    const c = L.curve;
    if (c.length < 8) return 0;
    const a = c[c.length - 8], b = c[c.length - 1];
    return b.t > a.t ? (b.w - a.w) / (b.t - a.t) : 0;
  })();

  return (
    <div style={{ display: "flex", flexWrap: "wrap", gap: 16, alignItems: "flex-start" }}>
      <Panel style={{ flex: "1 1 480px", minWidth: 0, padding: 10, ...paperBg }}>
        <LiveChart curve={L.curve} ghost={ghost} running={L.capturing} />
      </Panel>

      <div style={{ display: "flex", flexDirection: "column", gap: 16, flex: "0 1 260px" }}>
        <Panel style={{ padding: 20 }}>
          <Readout value={(L.yield_mg / 1000).toFixed(1)} unit="g" label="Yield" size={54}
            color={L.capturing ? T.trace : yieldFresh ? T.ink : T.inkSoft} />
          <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 14, marginTop: 22 }}>
            <Readout value={L.curve.length ? L.curve[L.curve.length - 1].t.toFixed(1) : "0.0"} label="Sec" size={22} />
            <Readout value={flow.toFixed(2)} label="g/s" size={22} color={T.flow} />
          </div>
        </Panel>

        <Panel style={{ padding: 16 }}>
          <Eyebrow>Status</Eyebrow>
          <div style={{ fontFamily: MONO, fontSize: 12, color: T.ink, lineHeight: 1.9, marginTop: 8 }}>
            <div>{wsUp ? "● server link up" : "○ server link down"}</div>
            <div style={{ color: yieldFresh ? T.ink : T.inkSoft }}>
              {yieldFresh ? `● cup scale · ${L.state}` : "○ cup scale quiet"}
            </div>
            <div style={{ color: doseFresh ? T.ink : T.inkSoft }}>
              {doseFresh ? `● grinder scale · ${(L.dose_mg / 1000).toFixed(1)} g` : "○ grinder scale quiet"}
            </div>
            {lastEvent && <div style={{ color: T.flow }}>{lastEvent}</div>}
          </div>
        </Panel>

        <Panel style={{ padding: 16 }}>
          <Eyebrow>In the basket</Eyebrow>
          <div style={{ marginTop: 8, fontFamily: SANS, fontSize: 14, color: bean ? T.ink : T.inkSoft }}>
            {bean ? `${bean.roaster} — ${bean.name}` : "no bean loaded"}
          </div>
          {ghost && (
            <div style={{ marginTop: 6, fontFamily: MONO, fontSize: 10.5, color: T.inkSoft }}>
              ghost: previous shot{bean ? " on this bean" : ""}
            </div>
          )}
        </Panel>
      </div>
    </div>
  );
}
