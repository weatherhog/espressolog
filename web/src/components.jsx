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
// machine_timer_s is the firmware's timer reading for the shot, and it is
// not always THIS shot's run. Before 0.16.1: the previous brew on a
// hot-water draw (the display runs no timer then) or with the display tap
// off; a flush pressed inside the 5 s settle (the timer is copied at
// completion), which leaves the flush's first seconds; and before 0.9.7 a
// misread " 95" idle frame, 9.5 s. From 0.16.1 (the timer-own-run fix)
// the firmware stores only a run it saw start, with two exceptions its
// CLAUDE.md lists: a flush pressed after pump-off but before the scale's
// stop is stored at its own full length, which can read above OR below the
// press-to-stop span p (a 1Cup flush after a 2Cup shot: 10.5 against 32);
// and a pour confirmed within 0.5 s of another run's last count carries
// that run.
//
// The rule, in order:
//  - a confirmed hot-water press (pump_line 3), any firmware: rejected --
//    the display runs no timer for it, so a stored one is another run's;
//  - with a usable p, any firmware: a lopsided window -- up to 9 s under
//    p (the 0a.7 lift shift plus drips; shot 212: timer 35.9, p 42.3), at
//    most 3 s over -- which also catches 0.16.1's late flush;
//  - 0.16.1+ with no usable p: trusted;
//  - older, no usable p: on a v3+ record (pump_line set) with no press
//    at all (pump_line 0), unchecked -- a missed hot-water press looks the
//    same and carries the previous brew; otherwise it stands alone if it is
//    a brew's length on 0.9.7+, else unchecked.
// A usable p: present, total_s > 0 (not a sub-5 g abort), stop_ms < 60000
// (a forced stop is not backdated).
//
// A rejected timer never disappears silently. The fallback is shown with
// the timer beside it: "disagreed" (≠) when a comparison or the hot-water
// press rejected it, "unchecked" (?) when nothing could check it. The
// cautious choice is visible, and a correct timer rejected because p is
// wrong (a borrowed press, a lift mid-pour) can still be read off the row.
// Not caught: a stale timer inside the window; on records with no switch
// data a stale timer >= 15 s (the display tap off); and on 0.16.1 a pour
// confirmed within 0.5 s of another run's last count, whose carried timer
// lands inside the window or has no p to check it.
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

// "ok": the timer is this shot's; null: there is none; "disagreed" or
// "unchecked": there is one and it is not shown as the time.
function timerVerdict(shot, p) {
  const m = shot.machine_timer_s;
  if (m == null || !(m > 0)) return null;
  if (shot.pump_line === 3) return "disagreed";
  if (p != null) {
    return p - m <= TIMER_UNDER_P_MAX_S && m - p <= TIMER_OVER_P_MAX_S ? "ok" : "disagreed";
  }
  if (firmwareAtLeast(shot.firmware_version, [0, 16, 1])) return "ok";
  if (shot.pump_line === 0) return "unchecked";
  return m >= MIN_BREW_S && firmwareAtLeast(shot.firmware_version, [0, 9, 7]) ? "ok" : "unchecked";
}

// Props for the time Readout. A rejected timer shows as a mark after the
// value and as visible small text under the label (a tooltip never shows
// on a tablet); the title repeats it in words for a pointer.
export function timeReadout(shot) {
  const t = shotTime(shot);
  const props = { value: t.s?.toFixed(1) ?? "—", unit: "s", label: t.label };
  if (t.timer) {
    const sign = t.timer === "disagreed" ? "≠" : "?";
    const m = shot.machine_timer_s.toFixed(1);
    props.mark = sign;
    props.note = `${sign} timer ${m}`;
    props.title = `machine timer ${m} s ${t.timer}; showing ${t.label.toLowerCase()}`;
  }
  return props;
}

// { s, label, timer }: timer is "disagreed" or "unchecked" when a machine
// timer was stored and is not the value shown, else null.
export function shotTime(shot) {
  const p = shot.pump_to_stop_s != null && shot.total_s > 0 && shot.stop_ms < 60000
    ? shot.pump_to_stop_s : null;
  const v = timerVerdict(shot, p);
  if (v === "ok") return { s: shot.machine_timer_s, label: "Time", timer: null };
  const timer = v; // null, "disagreed" or "unchecked"
  if (p != null) return { s: p, label: "Press→stop", timer };
  if (shot.total_s > 0) return { s: shot.total_s, label: "Pour", timer };
  return { s: null, label: "Time", timer };
}

export function Readout({ value, unit, label, color = T.ink, size = 34, mark, note, title }) {
  return (
    <div title={title} style={{ minWidth: 0 }}>{/* lets the note ellipsize in a grid cell */}
      <div style={{
        fontFamily: MONO, fontSize: size, lineHeight: 1, color,
        fontVariantNumeric: "tabular-nums", letterSpacing: "-0.02em",
      }}>
        {value}
        {unit && <span style={{ fontSize: size * 0.4, color: T.inkSoft, marginLeft: 3 }}>{unit}</span>}
        {mark && <span style={{ fontSize: size * 0.5, color: T.alert, marginLeft: 2 }}>{mark}</span>}
      </div>
      <Eyebrow style={{ marginTop: 5 }}>{label}</Eyebrow>
      {note && <div style={{
        fontFamily: MONO, fontSize: 9.5, color: T.alert, marginTop: 2,
        whiteSpace: "nowrap", overflow: "hidden", textOverflow: "ellipsis", maxWidth: "100%",
      }}>{note}</div>}
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
