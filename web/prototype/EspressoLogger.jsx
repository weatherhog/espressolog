import { useState, useRef, useEffect, useCallback } from "react";

/* ------------------------------------------------------------------ *
 *  Espresso shot logger — Phase 0 (logging only)
 *  Ascaso Dream PID · Eureka Mignon Single Dose · Bookoo Themis
 *
 *  Prototype with simulated scale data. No machine control.
 *  Visual reference: strip-chart recorder — pen on measurement paper.
 * ------------------------------------------------------------------ */

/* ---- tokens ------------------------------------------------------- */
const T = {
  paper: "#F2F3EE",
  panel: "#FBFBF8",
  gridFine: "#E8CFC4",
  gridBold: "#D9A992",
  ink: "#1B2430",
  inkSoft: "#6B7480",
  trace: "#17497A",
  flow: "#7A8B3F",
  alert: "#A3341F",
  hair: "#D5D6CE",
};

const MONO = "'SF Mono', 'DejaVu Sans Mono', Menlo, Consolas, monospace";
const SANS = "'Avenir Next', 'Segoe UI', system-ui, sans-serif";

const paperBg = {
  backgroundColor: T.paper,
  backgroundImage: `
    repeating-linear-gradient(0deg,  ${T.gridFine} 0 1px, transparent 1px 10px),
    repeating-linear-gradient(90deg, ${T.gridFine} 0 1px, transparent 1px 10px),
    repeating-linear-gradient(0deg,  ${T.gridBold} 0 1px, transparent 1px 50px),
    repeating-linear-gradient(90deg, ${T.gridBold} 0 1px, transparent 1px 50px)`,
};

/* ---- mock domain data --------------------------------------------- */
const BEANS = [
  {
    id: 1, roaster: "Kaffeewerkstatt", name: "Kolla Bolcha", origin: "Ethiopia",
    process: "Washed", roastLevel: "Light", roastDate: "2026-08-08",
    frozen: true, remainingDoses: 11,
  },
  {
    id: 2, roaster: "Five Elephant", name: "Finca Nueva Armenia", origin: "Honduras",
    process: "Natural", roastLevel: "Medium-light", roastDate: "2026-07-29",
    frozen: true, remainingDoses: 4,
  },
];

const PREP = {
  id: 1, name: "Standard double",
  basket: "IMS B702TH24 · 18 g", wdt: "spiral, 10 turns",
  leveler: true, tamp: true, puckScreen: "1.7 mm", rdt: false,
};

/* simulate a weight curve: returns [{t, w}] at 10 Hz */
function synth({ firstDrip = 6.5, peak = 1.75, decay = 0.008, stopAt = 36 }) {
  const out = [];
  let w = 0;
  for (let i = 0; i <= 450; i++) {
    const t = i / 10;
    let f = 0;
    if (t > firstDrip) {
      const x = t - firstDrip;
      f = x < 4 ? peak * (x / 4) : peak * Math.exp(-decay * (x - 4));
    }
    w += f * 0.1;
    out.push({ t, w: Math.max(0, w + (Math.random() - 0.5) * 0.04) });
    if (w >= stopAt) break;
  }
  return out;
}

const SHOTS = [
  {
    id: 3, at: "2026-08-24 08:12", beanId: 1, grindDial: 2.4, doseGround: 18.1,
    yieldFinal: 36.4, timeS: 29.6, meanFlow: 1.58, balance: "balanced", rating: 8,
    daysOffRoast: 16, settleOffset: 1.9, notes: "peach, tea-like finish. best so far",
    curve: synth({ firstDrip: 6.4, peak: 1.74, stopAt: 36.4 }),
  },
  {
    id: 2, at: "2026-08-23 08:20", beanId: 1, grindDial: 2.6, doseGround: 18.0,
    yieldFinal: 36.1, timeS: 21.4, meanFlow: 2.41, balance: "under", rating: 4,
    daysOffRoast: 15, settleOffset: 2.4, notes: "sharp, thin, lemon-peel bitter",
    curve: synth({ firstDrip: 4.9, peak: 2.62, decay: 0.004, stopAt: 36.1 }),
  },
  {
    id: 1, at: "2026-08-22 08:05", beanId: 1, grindDial: 2.2, doseGround: 18.2,
    yieldFinal: 35.8, timeS: 38.9, meanFlow: 1.09, balance: "over", rating: 5,
    daysOffRoast: 14, settleOffset: 1.5, notes: "drying, ashy in the tail",
    curve: synth({ firstDrip: 8.7, peak: 1.24, decay: 0.014, stopAt: 35.8 }),
  },
];

/* ---- small pieces -------------------------------------------------- */
function Eyebrow({ children, style }) {
  return (
    <div style={{
      fontFamily: MONO, fontSize: 10, letterSpacing: "0.14em",
      textTransform: "uppercase", color: T.inkSoft, ...style,
    }}>{children}</div>
  );
}

function Readout({ value, unit, label, color = T.ink, size = 34 }) {
  return (
    <div>
      <div style={{
        fontFamily: MONO, fontSize: size, lineHeight: 1, color,
        fontVariantNumeric: "tabular-nums", letterSpacing: "-0.02em",
      }}>
        {value}
        {unit && <span style={{ fontSize: size * 0.4, color: T.inkSoft, marginLeft: 3 }}>{unit}</span>}
      </div>
      <Eyebrow style={{ marginTop: 5 }}>{label}</Eyebrow>
    </div>
  );
}

function Panel({ children, className = "", style }) {
  return (
    <div className={className} style={{
      background: T.panel, border: `1px solid ${T.hair}`, borderRadius: 3, ...style,
    }}>{children}</div>
  );
}

function Chip({ active, onClick, children, tone = T.trace }) {
  return (
    <button onClick={onClick} className="px-3 py-2 flex-1" style={{
      fontFamily: MONO, fontSize: 11, letterSpacing: "0.1em", textTransform: "uppercase",
      border: `1px solid ${active ? tone : T.hair}`, borderRadius: 2,
      background: active ? tone : "transparent", color: active ? T.panel : T.inkSoft,
      cursor: "pointer",
    }}>{children}</button>
  );
}

function Stepper({ value, onChange, step, decimals = 1, unit }) {
  const btn = {
    width: 40, height: 40, fontFamily: MONO, fontSize: 18, cursor: "pointer",
    border: `1px solid ${T.hair}`, background: "transparent", color: T.ink, borderRadius: 2,
  };
  return (
    <div className="flex items-center gap-2">
      <button style={btn} onClick={() => onChange(+(value - step).toFixed(2))}>–</button>
      <div className="flex-1 text-center" style={{
        fontFamily: MONO, fontSize: 24, color: T.ink, fontVariantNumeric: "tabular-nums",
      }}>
        {value.toFixed(decimals)}
        {unit && <span style={{ fontSize: 12, color: T.inkSoft, marginLeft: 2 }}>{unit}</span>}
      </div>
      <button style={btn} onClick={() => onChange(+(value + step).toFixed(2))}>+</button>
    </div>
  );
}

/* ---- the chart ------------------------------------------------------ */
const CW = 760, CH = 380, PAD = { l: 46, r: 16, t: 16, b: 34 };

function Chart({ curve, ghost, targetYield, targetTime, running }) {
  const maxT = Math.max(38, Math.ceil(((curve.at(-1)?.t) || 0) / 5) * 5 + 5);
  const maxW = Math.max(46, Math.ceil(((curve.at(-1)?.w) || 0) / 10) * 10 + 10);
  const xs = (t) => PAD.l + (t / maxT) * (CW - PAD.l - PAD.r);
  const ys = (w) => CH - PAD.b - (w / maxW) * (CH - PAD.t - PAD.b);
  const path = (pts) => pts.length
    ? pts.map((p, i) => `${i ? "L" : "M"}${xs(p.t).toFixed(1)},${ys(p.w).toFixed(1)}`).join(" ")
    : "";

  const head = curve.at(-1);

  /* flow-rate trace: derivative over a 0.7 s window, scaled to right axis 0–4 g/s */
  const flowPts = [];
  for (let i = 7; i < curve.length; i++) {
    const a = curve[i - 7], b = curve[i];
    flowPts.push({ t: b.t, w: ((b.w - a.w) / (b.t - a.t)) * (maxW / 4) });
  }

  return (
    <svg viewBox={`0 0 ${CW} ${CH}`} className="w-full" style={{ display: "block" }}>
      {/* target box — you want the trace to exit through here */}
      <rect
        x={xs(targetTime[0])} y={ys(targetYield[1])}
        width={xs(targetTime[1]) - xs(targetTime[0])}
        height={ys(targetYield[0]) - ys(targetYield[1])}
        fill={T.flow} opacity="0.09" stroke={T.flow} strokeOpacity="0.45"
        strokeDasharray="3 3" strokeWidth="1"
      />
      <text x={xs(targetTime[1]) - 5} y={ys(targetYield[1]) - 7} textAnchor="end"
        style={{ fontFamily: MONO, fontSize: 9, fill: T.flow, letterSpacing: "0.1em" }}>
        TARGET
      </text>

      {/* axes */}
      <line x1={PAD.l} y1={CH - PAD.b} x2={CW - PAD.r} y2={CH - PAD.b} stroke={T.ink} strokeWidth="1" />
      <line x1={PAD.l} y1={PAD.t} x2={PAD.l} y2={CH - PAD.b} stroke={T.ink} strokeWidth="1" />

      {Array.from({ length: Math.floor(maxT / 5) + 1 }, (_, i) => i * 5).map((t) => (
        <g key={t}>
          <line x1={xs(t)} y1={CH - PAD.b} x2={xs(t)} y2={CH - PAD.b + 4} stroke={T.ink} />
          <text x={xs(t)} y={CH - PAD.b + 18} textAnchor="middle"
            style={{ fontFamily: MONO, fontSize: 10, fill: T.inkSoft }}>{t}</text>
        </g>
      ))}
      {Array.from({ length: Math.floor(maxW / 10) + 1 }, (_, i) => i * 10).map((w) => (
        <g key={w}>
          <line x1={PAD.l - 4} y1={ys(w)} x2={PAD.l} y2={ys(w)} stroke={T.ink} />
          <text x={PAD.l - 9} y={ys(w) + 3.5} textAnchor="end"
            style={{ fontFamily: MONO, fontSize: 10, fill: T.inkSoft }}>{w}</text>
        </g>
      ))}
      <text x={PAD.l - 9} y={PAD.t + 4} textAnchor="end"
        style={{ fontFamily: MONO, fontSize: 9, fill: T.inkSoft, letterSpacing: "0.1em" }}>G</text>
      <text x={CW - PAD.r} y={CH - 6} textAnchor="end"
        style={{ fontFamily: MONO, fontSize: 9, fill: T.inkSoft, letterSpacing: "0.1em" }}>SECONDS</text>

      {/* ghost — last shot, same bean */}
      {ghost && (
        <path d={path(ghost)} fill="none" stroke={T.trace} strokeOpacity="0.3"
          strokeWidth="1.5" strokeDasharray="5 4" />
      )}

      {/* flow rate */}
      <path d={path(flowPts)} fill="none" stroke={T.flow} strokeWidth="1.2" strokeOpacity="0.65" />

      {/* the pen */}
      <path d={path(curve)} fill="none" stroke={T.trace} strokeWidth="2.4"
        strokeLinecap="round" strokeLinejoin="round" />
      {head && running && (
        <circle cx={xs(head.t)} cy={ys(head.w)} r="4.5" fill={T.trace} />
      )}
    </svg>
  );
}

/* ---- pull screen ---------------------------------------------------- */
function Pull({ bean, onFinish, ghost }) {
  const [curve, setCurve] = useState([]);
  const [running, setRunning] = useState(false);
  const timer = useRef(null);
  const seed = useRef(null);

  const start = () => {
    seed.current = synth({
      firstDrip: 5.6 + Math.random() * 2.2,
      peak: 1.5 + Math.random() * 0.6,
      decay: 0.006 + Math.random() * 0.008,
      stopAt: 42,
    });
    setCurve([]); setRunning(true);
    let i = 0;
    timer.current = setInterval(() => {
      i += 1;
      if (i >= seed.current.length) { clearInterval(timer.current); setRunning(false); return; }
      setCurve(seed.current.slice(0, i));
    }, 100);
  };

  const stop = useCallback(() => {
    clearInterval(timer.current);
    setRunning(false);
    setCurve((c) => {
      if (c.length > 20) {
        const last = c.at(-1);
        onFinish({
          timeS: +last.t.toFixed(1),
          yieldAtStop: +last.w.toFixed(1),
          curve: c,
        });
      }
      return c;
    });
  }, [onFinish]);

  useEffect(() => () => clearInterval(timer.current), []);

  const head = curve.at(-1) || { t: 0, w: 0 };
  const flow = curve.length > 8
    ? (head.w - curve[curve.length - 9].w) / (head.t - curve[curve.length - 9].t) : 0;
  const ratio = head.w / 18.1;

  return (
    <div className="flex flex-col lg:flex-row gap-4">
      <Panel className="flex-1 p-3" style={paperBg}>
        <Chart curve={curve} ghost={ghost} targetYield={[35.5, 36.5]}
          targetTime={[28, 32]} running={running} />
      </Panel>

      <div className="flex flex-col gap-4" style={{ minWidth: 250 }}>
        <Panel className="p-5">
          <Readout value={head.w.toFixed(1)} unit="g" label="Yield" size={58}
            color={running ? T.trace : T.ink} />
          <div className="grid grid-cols-3 gap-3 mt-6">
            <Readout value={head.t.toFixed(1)} label="Sec" size={22} />
            <Readout value={flow.toFixed(2)} label="g/s" size={22} color={T.flow} />
            <Readout value={`1:${ratio.toFixed(2)}`} label="Ratio" size={22} />
          </div>
        </Panel>

        <button
          onClick={running ? stop : start}
          className="py-5 w-full"
          style={{
            fontFamily: MONO, fontSize: 15, letterSpacing: "0.16em", textTransform: "uppercase",
            border: `1px solid ${running ? T.alert : T.ink}`, borderRadius: 3, cursor: "pointer",
            background: running ? T.alert : T.ink, color: T.paper,
          }}>
          {running ? "Stop shot" : "Arm scale"}
        </button>

        <Panel className="p-4">
          <Eyebrow>In the basket</Eyebrow>
          <div className="mt-2" style={{ fontFamily: SANS, fontSize: 14, color: T.ink }}>
            {bean.roaster} — {bean.name}
          </div>
          <div className="mt-1" style={{ fontFamily: MONO, fontSize: 11, color: T.inkSoft }}>
            {bean.origin} · {bean.process} · 16 d off roast
          </div>
          <div className="mt-3 pt-3" style={{ borderTop: `1px solid ${T.hair}` }}>
            <div style={{ fontFamily: MONO, fontSize: 11, color: T.inkSoft, lineHeight: 1.7 }}>
              GRIND 2.4 · DOSE 18.1 g<br />
              {PREP.basket}<br />
              WDT {PREP.wdt} · leveller · screen
            </div>
          </div>
        </Panel>
      </div>
    </div>
  );
}

/* ---- capture screen -------------------------------------------------- */
function Capture({ pending, onSave, onDiscard }) {
  const [grind, setGrind] = useState(2.4);
  const [dose, setDose] = useState(18.1);
  const [yieldFinal, setYieldFinal] = useState(pending ? pending.yieldAtStop + 1.9 : 36.0);
  const [balance, setBalance] = useState(null);
  const [rating, setRating] = useState(null);
  const [notes, setNotes] = useState("");

  if (!pending) {
    return (
      <Panel className="p-10 text-center">
        <Eyebrow>No shot waiting</Eyebrow>
        <div className="mt-3" style={{ fontFamily: SANS, fontSize: 15, color: T.ink }}>
          Pull a shot and it lands here for tasting notes.
        </div>
      </Panel>
    );
  }

  const settle = +(yieldFinal - pending.yieldAtStop).toFixed(1);

  return (
    <div className="flex flex-col lg:flex-row gap-4">
      <Panel className="p-6 flex-1">
        <Eyebrow>Measured</Eyebrow>
        <div className="grid grid-cols-3 gap-4 mt-4 mb-8">
          <Readout value={pending.timeS.toFixed(1)} unit="s" label="Time" size={30} />
          <Readout value={pending.yieldAtStop.toFixed(1)} unit="g" label="At stop" size={30} />
          <Readout value={settle > 0 ? `+${settle}` : settle} unit="g" label="Settled" size={30}
            color={T.flow} />
        </div>

        <Eyebrow>Confirm</Eyebrow>
        <div className="grid grid-cols-3 gap-5 mt-4">
          <div>
            <Eyebrow style={{ marginBottom: 8 }}>Grind</Eyebrow>
            <Stepper value={grind} onChange={setGrind} step={0.1} />
          </div>
          <div>
            <Eyebrow style={{ marginBottom: 8 }}>Dose</Eyebrow>
            <Stepper value={dose} onChange={setDose} step={0.1} unit="g" />
          </div>
          <div>
            <Eyebrow style={{ marginBottom: 8 }}>Final yield</Eyebrow>
            <Stepper value={yieldFinal} onChange={setYieldFinal} step={0.1} unit="g" />
          </div>
        </div>

        <div className="mt-6 pt-5" style={{ borderTop: `1px solid ${T.hair}` }}>
          <div style={{ fontFamily: MONO, fontSize: 11, color: T.inkSoft, letterSpacing: "0.06em" }}>
            PREP · {PREP.name} · {PREP.basket} · WDT {PREP.wdt} · leveller · tamp · {PREP.puckScreen} screen
          </div>
        </div>
      </Panel>

      <Panel className="p-6" style={{ minWidth: 300 }}>
        <Eyebrow>Taste</Eyebrow>
        <div className="flex gap-2 mt-3">
          <Chip active={balance === "under"} onClick={() => setBalance("under")} tone={T.alert}>Under</Chip>
          <Chip active={balance === "balanced"} onClick={() => setBalance("balanced")} tone={T.flow}>Balanced</Chip>
          <Chip active={balance === "over"} onClick={() => setBalance("over")} tone={T.alert}>Over</Chip>
        </div>

        <Eyebrow style={{ marginTop: 22 }}>Rating</Eyebrow>
        <div className="flex gap-1 mt-3">
          {Array.from({ length: 10 }, (_, i) => i + 1).map((n) => (
            <button key={n} onClick={() => setRating(n)} className="flex-1 py-3" style={{
              fontFamily: MONO, fontSize: 12, cursor: "pointer", borderRadius: 2,
              border: `1px solid ${rating === n ? T.trace : T.hair}`,
              background: rating === n ? T.trace : "transparent",
              color: rating === n ? T.panel : T.inkSoft,
            }}>{n}</button>
          ))}
        </div>

        <Eyebrow style={{ marginTop: 22 }}>Notes</Eyebrow>
        <textarea
          value={notes} onChange={(e) => setNotes(e.target.value)} rows={4}
          placeholder="What it actually tasted like."
          className="w-full mt-3 p-3"
          style={{
            fontFamily: SANS, fontSize: 14, color: T.ink, background: T.paper,
            border: `1px solid ${T.hair}`, borderRadius: 2, resize: "none", outline: "none",
          }}
        />

        <button
          onClick={() => onSave({ grind, dose, yieldFinal, balance, rating, notes, settle })}
          className="w-full py-4 mt-4"
          style={{
            fontFamily: MONO, fontSize: 13, letterSpacing: "0.16em", textTransform: "uppercase",
            background: T.ink, color: T.paper, border: "none", borderRadius: 3, cursor: "pointer",
          }}>
          Save shot
        </button>
        <button onClick={onDiscard} className="w-full py-3 mt-2" style={{
          fontFamily: MONO, fontSize: 11, letterSpacing: "0.14em", textTransform: "uppercase",
          background: "transparent", color: T.alert, border: `1px solid ${T.hair}`,
          borderRadius: 3, cursor: "pointer",
        }}>
          Discard — channelled / test
        </button>
      </Panel>
    </div>
  );
}

/* ---- history --------------------------------------------------------- */
function Spark({ curve, tone }) {
  const w = 120, h = 34;
  const mx = curve.at(-1)?.t || 1, my = curve.at(-1)?.w || 1;
  const d = curve.filter((_, i) => i % 4 === 0)
    .map((p, i) => `${i ? "L" : "M"}${(p.t / mx) * w},${h - (p.w / my) * h}`).join(" ");
  return (
    <svg width={w} height={h} style={{ display: "block" }}>
      <path d={d} fill="none" stroke={tone} strokeWidth="1.6" />
    </svg>
  );
}

function History({ shots, selected, toggle }) {
  const tone = (s) => s.balance === "balanced" ? T.flow : T.alert;
  return (
    <div className="flex flex-col lg:flex-row gap-4">
      <Panel className="flex-1" style={{ overflow: "hidden" }}>
        {shots.map((s, i) => (
          <button key={s.id} onClick={() => toggle(s.id)}
            className="w-full flex items-center gap-5 px-5 py-4 text-left"
            style={{
              borderTop: i ? `1px solid ${T.hair}` : "none", cursor: "pointer",
              background: selected.includes(s.id) ? T.paper : "transparent", border: "none",
              borderTopWidth: i ? 1 : 0, borderTopStyle: "solid", borderTopColor: T.hair,
            }}>
            <div style={{ width: 92 }}>
              <div style={{ fontFamily: MONO, fontSize: 12, color: T.ink }}>{s.at.slice(5, 10)}</div>
              <div style={{ fontFamily: MONO, fontSize: 10, color: T.inkSoft }}>{s.at.slice(11)}</div>
            </div>
            <Spark curve={s.curve} tone={tone(s)} />
            <div className="flex-1 grid grid-cols-4 gap-3">
              <Readout value={s.grindDial.toFixed(1)} label="Grind" size={17} />
              <Readout value={s.timeS.toFixed(1)} unit="s" label="Time" size={17} />
              <Readout value={s.meanFlow.toFixed(2)} label="g/s" size={17} color={T.flow} />
              <Readout value={s.rating} label="Score" size={17}
                color={s.rating >= 7 ? T.flow : T.alert} />
            </div>
            <div style={{ width: 190, fontFamily: SANS, fontSize: 12, color: T.inkSoft }}>
              {s.notes}
            </div>
          </button>
        ))}
      </Panel>

      <Panel className="p-3" style={{ minWidth: 340, ...paperBg }}>
        <Eyebrow style={{ padding: 6 }}>
          Overlay — {selected.length || "none"} selected
        </Eyebrow>
        <svg viewBox="0 0 340 260" className="w-full">
          {shots.filter((s) => selected.includes(s.id)).map((s) => {
            const d = s.curve.map((p, i) =>
              `${i ? "L" : "M"}${28 + (p.t / 42) * 300},${230 - (p.w / 45) * 205}`).join(" ");
            return <path key={s.id} d={d} fill="none" stroke={tone(s)} strokeWidth="2" />;
          })}
          <line x1="28" y1="230" x2="328" y2="230" stroke={T.ink} />
          <line x1="28" y1="25" x2="28" y2="230" stroke={T.ink} />
        </svg>
      </Panel>
    </div>
  );
}

/* ---- beans ----------------------------------------------------------- */
function Beans({ beans, activeId, setActive }) {
  return (
    <div className="grid gap-4" style={{ gridTemplateColumns: "repeat(auto-fill,minmax(300px,1fr))" }}>
      {beans.map((b) => (
        <Panel key={b.id} className="p-5" style={{
          borderColor: b.id === activeId ? T.trace : T.hair,
          borderWidth: b.id === activeId ? 2 : 1,
        }}>
          <div className="flex justify-between items-start">
            <Eyebrow>{b.roaster}</Eyebrow>
            {b.frozen && <Eyebrow style={{ color: T.trace }}>❄ frozen</Eyebrow>}
          </div>
          <div className="mt-2" style={{ fontFamily: SANS, fontSize: 19, color: T.ink }}>{b.name}</div>
          <div className="mt-1" style={{ fontFamily: MONO, fontSize: 11, color: T.inkSoft }}>
            {b.origin} · {b.process} · {b.roastLevel}
          </div>
          <div className="grid grid-cols-2 gap-3 mt-5">
            <Readout value={b.remainingDoses} label="Doses left" size={22} />
            <Readout value={Math.round((new Date("2026-08-25") - new Date(b.roastDate)) / 864e5)}
              unit="d" label="Off roast" size={22} />
          </div>
          <button onClick={() => setActive(b.id)} className="w-full py-3 mt-5" style={{
            fontFamily: MONO, fontSize: 11, letterSpacing: "0.14em", textTransform: "uppercase",
            background: b.id === activeId ? T.trace : "transparent",
            color: b.id === activeId ? T.panel : T.ink,
            border: `1px solid ${b.id === activeId ? T.trace : T.hair}`,
            borderRadius: 2, cursor: "pointer",
          }}>
            {b.id === activeId ? "In the hopper" : "Load this bag"}
          </button>
        </Panel>
      ))}
    </div>
  );
}

/* ---- shell ------------------------------------------------------------ */
export default function EspressoLogger() {
  const [tab, setTab] = useState("pull");
  const [shots, setShots] = useState(SHOTS);
  const [pending, setPending] = useState(null);
  const [activeBean, setActiveBean] = useState(1);
  const [selected, setSelected] = useState([3, 2]);

  const bean = BEANS.find((b) => b.id === activeBean);
  const ghost = shots.find((s) => s.beanId === activeBean)?.curve;

  const onFinish = (data) => { setPending(data); setTab("capture"); };

  const save = (form) => {
    setShots((prev) => [{
      id: Date.now(), at: "2026-08-25 09:41", beanId: activeBean,
      grindDial: form.grind, doseGround: form.dose, yieldFinal: form.yieldFinal,
      timeS: pending.timeS,
      meanFlow: +(pending.yieldAtStop / (pending.timeS - 6)).toFixed(2),
      balance: form.balance, rating: form.rating || 0, settleOffset: form.settle,
      notes: form.notes || "—", curve: pending.curve,
    }, ...prev]);
    setPending(null); setTab("shots");
  };

  const TABS = [["pull", "Pull"], ["capture", "Capture"], ["shots", "Shots"], ["beans", "Beans"]];

  return (
    <div style={{ background: T.paper, minHeight: "100%", padding: 20, fontFamily: SANS }}>
      <div className="flex flex-wrap items-baseline justify-between gap-3 mb-5">
        <div>
          <div style={{
            fontFamily: MONO, fontSize: 20, letterSpacing: "0.02em", color: T.ink,
          }}>
            SHOT<span style={{ color: T.trace }}>LOG</span>
          </div>
          <Eyebrow style={{ marginTop: 4 }}>
            Ascaso Dream PID · Mignon Single Dose · Themis Mini
          </Eyebrow>
        </div>
        <div className="flex items-center gap-2">
          <span style={{
            width: 7, height: 7, borderRadius: 99, background: T.flow, display: "inline-block",
          }} />
          <Eyebrow>Scale connected · 10 Hz</Eyebrow>
        </div>
      </div>

      <div className="flex gap-1 mb-5" style={{ borderBottom: `1px solid ${T.hair}` }}>
        {TABS.map(([k, label]) => (
          <button key={k} onClick={() => setTab(k)} className="px-5 py-3" style={{
            fontFamily: MONO, fontSize: 11, letterSpacing: "0.14em", textTransform: "uppercase",
            background: "transparent", border: "none", cursor: "pointer",
            color: tab === k ? T.ink : T.inkSoft,
            borderBottom: `2px solid ${tab === k ? T.trace : "transparent"}`,
            marginBottom: -1,
          }}>
            {label}
            {k === "capture" && pending && <span style={{ color: T.alert }}> ●</span>}
          </button>
        ))}
      </div>

      {tab === "pull" && <Pull bean={bean} onFinish={onFinish} ghost={ghost} />}
      {tab === "capture" && (
        <Capture pending={pending} onSave={save} onDiscard={() => { setPending(null); setTab("pull"); }} />
      )}
      {tab === "shots" && (
        <History shots={shots} selected={selected}
          toggle={(id) => setSelected((s) => s.includes(id) ? s.filter((x) => x !== id) : [...s, id])} />
      )}
      {tab === "beans" && <Beans beans={BEANS} activeId={activeBean} setActive={setActiveBean} />}
    </div>
  );
}
