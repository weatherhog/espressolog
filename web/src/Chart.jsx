import { T, MONO } from "./tokens.js";

// Sparkline for list rows: shape only, no axes.
export function Spark({ curve, tone = T.trace }) {
  const w = 120, h = 34;
  if (!curve || curve.length < 2) return <svg width={w} height={h} />;
  const mx = curve.at(-1).t || 1;
  const my = Math.max(...curve.map((p) => p.w), 1);
  const d = curve.filter((_, i) => i % 4 === 0)
    .map((p, i) => `${i ? "L" : "M"}${((p.t / mx) * w).toFixed(1)},${(h - (p.w / my) * (h - 2)).toFixed(1)}`)
    .join(" ");
  return (
    <svg width={w} height={h} style={{ display: "block", flexShrink: 0 }}>
      <path d={d} fill="none" stroke={tone} strokeWidth="1.6" />
    </svg>
  );
}

// Overlay chart: one or more weight traces on the measurement paper.
// With a single trace, its flow rate is drawn too (right-implied 0–4 g/s
// band, same estimator as the firmware: slope over ~700 ms).
export function Overlay({ series }) {
  const CW = 720, CH = 420, PAD = { l: 46, r: 16, t: 18, b: 36 };
  const all = series.flatMap((s) => s.curve);
  const maxT = Math.max(30, Math.ceil(Math.max(0, ...all.map((p) => p.t)) / 5) * 5);
  const maxW = Math.max(40, Math.ceil(Math.max(0, ...all.map((p) => p.w)) / 10) * 10);
  const xs = (t) => PAD.l + (t / maxT) * (CW - PAD.l - PAD.r);
  const ys = (w) => CH - PAD.b - (w / maxW) * (CH - PAD.t - PAD.b);
  const path = (pts) =>
    pts.map((p, i) => `${i ? "L" : "M"}${xs(p.t).toFixed(1)},${ys(p.w).toFixed(1)}`).join(" ");

  const flowPts = [];
  if (series.length === 1) {
    const c = series[0].curve;
    for (let i = 7; i < c.length; i++) {
      const a = c[i - 7], b = c[i];
      if (b.t > a.t) flowPts.push({ t: b.t, w: ((b.w - a.w) / (b.t - a.t)) * (maxW / 4) });
    }
  }

  return (
    <svg viewBox={`0 0 ${CW} ${CH}`} style={{ display: "block", width: "100%" }}>
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

      {flowPts.length > 0 && (
        <path d={path(flowPts)} fill="none" stroke={T.flow} strokeWidth="1.2" strokeOpacity="0.65" />
      )}
      {series.map((s) => (
        <path key={s.id} d={path(s.curve)} fill="none" stroke={s.tone}
          strokeWidth="2.2" strokeLinecap="round" strokeLinejoin="round" />
      ))}
    </svg>
  );
}
