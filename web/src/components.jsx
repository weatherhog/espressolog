import { T, MONO, SANS } from "./tokens.js";

export function Eyebrow({ children, style }) {
  return (
    <div style={{
      fontFamily: MONO, fontSize: 10, letterSpacing: "0.14em",
      textTransform: "uppercase", color: T.inkSoft, ...style,
    }}>{children}</div>
  );
}

// The headline shot time is the machine's own: pump on to pump off, the
// number on the display and the one recipes mean by "25-30 s" (counted from
// pump start, pre-infusion included). total_s is the scale's time, first
// drip to a flat weight, ~10 s shorter with pre-infusion. Shots without a
// display reading fall back to the press-to-stop span, then to the scale's
// time, and the label says which, so the three are never shown as one.
export function shotTime(shot) {
  if (shot.machine_timer_s != null) return { s: shot.machine_timer_s, label: "Time" };
  if (shot.pump_to_stop_s != null) return { s: shot.pump_to_stop_s, label: "Press→stop" };
  if (shot.total_s != null) return { s: shot.total_s, label: "Pour" };
  return { s: null, label: "Time" };
}

export function Readout({ value, unit, label, color = T.ink, size = 34 }) {
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

export function Panel({ children, style }) {
  return (
    <div style={{
      background: T.panel, border: `1px solid ${T.hair}`, borderRadius: 3, ...style,
    }}>{children}</div>
  );
}

export function Chip({ active, onClick, children, tone = T.trace }) {
  return (
    <button onClick={onClick} style={{
      flex: 1, padding: "10px 12px",
      fontFamily: MONO, fontSize: 11, letterSpacing: "0.1em", textTransform: "uppercase",
      border: `1px solid ${active ? tone : T.hair}`, borderRadius: 2,
      background: active ? tone : "transparent", color: active ? T.panel : T.inkSoft,
      cursor: "pointer",
    }}>{children}</button>
  );
}

export function Stepper({ value, onChange, step, decimals = 1, unit, min = 0 }) {
  const btn = {
    width: 40, height: 40, fontFamily: MONO, fontSize: 18, cursor: "pointer",
    border: `1px solid ${T.hair}`, background: "transparent", color: T.ink, borderRadius: 2,
  };
  const set = (v) => onChange(+Math.max(min, v).toFixed(2));
  return (
    <div style={{ display: "flex", alignItems: "center", gap: 8 }}>
      <button style={btn} onClick={() => set(value - step)}>–</button>
      <div style={{
        flex: 1, textAlign: "center", fontFamily: MONO, fontSize: 22,
        color: T.ink, fontVariantNumeric: "tabular-nums",
      }}>
        {value.toFixed(decimals)}
        {unit && <span style={{ fontSize: 12, color: T.inkSoft, marginLeft: 2 }}>{unit}</span>}
      </div>
      <button style={btn} onClick={() => set(value + step)}>+</button>
    </div>
  );
}

export function Tag({ children, tone = T.inkSoft }) {
  return (
    <span style={{
      fontFamily: MONO, fontSize: 9, letterSpacing: "0.1em", textTransform: "uppercase",
      color: tone, border: `1px solid ${tone}`, borderRadius: 2, padding: "2px 6px",
    }}>{children}</span>
  );
}

export { T, MONO, SANS };
