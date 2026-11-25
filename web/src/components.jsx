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
// machine_timer_s on firmware before 0.16.1 is the firmware's last timer
// reading, which is not always THIS shot's run: the previous brew on a
// hot-water draw (the display runs no timer then) or with the display tap
// off; a flush pressed inside the 5 s settle (the timer is copied at
// completion), which leaves the flush's first seconds; and before 0.9.7 a
// misread " 95" idle frame, 9.5 s. From 0.16.1 (the timer-own-run fix) the
// firmware stores only this run's timer, so it is shown as stored.
//
// For older records the press-to-stop span p referees when it is usable,
// through a lopsided window: the timer may sit up to 9 s under p (the 0a.7
// lift shift plus drips; shot 212: timer 35.9, p 42.3), at most 3 s over.
// Without a usable p (no switch data, a missed press, an abort, a forced
// stop) the timer stands alone if it is a brew's length on 0.9.7+. A
// confirmed hot-water press (pump_line 3) never takes the timer.
// When the timer is rejected the fallback is marked disputed: p is not
// authoritative either (it reads high on a borrowed press, low on a lift
// mid-pour), so the row says the two disagreed rather than pick silently.
// Not caught on older records: a stale timer within the window (the tap
// off, or a stale value 0-9 s under p); a stale timer >= 15 s with no
// usable p (a missed hot-water press, the tap off with switches off); and
// a correct timer rejected because p is wrong, which shows marked.
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

// true: the timer is this shot's; false: there is none; "disputed": there
// is one and it was rejected.
function timerVerdict(shot, p) {
  const m = shot.machine_timer_s;
  if (m == null || !(m > 0)) return false;
  if (firmwareAtLeast(shot.firmware_version, [0, 16, 1])) return true;
  if (shot.pump_line === 3) return "disputed";
  if (p != null) {
    return p - m <= TIMER_UNDER_P_MAX_S && m - p <= TIMER_OVER_P_MAX_S || "disputed";
  }
  return m >= MIN_BREW_S && firmwareAtLeast(shot.firmware_version, [0, 9, 7]) || "disputed";
}

// Props for the time Readout: the value, its label, and when disputed a
// "?" mark with a tooltip giving the rejected timer.
export function timeReadout(shot) {
  const t = shotTime(shot);
  return {
    value: t.s?.toFixed(1) ?? "—", unit: "s", label: t.label,
    mark: t.disputed ? "?" : undefined,
    title: t.disputed
      ? `machine timer ${shot.machine_timer_s.toFixed(1)} s disagreed; showing ${t.label.toLowerCase()}`
      : undefined,
  };
}

// { s, label, disputed }: disputed means a machine timer was stored and
// rejected, so the value shown is a fallback the timer disagreed with.
export function shotTime(shot) {
  const p = shot.pump_to_stop_s != null && shot.total_s > 0 && shot.stop_ms < 60000
    ? shot.pump_to_stop_s : null;
  const v = timerVerdict(shot, p);
  if (v === true) return { s: shot.machine_timer_s, label: "Time", disputed: false };
  const disputed = v === "disputed";
  if (p != null) return { s: p, label: "Press→stop", disputed };
  if (shot.total_s > 0) return { s: shot.total_s, label: "Pour", disputed };
  return { s: null, label: "Time", disputed: false };
}

export function Readout({ value, unit, label, color = T.ink, size = 34, mark, title }) {
  return (
    <div title={title}>
      <div style={{
        fontFamily: MONO, fontSize: size, lineHeight: 1, color,
        fontVariantNumeric: "tabular-nums", letterSpacing: "-0.02em",
      }}>
        {value}
        {unit && <span style={{ fontSize: size * 0.4, color: T.inkSoft, marginLeft: 3 }}>{unit}</span>}
        {mark && <span style={{ fontSize: size * 0.5, color: T.alert, marginLeft: 2 }}>{mark}</span>}
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
