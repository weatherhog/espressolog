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
// drip to a flat weight, ~10 s shorter with pre-infusion.
//
// machine_timer_s is only trusted when it describes THIS shot's pump run.
// The firmware keeps the last timer it saw until a new one replaces it, so
// the stored value can be another run's: the previous brew on a hot-water
// draw (the display runs no timer then) or with the display tap off; a
// flush pressed inside the 5 s settle (the timer is copied at completion);
// and, before firmware 0.9.7, a misread " 95" idle frame stored as 9.5 s.
// So it needs a lever-A press (switch_direction: 1Cup or 2Cup) and must
// fit the press-to-stop span p, which is pump-off plus the drips before the
// weight went flat: ~1 s more than the timer on a clean shot. The window is
// lopsided on purpose. Every wrong timer above reads far BELOW p (a flush,
// 9.5 s) or comes without a lever-A press (a hot-water draw); but p itself
// reads up to ~7 s HIGH on a lift-terminated shot from detector 0a.7 or
// earlier (stop_ms overwritten, CLAUDE.md "Shot detection"; shot 212:
// timer 35.9, p 42.3), and there the timer is the right number.
// Not caught: a stale timer from the previous brew with the display tap
// off that happens to land inside the window.
// Otherwise the press-to-stop span itself ("Pump"), then the scale's own
// time ("Pour"); the label says which, so the three never pass as one.
const TIMER_UNDER_P_MAX_S = 9; // lift shift (~7 s) plus drips
const TIMER_OVER_P_MAX_S = 3;
export function shotTime(shot) {
  const m = shot.machine_timer_s, p = shot.pump_to_stop_s;
  if (m != null && p != null && shot.switch_direction != null && m > 0
      && p - m <= TIMER_UNDER_P_MAX_S && m - p <= TIMER_OVER_P_MAX_S) {
    return { s: m, label: "Time" };
  }
  if (p != null) return { s: p, label: "Pump" };
  if (shot.total_s) return { s: shot.total_s, label: "Pour" };
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
