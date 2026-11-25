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
// machine_timer_s is the firmware's last timer reading, which is not always
// THIS shot's run: the previous brew on a hot-water draw (the display runs
// no timer then) or with the display tap off; a flush pressed inside the
// 5 s settle (the timer is copied at completion), which leaves the flush's
// first seconds; and before firmware 0.9.7 a misread " 95" idle frame,
// 9.5 s. The press-to-stop span p cannot referee it alone either: p is the
// lead alone on a sub-5 g abort (stop_ms 0), ~7 s high on a forced stop
// (stop_ms >= 60000 is not backdated) or an 0a.7 lift while settling, low
// on a lift mid-pour or a scale-lost fault, and whole flushes high when a
// missed start press borrows an earlier one.
//
// So: a hot-water draw (a press, but not lever A) never takes the timer.
// Otherwise the timer stands on its own when it is a brew's length
// (MIN_BREW_S) on firmware 0.9.7 or later -- every wrong value above but
// the stale-tap one is a few seconds -- or, when shorter, if a usable p
// agrees with it. The stale previous-brew timer with the tap off is not
// caught; the timer-own-run firmware fix stores none there.
// Fallbacks say what they are: "Press→stop" (p), then "Pour" (first drip
// to a flat weight, ~10 s short of the machine on a pre-infused shot).
const MIN_BREW_S = 15;
const TIMER_AGREE_S = 3;

function firmwareAtLeast(v, want) {
  const m = /^(\d+)\.(\d+)\.(\d+)/.exec(v || "");
  if (!m) return false;
  const a = [+m[1], +m[2], +m[3]];
  for (let i = 0; i < 3; i++) if (a[i] !== want[i]) return a[i] > want[i];
  return true;
}

export function shotTime(shot) {
  const m = shot.machine_timer_s;
  const pressed = shot.pump_to_stop_s != null;
  const p = pressed && shot.total_s > 0 && shot.stop_ms < 60000
    ? shot.pump_to_stop_s : null;
  const hotWater = pressed && shot.switch_direction == null;
  if (m != null && m > 0 && !hotWater) {
    if (m >= MIN_BREW_S && firmwareAtLeast(shot.firmware_version, [0, 9, 7])) {
      return { s: m, label: "Time" };
    }
    if (p != null && Math.abs(p - m) <= TIMER_AGREE_S) return { s: m, label: "Time" };
  }
  if (p != null) return { s: p, label: "Press→stop" };
  if (shot.total_s > 0) return { s: shot.total_s, label: "Pour" };
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
