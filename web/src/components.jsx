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
// So: when a usable p exists it referees, through a lopsided window -- the
// timer may sit up to 9 s under p (the 0a.7 lift shift plus drips; shot
// 212: timer 35.9, p 42.3) but at most 3 s over it -- and only for a
// lever-A press (a hot-water draw's timer is never its own). A shot with
// a short p (a lift mid-pour) therefore shows Press→stop, not its timer.
// Without a usable p (no switch data, an abort, a forced stop) the timer
// stands alone if it is a brew's length on firmware 0.9.7+ (the flush and
// " 95" values are seconds long) -- except on a record from the
// switch-sensing era (pump_line set, format v3+) with no lever-A press:
// that is a hot-water draw or a missed press, and its timer may be the
// previous brew's, a full brew long. That also drops the timer on a v3+
// record taken with the switch tap off; the tap is never off in service.
// Not caught: a stale previous-brew timer on a record WITHOUT switch data
// (the display tap off, or pre-0.10.0 with no timer of its own). The
// timer-own-run firmware fix, not merged, would stop storing it.
// Fallbacks say what they are: "Press→stop" (p), then "Pour" (first drip
// to a flat weight, ~10 s short of the machine on a pre-infused shot).
const MIN_BREW_S = 15;
const TIMER_UNDER_P_MAX_S = 9;
const TIMER_OVER_P_MAX_S = 3;

function firmwareAtLeast(v, want) {
  const m = /^(\d+)\.(\d+)\.(\d+)/.exec(v || "");
  if (!m) return false;
  const a = [+m[1], +m[2], +m[3]];
  for (let i = 0; i < 3; i++) if (a[i] !== want[i]) return a[i] > want[i];
  return true;
}

function timerIsThisRun(shot, p) {
  const m = shot.machine_timer_s;
  if (m == null || !(m > 0)) return false;
  const leverA = shot.switch_direction != null;
  if (p != null) {
    return leverA && p - m <= TIMER_UNDER_P_MAX_S && m - p <= TIMER_OVER_P_MAX_S;
  }
  if (shot.pump_line != null && !leverA) return false;
  return m >= MIN_BREW_S && firmwareAtLeast(shot.firmware_version, [0, 9, 7]);
}

export function shotTime(shot) {
  const p = shot.pump_to_stop_s != null && shot.total_s > 0 && shot.stop_ms < 60000
    ? shot.pump_to_stop_s : null;
  if (timerIsThisRun(shot, p)) return { s: shot.machine_timer_s, label: "Time" };
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
