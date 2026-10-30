# Phase 0 status — as of 2026-10-19

Phase 0a (scale logging, stages 0–6) is **complete and in production**.
This file is the resume point: read it (and CLAUDE.md) before continuing.

## DEPLOY THE SERVER BEFORE FLASHING THE FIRMWARE — 2026-10-12

**DONE 2026-10-13, in this order, and the server was verified between the
two steps** — see *The deploy and the flash* below. The rule stays here
because it applies to every future round, not because anything is pending.

0.9.4 requires an `X-Espressolog-Ingest` header before it will delete an
uploaded record, and only a server built from this tree sets it. Flash first
and every `201` arrives unmarked: the device keeps the record, logs *"NOT the
ingest endpoint"*, and head-of-line blocks the whole queue. It is loud on
serial and shows up as "no new shots on the server", so it will be caught —
but the spool fills at ~490 records and then `writeShot` starts refusing, and
that loses shots.

Order: `make deploy` (which runs migration 009), confirm `/healthz`, then
`pio run -t upload`.

**0.9.4's invariant-2 guard needs `switches on`, not just `display on`.**
`machineBusy()` asks the machine whether the pump is running, and it has two
sources: the display's `Mode::TIMER` and the switch lines. Both are
NVS-persisted booleans that default off. With only the display tap live, a
framing gap longer than 2 s during pre-infusion makes the guard fall through
to switch state that nothing is writing, and it returns false mid-pour — and
this repo records one burst of ~6086 dropped display edges at pump start,
which its own comment calls 1.88 s of bus. That is ~120 ms of margin on a
number measured once. **Enable both taps, or accept that the guard is
best-effort.**

**And know what the hot-water guard costs — IT LOOKS TOO SHORT, 2026-10-19.**
A lever-B draw blocks flash writes, uploads and SNTP for a flat
<!-- fact:brew-guard-ms 75 s -->
`BREW_GUARD_MS` (75 s). The bench question this paragraph used to ask —
*"does lever B latch, and how long is a typical draw?"* — is half answered:
**it latches** (observed), a second press stops it (observed), and left alone
it runs for *"a couple of minutes"* before self-terminating.

**Grade that last clause: it is the operator's recollection, not a
measurement.** A draft headed this paragraph "IT IS TOO SHORT, **measured**"
— the latching is measured, the duration is not, and the whole
too-short conclusion rests on the duration.

**If the recollection holds, the guard expires while the pump is running.**
Note the mechanism: `brew_guard.cpp:5` backdates to the **press**, so the
75 s runs from the press, not the release — a draft said "released, ~250 ms
after the press", which is wrong and makes the guard marginally shorter.

**And 75 s does NOT come from 2Cup.** `brew_guard.h:27-28` sizes it from
2Cup's **factory-default 60 s plus margin**. A draft wrote that the 75 s came
from 2Cup's then-unknown stored duration and that the guess "was low" now
that 2Cup is known to be 45 s — incoherent, since 45 < 75. What would make
the guard insufficient is the water draw, which has nothing to do with 2Cup.

**Edge-driving the guard is the obvious direction and is NOT sufficient on its
own** — a draft presented it as the fix. The second press is a real stop
signal on a line already polled, but **the machine self-terminates without
one**, and then `busy()` never clears: the latched-busy-forever failure
the latched-busy-forever comment in `brew_guard.cpp` documents, which blocks flash writes indefinitely and
is strictly worse than a guard that is sometimes short. **Any edge-driven
design needs an upper bound too.** The cost meanwhile is bounded (`pending_w`
holds 8 weighings before it drops them). CLAUDE.md carries the full entry. Back up the database first — 009 is irreversible and its
header carries two dry-run queries to run against a copy beforehand.

## The deploy and the flash — 2026-10-13

Server first, verified, then the board. Both are done and both were checked
after the fact rather than assumed.

### Migration 009 on production, and why the dry run earned its place

Production was at `user_version = 8` with **137 shots**. Backed up first to
`/var/lib/espressolog/backup-pre009-20261013-104824.db` — `integrity_check
ok`, 137 shots in the copy — because 009 is irreversible.

**Name the two statements by PREDICATE, never by number.** 009 says so in as
many words, because they were reordered and "statement 1" then meant
different things in different drafts — and the file's own dry-run block
labels them in the opposite order to the one they execute in. An earlier
version of this section used numbers and got them crossed, attaching
`18.109` to the literal-triple predicate. It belongs to the other one.

| Predicate | What it matches | Dry run on production |
|---|---|---|
| **literal-triple** (runs first) | `grind_dial=2.4 AND brew_temp_c=93.0 AND preinfusion_s=0.0`, manual, no weighing — shot 3's fabricated recipe, dose `18.0` | **1 row — shot 3**, which is the pass condition 009 states |
| **3-decimal** (runs second) | manual with a dose carrying 3 decimals, which the ≤2-decimal capture stepper cannot produce | **20 rows, not the 17** the working snapshot listed |

**The dry run found three rows no snapshot had shown** — shots **87, 90 and
105**, all at `17.911`. That matters because 009 warns a nulled dose is only
recoverable if the same value survives elsewhere as a *measured* one, and
post-snapshot rows were each seeded from a different preceding dose.
**`17.911` is measured on shots 51 and 108**, so the three are recoverable on
the same footing as the 17.

**The 3-decimal statement does NOT test for a missing weighing row, and an
earlier draft said it did.** It has no `NOT EXISTS (… weighing …)` clause at
all; 009 is explicit that it matches on the rounding *rather than* on
"manual with no weighing", because the latter would also catch a dose a human
genuinely typed on an unweighed shot. That every matched row happens to lack
a weighing is an observation about this database, not the predicate.

**Measured on the backup, after the fact** (`sqlite3 -readonly` against
`backup-pre009-…db`):

| | |
|---|---|
| rows with `dose_source='manual'` | **21** |
| of those, with a weighing row | **0** |
| caught by literal-triple | 1 (shot 3) |
| caught by 3-decimal | 20 |
| **manual rows matched by neither** | **0** |

Only three distinct manual doses ever existed: `17.911`, `18.109`, and shot
3's literal `18.0`.

**Do NOT write this up as "0 % false negatives".** An earlier draft did, and
it is the wrong frame twice over. 009 predicts its own miss class — a
fabricated dose seeded from a measured value with **≤2 decimals**, which is
arithmetically indistinguishable from human input and which it expects to be
*most* of the newer damage. That class is still large here: **38 of 81
measured doses (47 %)** round to 2 places, against the 29/48 (60 %) 009
measured on the older snapshot. Shot 3 is itself an instance of it — dose
`18.0`, caught only because its recipe triple is unique.

So the honest statement is narrower and stronger than a percentage: **no
manual-attributed row survived either predicate**, and the three dose values
that ever appeared were all accounted for. Whether a fabricated dose was ever
written that looks exactly like human input is **not decidable from the
data** — 009 says as much, and says cleaning it would need per-shot judgement
rather than a predicate. What is closed is the source: `Capture.jsx` no
longer PATCHes untouched fields, so the population cannot grow.

The migration was then simulated on a copy before being run for real —
**56 NULL / 81 measured / 137 shots**, shot 3's recipe cleared, the 0f pool
intact. The live result **matched the simulation exactly**, which is the
check that makes the simulation worth doing.

After `make deploy`: service **active, `NRestarts=0`** (not crash-looping),
`user_version = 9`.

### What was verified on the live server

| Check | Result |
|---|---|
| `/healthz` | 200 |
| POST with a garbage body | **422 with `X-Espressolog-Ingest: 1`** |
| typo'd path `/api/v1/ingestt` | **404** |

The last one is the important one. Before the `/api/` guard, an unmatched
path fell through to the SPA handler and returned **200 with an HTML body** —
which the device read as a successful ingest and deleted the record on.

**But the 404 closes only half of it, and saying "that failure is closed"
overstates what was tested.** `api.go` is explicit: the guard covers
`/api/<typo>` **only**. The example that originally found the hole — a POST
to `/espressolog/api/v1/ingest` — does not begin with `/api/`, still reaches
the SPA handler and still returns 200 with HTML. What covers *that* is the
`X-Espressolog-Ingest` marker, i.e. row two of this table. **Two different
holes, two different patches**; both were verified above, but they are not
the same check.

### The firmware

`pio run -t upload`, 1192528 bytes, hash verified. Banner:

```
# espressolog 0.9.4 (detector 0a.6)
# LittleFS: 16/1920 KB used, spool ready, 0 pending
# display bus on (clk=GPIO4 data=GPIO5)
# flow on (GPIO6) | switches on (1cup=GPIO7 2cup=GPIO15 steam=GPIO16 water=GPIO17)
```

**`switches on`** — the toggle 0.9.4's invariant-2 guard needs, which the
section above warns is NVS-persisted and defaults off. All three taps are
enabled.

**`net status` confirms the endpoint is the same origin the ingest marker was
verified on.** That was the one link in the marker contract not checkable
from the server side: the device's endpoint lives in NVS, and a board pointed
somewhere else would have met an unmarked 200 and kept every record.

### Nothing is quarantined and the spool is empty

`ls` returns `0 record(s), 0 bytes` with **no `QUARANTINED` line**, and
`0 pending`. Nothing is stranded and the queue is not blocked.

**How much that proves took two wrong answers to pin down, in opposite
directions.** Both are kept, because each is a plausible misreading of the
same code.

- A first draft said it proves `/spool/bad` was never created. **Too strong
  on its own**: `Spool::list` prints the QUARANTINED line under
  `if (quarantined)` — a count of non-directory files *inside* the directory
  — so an **empty** `/spool/bad` prints nothing and is invisible to every
  command the firmware has.
- The correction then said an empty `/spool/bad` is "exactly what the wedge
  leaves behind". **That inverts the mechanism.** The wedge *begins* with a
  successful quarantine: `net.cpp` does `mkdir` then `rename`, and that first
  rename moves a real record in. A wedged board therefore has a **non-empty**
  `/spool/bad`, and `ls` names it. The *"the rename fails"* line in
  `Spool::oldest()`'s comment describes the **second** iteration — renaming
  the directory into itself — and empties nothing.

**So the clean `ls` does rule out an un-cleaned wedge**, which is the state
that matters. What it cannot rule out is an empty `/spool/bad` left behind by
a manual `rm bad/<name>`, a `format`, or a first rename that failed — none of
which is recorded as having happened.

**Name the right function.** The wedge was in **`Spool::oldest()`**: `"bad"`
sorts before both `s-` and `w-`, so it won the pick forever. `pendingCount()`
is only a diagnostic counter — every caller is a printf.

**`pendingCount()` did NOT already filter directories, and a draft said it
did.** The `isDirectory()` test landed in **713899f**, the same commit as the
wedge fix; `713899f^` — the 0.9.0 that ran on the board until this flash —
counts every entry. Consequence worth keeping: **any pre-2026-10-13 "N
pending" reading is inflated by one whenever `/spool/bad` exists**, including
the figure the `net: STUCK` message quotes. The draft got this by reading a
*later* commit's source and calling it history, which is the third instance
of that error in one sitting.

**Either way it is now inert**: 0.9.4's picker skips directories.

### What the flash did and did not settle about the display tap

With the machine off, `status` reports `display=[] mode=none (STALE)`,
`frames=0 dropped_edges=0`, all four switches `idle`, `flow last=NEVER`.

**`frames=0` alone is ambiguous — but `pins` resolves it, and a draft of this
section said nothing could.** The documented swapped-pair signature is
`frames=0` **with both lines idling high** (CLAUDE.md, 2026-10-09), because
J5's 4.7 K pull-ups sit on the *machine's* rail. `pins` reads:

```
# pins: flow/GPIO6=0 | 1cup/GPIO7=0 2cup/GPIO15=0 steam/GPIO16=0 water/GPIO17=0 | clk/GPIO4=0 data/GPIO5=0
# expected at rest: flow=1 (idles high on the board pull-up), all four switches=0
```

Every machine-sourced pull-up is dead — including the flow line, which the
firmware itself says should read **1**. That is an unpowered machine, and it
is not the swap signature. `printPins()` cites CLAUDE.md for exactly this
discrimination, so the claim that nothing distinguishes the two was wrong
against a comment in the function being quoted.

**It does not prove the taps work.** With the machine off a swapped pair
reads 0/0 as well; what the reading does is remove any need for a wiring
hypothesis to explain `frames=0`. **The first `status` with the machine ON is
still what confirms the bus** — one line, no setup.

### A deploy-host note

`espressolog.lan` is the LXC's **DHCP-reserved hostname** — not the AdGuard
rewrite, which is the separate `espresso.example.com → espressolog.lan` entry
in the table above. Either way it does **not** resolve from the dev Mac, so `make deploy` needs the host passed
explicitly:

```
make deploy HOST=root@<the LXC's address>
```

The address is deliberately not recorded in this repo — `README.md` states
that every hostname, IP and MAC in the docs is a placeholder, and that rule
covers this one. `server/Makefile` defaults to
`root@espressolog.lan`, which works from anything using the LAN resolver and
fails from anything that is not.

## What runs where

| Piece | Where | State |
|---|---|---|
| **Firmware 0.9.4 is what is ON THE BOARD (flashed 2026-10-13)**, detector 0a.6. 0.9.1 through 0.9.3 were built and never flashed, so no shot record will ever carry them. **Caveat: three different binaries shipped as "0.9.0"** — the `flowSetEnabled` behaviour change landed before the bump, so `shot.firmware_version` cannot separate them. Shots 110–112 are all excluded anyway. | ESP32-S3 on a USB charger by the machine | both Bookoos bound: yield `aa:bb:cc:dd:ee:01`, dose `aa:bb:cc:dd:ee:02`. **All three taps were fitted and live on 2026-10-08** — J5, J2 and J4 — and the breadboard was then disassembled; see the bench section. The display bus **drops ~1 % of edges** during a flush, cause open (an earlier reading of `dropped=0` was taken before the other taps existed). 0.8.0's parser, written blind against the 0e captures, **was validated against a real brew**: mode flipped `temp` → `timer`, counted `042/080/112/143/174/206` in tenths, returned to `temp 95 C`. **0.8.1 and 0.8.2 both add `beginCapture()`** — 0.8.1 was flashed, then `display.cpp` changed again, so the board briefly ran a 0.8.1 that no longer existed in the repo; bumped to 0.8.2 and reflashed rather than leave that ambiguity. No shot record carries either version. See 0e. |
| Go server + SQLite + PWA | Proxmox LXC `espressolog`, Debian 13, `espressolog.lan` (DHCP-reserved) | systemd `espressolog.service`, db at `/var/lib/espressolog/espressolog.db` |
| HTTPS | Caddy on the same LXC, `https://espresso.example.com` | Let's Encrypt via Cloudflare DNS-01; CF token in `/etc/caddy/env` |
| DNS | AdGuard Home rewrite `espresso.example.com → espressolog.lan` | resolution is LAN-only; no public A record (challenge TXT only) |
| Deploys | `make deploy` in `server/` (web+binary), `pio run -t upload` in `firmware/` | ESP talks plain HTTP to `:8080` directly, never through Caddy |

## The chain carried machine data end-to-end for the first time — shot 109, 2026-10-07

The first shot carrying data the machine told us about itself:

| id | yield | flow | `boiler_temp_start_c` | `machine_timer_s` | `setpoint_touched` | fw |
|---|---|---|---|---|---|---|
| **109** | 70.9 g | 4.31 g/s | **95.0** | **16.0** | 0 | 0.8.2 |
| 108 | 37.6 g | 1.98 g/s | — | — | 0 | 0.8.0 |

Machine display → J5 tap → ESP32 → spool v2 → upload → server decode →
database. Every link had been built and tested separately; none had
carried a live draw.

**Shot 109 was a FLUSH WITH NO BASKET, and it is `excluded = 1`.** 70.9 g
at **4.31 g/s** is 2.2× the same morning's shot 108 and roughly **12 σ
above the 0f mean of ~1.72 g/s** (σ 0.21) — water meeting no puck
resistance at all, which is exactly what it was. Flagged in the database
with `exclude_reason = "flush, no basket — first end-to-end test of the
J5 display tap (2026-10-07)"` so it cannot silently join a 0f pool, where
at this magnitude one row would move the result on its own.

**The anomaly was caught from the numbers before anyone said what the draw
was**, which is the useful part: 4.31 g/s against an 0f baseline is a
12 σ outlier, and the file would have stopped on it regardless.

None of it weakens what follows: bytes are bytes, and the data path is
what this row proves.

**A scale check, which it passes cleanly — and that is the real content.**
The failure most likely in a first end-to-end run is a units error, and
the wire carries the timer in **tenths of a second**. `machine_timer_s`
reading **16.0** rather than 160 or 1.6 is what this row proves.

Beyond that, only plausibility, and the obvious check is invalid.
70.9 g over the machine's 16.0 s of pump time is **4.43 g/s average**,
against the scale's **4.31 g/s**. Same region, so nothing contradicts —
but it is not a precise cross-check and must not be written as one:
`mean_flow_gps` is measured only from 5 g out to 80 % of final (so 51.7 g
of this pour), while `machine_timer_s` is pump-on to pump-off and
`yield_final_g` includes post-pump settling. A first draft divided total
yield by the windowed mean and called the 16.45 s result agreement with
16.0 s; that compares three mismatched quantities, and its own logic runs
backwards — the slow first-drip and tail should make the true time
*longer* than that estimate, not shorter.

**What this one row actually validates** — narrower than a first draft
claimed:

| Claim | Status |
|---|---|
| 113-byte **v2 header** round-trips device → server | ✓ all three fields populated |
| migration **008**'s three columns | ✓ |
| the display parser (written in 0.8.0, unchanged through 0.8.2) | ✓ temperature read, timer carried |
| `record.go`'s **v1** dispatch | ✗ **not demonstrated in production** |

The v1 claim was wrong twice over. Shot 108 is **not** a v1 record — spool
format v2 shipped *with* firmware 0.8.0, so every 0.8.0 row is v2 with the
display off. And the last genuine v1 writer, 0.7.0, stopped at
**2026-10-04 07:07 UTC**, while the current binary was deployed at
**18:41 UTC the same day** — so **no v1 record has ever been decoded by
the running server.**

It is covered by Go unit tests (`record_test.go`, `store_test.go` pin v1
fixtures), which is not nothing. But this is the path that guards
**invariant 6**, and replaying one archived v1 record through the current
binary would turn a unit test into production evidence. Worth doing.

**`setpoint_touched` has never been observed reading 1 end-to-end.** Both
rows read 0, so the column is so far indistinguishable from one hard-wired
to zero. The complementary test — edit the setpoint *during* a pour and
confirm a 1 lands in the record — is what proves the field carries signal,
and it has not been done.

**That said, `clearSetpointTouched()` was validated by accident, which is
the best kind.** The latch was *set* going into shot 109 — jumpers had been
swapped live minutes earlier, the measured mechanism below — and it
cleared at pour start under real conditions. The host test asserts that;
this is the production evidence.

**The honest limit on `boiler_temp_start_c = 95.0`.** Three legs, and the
third is the strongest:

- below ~88 °C the display **demonstrably tracks the real boiler** — it
  climbs one degree at a time and jitters ±1 °C;
- at 95 it holds **±0 °C for a minute at a time**, which no PID boiler
  does;
- during post-steam cooldown it **blinks ` 95` while the boiler is
  demonstrably hotter than that** — the one case where we independently
  know the boiler is *not* at the displayed value.

**So the precise statement is narrower than "the column is meaningless":**
a reading **at the setpoint value** cannot be distinguished from the
setpoint; a reading **below ~88 °C is a real temperature**. Nobody pulls a
shot at 82 °C, so in practice this column means "at setpoint" — but it is
not uniformly uninformative and should not be written as if it were.

`008_machine_signals.sql`'s comment used to assert the opposite outright
("this is what the machine said it had"); **it has been corrected in
place**, because CLAUDE.md tells readers to read the schema first and that
comment is the authority for anyone touching the data model.

## The bench session, 2026-10-08 — all seven inputs live, 0d end to end

Everything on the breadboard, all three T-pieces in the machine, firmware
0.9.0. What it settled and what it opened.

### Settled

<!-- fact:divider-j4 100 K + 220 K -->
<!-- fact:digmesa-k-factor 2382 pulses/L -->

| | |
|---|---|
| **`selftest`** | **500/500, 0 glitches, 0 ring drops** at 100 Hz — 6.3× the rate at the sensor's rated max flow (0.40 L/min × 2382 = 15.9 Hz). The one thing host tests cannot establish, because a real shot has no independent count to check against. |
| **All four switches** | correct GPIO mapping, no false reprogram. *(The "taps of 345–480 ms" this row carried is **one session's** presses — 2026-10-19 logged 195-295 ms and 80-90 ms. **The pump was watched answering only the six fast ones**, so the latching evidence is those; the longer ten give width only. See CLAUDE.md.)* |
| **Lever B** | **plug inspection → functional.** B up logged `water`, B down logged `steam` |
| **J4 first contact** | **no `E01`.** The 100 K + 220 K is energised and the machine has not noticed — the tap that faulted it on 2026-10-03 |
| **Worst poll gap** | **10 ms** on 2026-10-08 against that session's ~400 ms presses — **both halves of that ratio are superseded.** 2026-10-19 measured **42-57 ms** against **80-90 ms** presses, and missed two of six. The 40:1 headroom this row claimed is ~1.6:1. See the poll-gap reconciliation below; polling still stays, for reasons that are no longer this number |
| 0d end to end | the PATH works — `inlet_pulses` reaches `shot_sample`. **What the numbers mean is unresolved**; see `docs/flowmeter-j4.md` |
| **K-factor** | datasheet **2382 pulses/L** for our exact part. In-circuit counts are ~5× that and **what they are is unresolved** — see `docs/flowmeter-j4.md` |

**The 0d chain is proven by the data, not by inspection.** Shot 109 (fw
0.8.2, no tap) stores `inlet_pulses` NULL; shots 110–112 (fw 0.9.0, tap live)
store integers. That is the distinction `SPOOL_FLAG_FLOW_TAP` exists for,
and the one the server destroyed until 2026-10-07 by binding NULL for every
record.

**Shot 110 is the one that proves it.** Its 46 samples are all `0` — not
NULL. A stored zero that *is* a measurement ("the tap was live and no water
moved") is exactly what the flag buys, and without it that zero is
indistinguishable from the placeholder. 111 and 112 climb monotonically from
0 to 100 and 927. 110–112 are marked `excluded` —
they are calibration flushes, not shots.

**1027 of 1209 counts landed inside shot windows.** The missing ~180 is flow
between cycles and before each pour confirmed, which is the documented
two-gap behaviour and closes when 0c supplies the pump-start edge.

### Opened, and SMALLER than first written: the display bus drops ~1 % of edges

**`frames=3677 dropped_edges=5812`.** The first write-up of this called it
"more lost than captured" and made it the reason to stop work. **That was a
units error of about two orders of magnitude.** `dbus_frames` counts
completed frames (66, 67, or 133 bits merged); `dbus_dropped` counts
individual ISR edges. 3677 frames is on the order of 250–490 k edges, so
5812 dropped is **~1 %** of the bus. The serial line printed the two
counters side by side unlabelled, which is what invited it; 0.9.1 prints
`dropped_edges` with a percentage instead.

~1 % of digit readings lost in bursts is still worth closing —
`boiler_temp_start_c` and `machine_timer_s` (the DB columns;
`machine_timer_dl` on the wire) both come off this bus — but it
is a defect to schedule, not a reason to stop.

**Two candidate causes, and the session's own data disfavours the first.**

- *The spool's flash write.* Each cycle closes a record and writes to
  LittleFS, blocking tens of ms against a ring holding ~50 ms of bus. But
  `pollSwitches` runs in the same `loop()` and measured a **worst poll gap of
  10 ms** across the whole session. A tens-of-ms stall would have shown
  there and did not.
- *A noisy GPIO4 firing far above 9.9 kHz.* Consistent with both readings,
  and with the 2229 flow-ring drops measured on a floating lead the same
  evening. **This is now the leading suspect**, against the first write-up.

**What the data does NOT support:** that these drops accumulated *during the
flush*. All three readings (835/0, 7545/20016, 3677/5812) are single
cumulative samples since boot, with no before-flush value recorded. The
counters were free-running and nothing could reset them — `displaySetEnabled`
had the same early-return defect that was fixed in `flowSetEnabled` in the
same commit. **0.9.1 makes it idempotent**, so `display on` now zeroes both
counters and a per-event figure is obtainable.

**Settling the K-factor without the analyser — `flow filter <us>`, 0.9.1.**
The glitch window is now runtime-settable, because the obvious instrument is
the wrong one: the logic analyser tap on `#` is what produced the `E01` fault
on 2026-10-03, and its corrected 8.2 K conditioning has never been re-tested,
while the ESP tap at 100 K + 220 K now has been. So reuse the proven tap and
vary the filter instead.

**Sweep four widths, do not compare two.** The first version of this called
for 2 ms against 20 ms and could not have worked — both outcomes give the
same answer:

| | |
|---|---|
| a 20 ms window starts rejecting at | 50 Hz = **8.5 ml/s** |
| rated ceiling 0.40 L/min | 25 ms — only **27 %** clear of the window |
| hot-water draw at 10 ml/s | 17 ms — **below** it: every second pulse lost, a **50 %** drop |
| the "it was ringing" verdict | a drop toward 2400/5915 = **41 %** |

A filter that is too wide and ringing that is real are indistinguishable at
two points. The ~140 ms spacing quoted to justify 20 ms is **espresso** flow
(1.2 ml/s); the experiment runs on a **flush**, which is unrestricted — and
neither calibration flush's duration was recorded, so which regime they ran
in is unknown.

### The test that actually separates water from interference: vary the FLOW RATE

**Do this one first.** The width sweep below is secondary and cannot settle
the question by itself.

Everything measured so far has been a free flush at one rate, which is the
same confound that invalidated the 3.32/3.34 ratio argument three paragraphs
up: at a fixed flow rate, a count proportional to *volume* and a count
proportional to *time* are indistinguishable. Varying the rate separates them
outright, and you already need to do it:

**Measure pulses per SECOND, not per gram.** Per-gram is confounded on a
shot: the inlet water splits between cup, puck retention and the 3-way dump,
so the scale sees maybe 60 % of it. Per-second needs no volume at all.

**THIS TEST HAS BEEN RUN — 2026-10-09 — and the table below is kept only as
the prediction it made.** Two shots measured **~22 Hz**, which is none of the
three columns. The 50 Hz column is **dead** (see `docs/flowmeter-j4.md`), the 5915
column rests on a figure now withdrawn, and what the counts measure is
unresolved. Do not re-run this expecting it to decide anything.

| | ~~pump-synchronous (50 Hz)~~ **dead** | water @ 2382 | ~~water @ 5915~~ **withdrawn** |
|---|---|---|---|
| flush, ~8 ml/s | 50 Hz | 19 Hz | 47 Hz |
| **shot, ~2 ml/s** | 50 Hz | **4.8 Hz** | 11.8 Hz |
| **MEASURED, two shots** | — | — | **~22 Hz** |

The flush row is still worth keeping for why two careful weighed runs settled
nothing: at that rate the two leading candidates were **three hertz apart**,
so the flush regime is exactly where they overlap.

**On a shot they separate 4–10×.** ~50/s means the signal is swamped by the
pump and the flowmeter is a worse pump detector than 0c already is. 5–12/s
means it is water, and the figure says which K-factor is right.

Pulses before, pulses after, and **a stopwatch**. No new hardware, and a real
shot is an open item anyway — every run so far has been a flush.

### The width sweep, secondary

`flow filter` at **2000, 5000, 10000**, one flush each, weight and duration
recorded.

**A plateau does NOT mean the counts are water.** Any periodic source whose
period exceeds every width swept plateaus identically, and two are live here:
**100 Hz mains/triac coupling** (10 ms — which `machine_io.h` deliberately
does *not* reject) and **one count per vibratory-pump stroke** (50 Hz, 20 ms).

**The 50 Hz pump-stroke reading is DEAD** — it fitted the flushes with no
free parameters (1209 counts over ~24 s is ~50 Hz, 164 over ~3 s is ~51 Hz)
and the shots came in at ~22 Hz. Kept here because it fitted well, which is
exactly why it is worth recording as refuted.

**20 ms is excluded as an anchor**, not swept for the verdict: it starts
rejecting real pulses at 8.5 ml/s, inside the band a free flush runs at, so a
missing plateau there carries no information. **5 ms is marginal** in the
other direction — at 2.47 counts per real pulse, some ringing must already
span ~3 ms. So **2 vs 10 is the comparison that carries weight**:

- **2 and 10 agree** → narrow-window ringing is not being counted.
- **2 ≫ 10** → it is, and the 2 ms figure is inflated by that factor.

**Replicate.** Run-to-run K-factor spread at a *fixed* width is already 10 %,
so a step smaller than that is not distinguishable from flush-to-flush
variation at n=1. Two runs per width, minimum.

Note pulses= **before and after each run and subtract**. `flow on` resets the
glitch and drop counters but **not** `pulses=` — that is deliberate (the
detector takes a difference across it) and the 0.9.1 command text wrongly
told the operator otherwise.

Host cases 5b and 5c pin both outcomes, and 5b now asserts the getter and
uses off-grid widths — a setter that merely bucketed its argument into
{0, 2000, 20000} passed the earlier version of both cases.

The filter is **not persisted**: a width left from a bench run must not
silently shape months of shot data, so a reboot restores 2 ms. Within a
session it does shape every shot, and nothing in the record says which width
produced it — which is why `flow filter` is refused during a shot.

**The dropped-edge experiment, now runnable:** `display on` to zero the counters, one
flush with the yield scale disconnected so nothing is spooled, then read. Low
`dropped_edges` implicates the spool; high implicates the ISR.

### Two defects the host tests could not have found

- **`flow on` did not reset the diagnostics when flow was already on.**
  `flowSetEnabled` opened with `if (on == flow_on) return;`, so the
  documented clean-slate command silently did nothing and a stale glitch
  count of 1384 was read as current during calibration. Both enable paths
  are now idempotent, and the interrupt is detached before the ring is
  zeroed because re-running on a live tap is now reachable.
- **An unterminated J4 cable counts mains hum.** With the T-piece out of the
  machine and `flow on`, GPIO6 logged 546 counts and 9416 glitches from a
  100 K series into a floating lead against a 220 K pull-down. Harmless, and
  the diagnostics reported it correctly — but do not leave `flow on` with J4
  unplugged, and do not read a count taken in that state.

Also seen: hot-plugging the J5 tap on a live bus can latch
`setpoint_touched`, because the garbage edges mimic the ~410 ms blink. It
clears at the next pour start.

## 0c and 0d firmware — written 2026-10-07, bench-testable without the machine

**0.9.0 added the code for both remaining taps. Both were wired and proven
on 2026-10-08** — see the bench section above; this section records what the
firmware was when it was written. The
point of writing them now is that the breadboard is still up and the machine
is closed: everything that can be settled without voltage should be settled
before the perfboard is drilled, because after that the pin map is copper.

What landed:

| | |
|---|---|
| `firmware/src/machine_io.{h,cpp}` | all the logic: glitch filter, debouncer, press duration, reprogram threshold, event queue |
| `firmware/test/host/machine_io_test.cpp` | 13 cases, all four mutations checked |
| `detector.{h,cpp}` | `feed()` takes the free-running count; samples store pulses since t=0 |
| `spool.h` / `record.go` / `store.go` | `SPOOL_FLAG_FLOW_TAP` (bit 4) |
| `main.cpp` | ISR + polling + `flow`/`switches`/`pins`/`selftest` |

**The logic is in its own unit for the reason `machine_context.h` records:**
the last time this kind of logic lived inline in `main.cpp` it had no tests
and shipped two bugs a fully green suite could not see.

### Two decisions worth knowing before the bench

**The switch lines are POLLED, not interrupt-driven.** A press is tens to
hundreds of ms against a ~5 ms loop, so resolution is ample, and polling
cannot be storm-triggered by a floating line the way four more edge
interrupts could. The risk it does carry is a press missed inside a long
`loop()` stall — so the worst poll gap is **measured and printed in
`status`** rather than assumed. If the bench shows gaps near a press length,
this becomes interrupt-driven, and the measurement will be the reason.

> **THAT TRIGGER FIRED ON 2026-10-19 AND THE FIRST WRITE-UP DID NOT SAY SO.**
> The bench showed gaps of **42-57 ms** against presses of **80-90 ms** — not
> merely "near a press length" but **~60 % of one** — and **two of six presses
> went unlogged**. This was a pre-registered decision rule with its evidence
> delivered, and the session recorded the gap, recorded the misses, and left
> the rule unmentioned.
>
> **It does not fire on its own merits, though, and that is the one thing
> saving it.** The rule was written for the *polling* design; the 2026-10-19
> session also established that the switch log **is not an instrument below
> ~100 ms**, so interrupt-driving it would sharpen a measurement this project
> has no use for — presses are tens to hundreds of ms and the guard runs from
> the press. **And the analyser redo supersedes both**: microsecond edges on a
> J2 junction answer the pulse-width question that motivated the rule, without
> four more edge interrupts on machine-facing lines.
>
> So: **the rule is retired, not satisfied** — and retiring it deliberately is
> the point, because a pre-registered trigger that fires and is silently
> dropped is worth less than no trigger at all.
A missed press is also not silent: the display bus runs the machine's own
shot timer, so a brew with no switch event still leaves a trace.

**Nothing persists a switch event yet.** Presses go to serial and the live
socket; `machine_event` is untouched. That is deliberate — persistence is
not what gates the perfboard, and `machine_event`'s schema still assumes
ONE lever (`direction` is `CHECK IN ('down','up')`), which cannot express
"lever B up" versus "lever A up". That migration is its own job.

## 2026-10-09 — the datasheet arrived, and what the J4 counts are is UNRESOLVED

Two sessions in one day. The datasheet findings are in CLAUDE.md's 0d
section; this is what the bench produced.

**The bracket, from a single capture polled every 3 s:**

| | pulses | glitches | flow ring | display ring |
|---|---|---|---|---|
| idle before, **7 samples** at a 3 s poll | frozen | frozen | 0 dropped | frozen |
| pump running, 33.2 s sampled | +736 (**761** with the gaps) | +4248 | **629 dropped** | **+6086 at start**, then +164 |
| idle after, **6 samples** | frozen | frozen | 629 | frozen |

Nothing in either idle stretch, everything while the pump runs. Both taps.
**25 counts fall in the unsampled gaps** at pump start and stop, so 736 is
what two in-shot samples bracket and 761 is the honest total — CLAUDE.md's
0d entry carries the same figures. An earlier version of this table said
"40 s / 45 s idle"; what is established is the **sample count**, not the
duration.

**Two shots agree and air was not the cause:** 752 counts / 36.5 g (after a
priming failure) and 736 / 37.9 g (clean). That is **~5×** the datasheet once
inlet rather than yield is used (the scale sees maybe 60 % of the water; the
rest is retention and the 3-way dump), not the ~8× an earlier version quoted
by dividing by yield.

**What the counts ARE is unresolved — see CLAUDE.md.** A published reading
that they were pump noise is **withdrawn**: the aborted attempt ran the pump
**19.3 s** with no water, of which **16.7 s was sampled**, and logged 11
counts where coupling predicts **370** over that window — **34×**. The glitch
*rate* collapsed 128/s to 5.15/s on consistent windows, **25×**, which is the
quantity that measures coupling at the pin directly. (Both ratios were briefly
quoted as 39× and 29× by dividing a prediction over the full pump-on duration
by an observation made over the sampled window. CLAUDE.md is normative.)

**The opposite reading is NOT refuted, and this paragraph said it was until
2026-10-12.** The objection quoted here — 22.2 Hz at the datasheet K implies
9.31 ml/s, "above its rated maximum" — is **circular** and CLAUDE.md withdrew
it: the rated range is where K is *specified*, not a hydraulic limit, and if
the flow really is above that range then 2382 is not the applicable K, so
dividing by 2382 to prove it is out of range proves nothing. Worse, the
recirculation account *predicts* roughly that figure.

**NEW EVIDENCE 2026-10-17, AND IT SUPPORTS THE WATER READING RATHER THAN
KILLING IT.** The operator watches the 0–16 bar gauge on every shot: normal
pulls sit at ~9 bar, a coarse grind barely raises it, an ultra-fine grind goes
*"higher than 9 bar, but not way higher"*.

*(A draft read that as proof the OPV sits above 9 bar and therefore never
opens on a normal shot — **withdrawn the same day**. It rested on "a relief
valve cannot be exceeded at its own setpoint", which is false: spring valves
exceed their cracking pressure under flow, by 10–25 % at full flow. A second
draft then argued a clamp at ~9 explains the repeatability — **also
withdrawn, 2026-10-19**: the operator dials the grind toward a target, so
"~9 bar every time" needs no regulator. **The OPV was measured at 11 bar
full-flow on 2026-10-19**, which puts cracking somewhere near 8–10 and leaves
a normal shot inside that band — relief on a normal pull is neither
established nor excluded. CLAUDE.md is normative.)*

So the position is unchanged: **one reading refuted, the other unresolved** —
with the gauge now weak evidence for it. CLAUDE.md is normative and carries
the volume accounting and the discriminating experiment.

**A capacitor is a CANDIDATE, not a measured requirement.** Nothing has been
fitted; what was measured is the symptom. J4's 10 nF (73.1 kΩ, 218 Hz) is
arithmetically sound but attenuates 150 Hz by only 1.69 dB — "crushed" was
wrong — and could *raise* the glitch count by turning the falling edge into a
~1.1 ms ramp through the noise band. J5's case is weaker still: those drops
are **not coupling** (one burst at pump start, constant across two runs of
different length) **but their cause is UNIDENTIFIED**. If J5 ever gets one it is **470 pF, never 10 nF** — a
2026-10-09 edit specified 10 nF, which against a 6.9 kΩ source is a 69 µs
time constant facing 36 µs clock highs and would have stopped the bus
decoding while presenting as a wiring fault.

**What the flowmeter did that nothing else could:** the failed attempt ran
the display timer a full 19.3 s with the lever pressed, looking from every
other signal like a brew, and the flow count logged **11 pulses**. No water
moved and only the flow count knew. That is 0d's independent diagnostic,
demonstrated — and the reason to fix the tap rather than drop it.

**That is the refutation, not a curiosity** — an earlier version filed it as
"open" while publishing the conclusion it contradicts.

**The pump ran 19.3 s; 16.7 s of it was sampled. BOTH numbers are real and
each belongs in its own place.** An edit once "corrected" 19.3 to 16.7 under
the interval-vs-final-value convention — that convention is about counting
displayed steps from `001` and does not apply, because this file establishes
the timer **is the machine's own pump-on to pump-off measurement**, starting
at zero when the pump starts. So **19.3 s is the pump-on duration**.

**But 16.7 s is the window the 11 counts were observed over**, because the
first sample came 2.6 s after pump-on. A 2026-10-12 sweep put 19.3 s into the
refutation table and left the observation at 11 — a prediction over one window
divided by an observation over another, giving an overstated 428 / 39×.
**The honest form is 22.17 × 16.7 = 370 against 11, i.e. 34×**, with the
counts in the unsampled 2.6 s unknown and ~360 needed there to overturn it.
The same mismatch inflated the glitch-rate collapse to 29×; on consistent
windows it is 25×. CLAUDE.md is normative on all of these.

**Also found:** GPIO4/5 swapped during the rebuild. Both lines idled high,
`pins` looked perfect, `frames=0`. **Zero frames with correct levels is the
swapped-pair signature** — worth knowing because the levels tell you nothing.

### The review round found three bugs the tests were shaped not to catch

Recorded because the shape matters more than the bugs. All three passed a
green suite, and in each case the test exercised the right code path without
ever reaching the condition it claimed to check:

- **A false reprogram alarm.** A line already high on the first poll had its
  release timed from adoption, so an unwired (floating) line settling ten
  seconds later printed `*** HELD PAST 2 s: STORED TIMER REPROGRAMMED ***` —
  the loudest message in the firmware, asserting the machine had rewritten
  its own stored timer, for a press nobody made. The test covering that exact
  path ran 500 ms against a 2000 ms threshold. Fixed: an unwitnessed press is
  marked `synthetic` and can never raise the alarm (cases 11b/11c).
- **"A noise burst costs at most one spurious count"** — false. The filter
  rate-limits, it does not reject: 50 ms of 10 kHz hash scores **25** counts,
  about 10 ml of phantom water. The test iterated `t < 2000` against a 2000 µs
  threshold, stopping one step short of the behaviour it was asserting about.
  Comment corrected, test extended, and `status` now warns when glitches are a
  large fraction of counts.
- **`selftest` had no shot-state guard**, so it would inject ~500 phantom
  pulses into a live record and busy-wait ~5 s, overflowing the 64-deep scale
  queue. Invariant 2's harm by a route that is not a flash write.

Also fixed in the same round: the flow-tap flag is now latched at pour start
rather than read at shot close (it could stamp a record of placeholder zeros
as "no water moved"); `pins` configures the display pads, which it did not, so
it reported `clk=0 data=0` on a perfectly good J5 tap; `pollFlow()` moved above
the sample drain; `switches on` resets state adopted from a floating line; and
the ingest now version-guards the flag, so a v1 record cannot present it.

### Three structural problems, deferred on purpose

None of these is cleanup for 0.9.0; each is its own piece of work. Written
down because the first one invalidates an argument the schema makes.

1. **`002_ingest.sql` reasons from a safety net that does not exist.** Its
   comment argues a decoder bug is survivable because "a decoder bug can be
   repaired months later by re-parsing the archive". `ingest_record.raw` is
   written at `store.go:148` and **read by nothing** — no re-parse path, no
   tool, no endpoint. `firmware/test/host/replay` is not it (it reads a
   `t_ms,weight_mg` CSV through the detector, not raw blobs). Either build the
   re-parse path or stop reasoning from it.
2. **The pulse windowing is irreversible.** Only the delta is stored; the
   free-running base is never written anywhere. When 0c supplies the pump-start
   edge, every shot recorded by 0.9.0 is permanently missing its pre-infusion
   pulses — not recoverable from the database, and not from the raw record,
   because the firmware zeroed them before writing. Carrying the absolute base
   in the header (4 bytes, and a format bump) would make them re-windowable.
   Arguably invariant 3's territory: the free-running count is the sensor
   value, the windowed delta is derived from it plus a detection decision that
   is itself still under revision.
3. **Nothing can say a shot's count is bad.** `flow_dropped` and the glitch
   rejects are free-running serial counters. By the firmware's own reasoning a
   ring overflow means the line is carrying something that is not flow — i.e.
   *this shot's count is invalid* — and the record has no bit to say so.
   `SPOOL_FLAG_TRUNCATED` is the precedent for exactly that.

**And one ordering rule with nothing enforcing it:** deploy the server before
flashing 0.9.0. An older binary decodes a 0.9.0 record perfectly well — magic,
version, header_len and CRC are all unchanged — and then silently binds NULL
for every pulse, with its own suite green.

### THE BENCH RIG AS IT STANDS — 2026-10-18

The 2026-10-08 breadboard was disassembled; this is the rebuild. **Record it
because the rig is now semi-permanent**: the harnesses were insulated on
2026-10-18 and terminate in Dupont females, so this is no longer loose wire
that gets rebuilt from scratch each session.

**The three T-pieces all exist and carry their series resistors.** Verified by
meter on 2026-10-18, female-to-male-end on each branch, machine unplugged —
not assumed from the build:

| Tap | Series, **in the T-piece branch** | Shunt, on the breadboard |
|---|---|---|
| J2 ×4 (1Cup, 2Cup, Steam, Water) | **150 kΩ** | 220 kΩ |
| J4 `#` | **100 kΩ** | 220 kΩ |
| J5 `ESD4`, `ESD1` | **8.2 kΩ** | 15 kΩ |

That placement is not tidiness. **GPIO15-18 emit a ~60 µs low pulse at
power-up** and three switch pins are in that range; the series resistor at the
machine end is what keeps the tap passive through it. The same sweep also
re-derived which Dupont female is which conductor, so the labels come from a
measurement rather than from memory — which is what went wrong on the
2026-10-09 rebuild.

**J4's series read 100 kΩ, not 150 kΩ.** Worth stating as a positive result:
the bag holds both, they look identical, and 150 K / 220 K at J4 looks fine
nominally at 2.71 V and fails on tolerance stacking at −45 mV. The spec moved
from 120 K to 100 K on 2026-10-08, the same day the taps went live, so which
value went in was genuinely open until it was metered.

#### Breadboard, as laid out 2026-10-18

Seven circuits. Each is **two columns** — a junction column and a ground
column — with the shunt bridging them. The junction column carries the
T-piece female *and* the GPIO wire; the ground column goes to the rail.

| Shunt | Junction → GPIO | Fed from |
|---|---|---|
| 220 kΩ | **7** | J2 1Cup |
| 220 kΩ | **15** | J2 2Cup |
| 220 kΩ | **16** | J2 Steam |
| 220 kΩ | **17** | J2 Water |
| 220 kΩ | **6** | J4 `#` — **circuit present, T-piece NOT plugged in** |
| 15 kΩ | **4** | J5 `ESD4` — **clock** |
| 15 kΩ | **5** | J5 `ESD1` — **data** |

**The column numbers are arbitrary and deliberately not recorded here.** What
is load-bearing is the shunt *value* and the GPIO; a first draft of this
section prescribed specific columns and it only caused confusion, because
"row A" meant tap 1 in one person's notes and breadboard row A in the other's.
If the two halves reuse column numbers, note that **"column 35" is ambiguous**
and needs its half.

**Ground rail carries**: every ground column, **J5 `VSS`**, ESP `GND`,
analyser `GND`. J2 has **no ground pin** of its own — its five positions are
+5 V common and four direction lines — so `VSS` is what its shunts return to.
Without J5 connected, the J2 dividers have no reference.

**Deliberately unconnected:**

- **J5 `Disp-P4`** — branched through its 8.2 kΩ, far end open. Never a shunt,
  never anything that can source or sink.
- **J2 position 1** (+5 V common) — **not connected on this rig.** That is
  the only claim here that is an observation.

  **RESOLVED 2026-10-27, in CLAUDE.md, in favour of PHASE 0.** Invariant 1
  now reads *"**From PHASE 0 onward** J2 position 1 (+5 V common) rides in
  the loom"*, which agrees with the 0c section (*"Position 1 therefore
  rides in the **Phase 0** harness"*) and with the decision both derive
  from: `docs/phase-0-plan.md` records *"SETTLED 2026-10-12: position 1
  rides in the Phase 0 harness"*. **The exposure is live now, not at Phase
  1** — that conductor is where a bridge to a direction line starts a cycle
  and reprograms a stored timer within 2 s, and no jumper on the board is
  in that path.

  **What this entry said until then, and why it was right to say it:**
  invariant 1 read *"from Phase 1 provisioning onward"*, the 0c section
  said Phase 0, both sat in the normative file, and this entry flagged the
  disagreement rather than picking a side — *"do not resolve it from a
  bench-rig note"*. That was the correct call: the resolution came from the
  plan's settled decision, not from this rig. Kept because the next person
  to find two normative sentences disagreeing should do the same thing.

  None of it changes the bench fact at the top of this bullet: position 1
  is **not connected on this rig**, whatever phase the harness is in.
- **J4's T-piece** — the breadboard circuit sits idle. With nothing on the
  junction, GPIO6 is pulled low through 220 kΩ: a defined level, harmless.
  **`+` must never be connected to anything**, which is why the whole T-piece
  stays out rather than two of its three conductors.

**Analyser: CH0 on the 1Cup JUNCTION, not the raw line.** The junction sits at
5 × 220/370 = **2.97 V**, inside the analyser's 3.3 V world, so its input
clamp never conducts. That is structurally different from the 2026-10-03 J4
incident, where the leading hypothesis is ~12 mA through a clamp on a raw 5 V
net. Ground to the same rail.

**BUT THE LOADING IS UNRECORDED AND THE MARGIN IS THIN — check it, do not
assume it.** A draft of this section argued only that the analyser's clamp is
safe, which is about the *analyser*. The question it skipped is whether the
**GPIO still reads a press** with the analyser attached:

- It is **not** a 1 MΩ scope input. This repo records the instrument as a
  **74HC245PW behind 100 Ω**, i.e. a CMOS input — DC load is leakage, not a
  resistive divider, so the 244 mV drop a 1 MΩ assumption predicts does not
  apply.
- **It is still not nothing.** HC leakage runs to ~1 µA over temperature, and
  the junction's source impedance is 150 K ∥ 220 K = **89 kΩ** → up to
  **89 mV**. 0c's documented worst-case corner is **+203 mV**. So worst case
  eats nearly half of it.

**MEASURED 2026-10-19, and this paragraph said "nothing here has been
measured" after the reading was taken.** Probe on and probe off both read
**2.89 V** at the J2 junction — a difference below meter resolution, so under
~10 mV against the 89 mV worst case. **The worst case is not retired**: 1 µA
is a max over the part's full temperature range and this is one line at one
temperature on a bench. What it does establish is that **the analyser is fine
to use on a J2 junction**, which is the only place it is ever used.

**DONE 2026-10-19 — see the measured result two paragraphs up.** This
paragraph stood as an open action in the same commit that recorded its answer.
The instruction it gave was: the junction millivolt
reading with a lever held is a prescribed step — **take it twice, with and
without the analyser clipped on.** The difference is the loading, directly,
and it costs one extra reading.

**UNPLUG GPIO6 FROM THE IDLE J4 JUNCTION for this session.** A draft called it
"a defined level, harmless". It is a **FALLING-edge interrupt** pin, and with
the T-piece out its junction is a ~220 kΩ node with a wire on it and nothing
driving it — an antenna into an edge-triggered counter. The "counter frozen
during idle" evidence in the 0d tables was taken with the T-piece **connected**,
i.e. with the sensor's ~9.4 kΩ source holding the node. It says nothing about
an unterminated stub. Leave the resistor in place and pull the GPIO jumper.

**Why the analyser is there at all:** the last `status` reported **`worst poll
gap = 113 ms`**. The firmware can say a press happened; it cannot time one to
the resolution a latch-threshold hunt needs.

**ESP power: USB from the laptop, as every prior measurement in this repo
was taken.** The rule is "do not power the ESP32 from the machine"; a laptop
is not that, and its brick is an isolated SMPS. **Do not change this** — the
grounding was identical for the 345-480 ms presses, the 736 counts and shot
109, and changing it now introduces a variable across the dataset.

### THE 2026-10-19 BENCH SESSION — J2 and J5 live, six findings

J5 and J2 taps fitted, J4 deliberately left out. Everything below was
measured with the rig described above, ESP on laptop USB as always.

#### J5 had TWO faults, and the first one hid the second

**Symptom: `frames=0` for over an hour**, with the machine's display plainly
lit and showing 95 °C.

| Fault | Effect |
|---|---|
| **Shunts were 8.2 kΩ, not 15 kΩ** | junction at **1.94 V** against VIH 2.475 V — **535 mV short** |
| **`ESD4`/`ESD1` swapped** across GPIO4/GPIO5 | the documented swapped-pair signature |

**The order mattered, and getting it wrong cost most of the session.** While
the shunts were wrong, **neither orientation could work**, so swapping the
pair changed nothing — and that null result was read as *"the swap is
excluded"*. It was not excluded; it was **untestable**. Only after the levels
were fixed did the swap test mean anything, and then it resolved immediately:
`frames=177` in 8 s, `mode=temp 95 C`.

**The arithmetic, because it is the diagnosis:**

| Shunt | machine's line | junction | vs VIH |
|---|---|---|---|
| **15 kΩ** (correct) | 5 × 23.2/27.9 = **4.16 V** | **2.69 V** | **+215 mV** |
| 8.2 kΩ (fitted) | 5 × 16.4/21.1 = **3.89 V** | **1.94 V** | **−535 mV** ✗ |

**A draft of this table used VIH = 2.31 V (0.7 × VDD). That is the 74HC
figure, not the ESP32-S3's** — this repo uses **2.475 V** nominal and
**2.60 V** at a 5 %-high rail everywhere else, and quotes it when rejecting
220 K / 220 K at 0c. Corrected above; the diagnosis is unaffected, the
*margins* were flattered by 165 mV.

#### THE CORRECT J5 DIVIDER FAILS THE SAME CORNER THAT REJECTED 150 K AT J4

Two successive drafts wrote *"nobody has run that corner for 8.2 K / 15 K"*.
**It has now been run, and 8.2 K / 15 K does not pass it.** Same method as the
0d table: machine rail −5 %, ESP rail +5 % (VIH **2.60 V**), 1 % on our pair.

**THIS DIVIDER HAS A CEILING AS WELL AS A FLOOR, AND A DRAFT OF THIS SECTION
OPTIMISED ONLY THE FLOOR.** That draft recommended a **23.2 kΩ** shunt. It
puts the pin at **3.89 V** in the push-pull corner — **290 mV over the
ESP32-S3's 3.6 V absolute maximum** — and it reached a commit. The whole J5
tap is specified to be safe *whichever way the line is driven* (`main.cpp:52`,
and this file at the T-piece entry); **the drive type has never been
established**, so both columns bind:

| shunt | **OD corner** (pu 1 %) | **OD corner** (pu 5 %) | **PP worst** | vs flat 3.600 V | ~~vs 3.435 V~~ † | disturbance |
|---|---|---|---|---|---|---|
| **15 kΩ — AS BUILT** | **−70 mV ✗** | **−87 mV ✗** | 3.418 V | +182 mV | **+17 mV** | 16.8 % |
| 16.4 kΩ — the specified remedy | **+35 mV** | **+18 mV** | 3.523 V | +77 mV | **−88 mV** | 16.0 % |
| 17.4 kΩ | +104 mV | +88 mV | 3.591 V | **+9 mV** | −156 mV | 15.5 % |
| 18 kΩ | +144 mV | +127 mV | 3.629 V | **−29 mV** | −194 mV | 15.2 % |
| ~~23.2 kΩ~~ — **a draft's recommendation** | +431 mV | +415 mV | **3.899 V** | **−299 mV ✗** | −464 mV | 13.0 % |

*(OD = open-drain through the board's 4.7 K pull-ups, worst case for reading
HIGH: machine rail −5 %, VIH 2.60 V, shunt −1 %, series and pull-up +1 %.
PP = push-pull, worst case for the pin: machine rail +5 %, shunt +1 %, series
−1 %. **† BOTH ceiling columns are SUPERSEDED, not just the strict one** —
they treat VDD + 0.3 as a hard wall, which the clamp-current analysis below
withdraws, and that applies to the flat 3.600 V column exactly as much as to
the 3.435 V one. **Ignore every ✗ and ✓ in both.** *(A draft struck only the
3.435 V column and left the flat one live — striking the pessimistic
convention and keeping the favourable one, which is the inversion this
section exists to condemn, and it left 15 kΩ apparently passing a ceiling
test that no longer exists.)* **Both pull-up columns are given because the 4.7 K is a
community-schematic value, never metered** — a draft quoted only the
favourable one while calling it the pessimistic variant.)*

**THE CEILING IS A CURRENT LIMIT, NOT A VOLTAGE WALL — AND A DRAFT OF THIS
SECTION BUILT ITS HEADLINE RESULT ON TREATING IT AS A WALL.** That draft
noticed this repo uses two conventions (the J5 text compares against a flat
**3.600 V**; the J4 section uses VDD + 0.3 with the ESP rail moved, **3.435 V**
at −5 %), computed the feasible window under each, and published the result:

| convention | window, treating the ceiling as a hard wall |
|---|---|
| flat 3.600 V | ~~16.2 – 17.5 kΩ~~ — also superseded; under the corrected clamp corner it is **~16.2 – 16.6 kΩ** |
| 3.435 V | ~~EMPTY — no value works~~ |

**THE EMPTY CELL IS AN ARTIFACT AND IS WITHDRAWN.** What damages a pin above
VDD is **clamp-diode current**, and this tap has **8.2 kΩ in series with the
clamp**. This same file already reasons exactly that way, 700 lines down:
*"the same 8.2K also bounds clamp-diode current to ~0.5 mA if the ESP32 is
unpowered while the machine is on"* — a far worse case than this one. The J4
section does it too, accepting VDD + 174 mV as benign because *"a clamp diode
does not conduct meaningfully below ~0.4 V"*.

| shunt | PP corner | over VDD nom | over VDD at −5 % | past ~0.4 V? |
|---|---|---|---|---|
| 15 kΩ (as built) | 3.418 V | +118 mV | +283 mV | no |
| **16.4 kΩ** | 3.523 V | +223 mV | **+388 mV** | **no — by 12 mV** |
| 17.4 kΩ | 3.591 V | +291 mV | +456 mV | **yes** |
| 18 kΩ | 3.629 V | +329 mV | +494 mV | **yes** |
| 23.2 kΩ | 3.899 V ‡ | +599 mV | +764 mV | **yes, by far** |

*(‡ 3.899 V is the **open-circuit** divider output; once the clamp conducts
the pin sits lower. The ~24 µA a draft quoted for this came from
(599 − 400) mV / 8.2 kΩ and the draft then called it an **overstatement**.
It is an **understatement**, and in the direction that matters: the current
flows through the Thévenin source 8.2 K ∥ 23.2 K = **6.03 kΩ**, giving
**~33 µA at nominal VDD and ~60 µA at the −5 % corner** this section declares
governing. Still not destructive — which is why 23.2 kΩ is rejected for
putting the clamp into conduction at all, not for damage.)*

*(A first version of this table subtracted a **nominal** 3.300 V from
push-pull voltages computed at a machine rail of +5 % — mixing corners, which
is the exact defect corrected in the 16.4 K remedy passage further down this
file. Both columns are shown now. The −5 % column is the one that governs.)*

**16.4 kΩ clears it by 12 mV, not by the 177 mV the first version implied.**

**BUT "CLEARS IT" IS THE WRONG FRAME, AND A DRAFT USED THE CURRENT-LIMIT
PRINCIPLE ONLY WHERE IT HELPED.** That draft withdrew the hard-wall ceiling —
rightly — then turned round and declared 17.4 kΩ and 18 kΩ **EXCLUDED** by a
hard 0.4 V wall. The principle does not get to be a soft current limit for
16.4 kΩ and a cliff for 17.4 kΩ. Carried through consistently:

| shunt | over VDD (−5 %) | clamp current | verdict |
|---|---|---|---|
| 16.4 kΩ | +388 mV | **~0** | clean |
| 17.4 kΩ | +456 mV | **~10 µA** | *tolerable*, not excluded |
| 18 kΩ | +494 mV | ~16 µA | tolerable |
| 23.2 kΩ | +764 mV | **33–60 µA** | reject — continuous conduction |

**So the honest ordering is a gradient, not a window.** 16.4 kΩ is the only
value with the clamp genuinely off; 17.4–18 kΩ draw microamps the same
section calls "not destructive" at 23.2 kΩ; 23.2 kΩ is rejected for degree,
not kind.

**16.4 kΩ remains the choice** — it is the one value needing no appeal to how
much clamp current is acceptable, and it is what this repo already specified.
**But "~16.2 – 16.6 kΩ" is a preference band, not a hard window**, and a
draft stated it as though the arithmetic forced it.

**THE SOBER CONCLUSION, after this section reversed itself three times in one
day. Note how little of it is new:**

- **The floor failure is the solid finding.** As built, 15 kΩ sits at
  **−70 to −87 mV**. It must not go onto the perfboard. This has survived
  every round.
<!-- fact:divider-j5-shunt 16.4 K -->
- **Fit 16.4 kΩ** — two 8.2 K in series, 25 in stock. **+18 mV** at the
  pessimistic floor corner, node disturbance **16.0 %** against the built
  16.8 %, loading 171 µA against 179 µA, and the clamp stays off at the
  ceiling corner with little to spare. **This is the value this repo
  specified before any of this analysis started**, which is the strongest
  thing that can be said for it: three rounds of argument ended where the
  original specification already was.
- **Everything above ~16.6 kΩ is ruled out IF the line is push-pull** — by
  the clamp threshold at 17.4 and 18, and by outright conduction at 23.2.
  **That qualifier is load-bearing and a draft dropped it.** The whole table
  is a push-pull corner table; under open-drain the pin never exceeds 2.8 V
  and none of it applies.
- **So the feasible window is ~16.2 – 16.6 kΩ under push-pull**, and
  16.2 kΩ upward under open-drain. **A draft left "16.2 – 17.5 kΩ" standing**
  from before the clamp corner was redone; 17.4 and 17.5 are outside it.

**THE DRIVE TYPE IS STILL UNTESTED, AND NOBODY HAS A WORKING PROCEDURE FOR
IT. Two have been proposed in this file and both are wrong:**

| attempt | why it fails |
|---|---|
| "meter the J5 line: ~4.16 V = open-drain, ~5.0 V = push-pull" | `ESD1`/`ESD4` switch continuously, so a DC meter reads a duty-cycle mean, not a level. A push-pull line reads low and is called open-drain. |
| "J5 unplugged, read pin-to-5 V in Ω mode" | **there is no +5 V on J5** — it is ground plus three signals. And this file records that probing the J5 *board header* gave no continuity at all, which is why the T-piece window is the only place readings are obtainable. It returns a false null indistinguishable from push-pull. |

**Constraints any valid procedure must satisfy**, recorded so the third
attempt is not also wrong: it cannot be a DC voltage on a switching line; it
cannot ohm against a 5 V pin that J5 does not have; and **it must not touch
J4's `+`**, which is the one hard read-only rule in this project and which an
ohmmeter would violate by sourcing current.

**This is not blocking: 16.4 kΩ is correct under either drive type.** Fit it
and build.

**BUT THE MEASUREMENT HAS NOT LOST ITS VALUE, AND A DRAFT RETIRED IT WITH A
CIRCULAR ARGUMENT.** That draft said larger shunts "are ruled out by the
clamp threshold anyway, so the measurement has lost most of its value". **The
clamp threshold only exists if the line is push-pull** — which is the very
thing the measurement tests. Confirming open-drain voids the entire ceiling
table and reopens 18–22 kΩ with triple the floor margin. Using a table to
retire the experiment that would invalidate that table is the plainest
circularity this file has logged. (`main.cpp` carries the correct version and
was not changed to match the draft.)

**And a valid procedure probably does exist — the rejection above is too
strong on both halves:**

- **The 5 V reference need not be on J5.** `J2 position 1 is +5 V` on the same
  IRM-03-5 rail, and it is not the read-only conductor — this file's own
  constraint names **J4's `+`** specifically, which presupposes J2 pos 1 is
  fair game. Unpowered, machine off, both connectors unplugged: ohm J5 pin 2
  or 3 against J2 position 1.
- **The cited continuity failure was a different test.** It was J4's three
  shrouded `B3B-XH-A` pins probed against the J5 pin-1 **pad**, and this file
  records *"an occupied J5 pad"* as one hypothesis for it — which unplugging
  J5 removes.

So: **open, worth doing, and no longer believed impossible.** Not required
before the perfboard.

**WHY THE DRAFT REJECTED IT, AND WHY THAT ARGUMENT WAS WRONG.** It said 16.4 K
buys "+18 mV, thinner than the **25 mV** that got 220 K / 220 K rejected at
0c". **That compares a corner figure against a nominal one.** The 25 mV is
220 K / 220 K's *nominal* margin, which CLAUDE.md rejects precisely because
"supply tolerance alone consumes" it — at the same corner this table uses,
220 K / 220 K sits near **−250 mV**. Like for like, 16.4 K's **+18 mV** corner
is not in the same category at all — positive against deeply negative.
*(Quote +18, the pessimistic pull-up case. A draft wrote +43 here, which is
the favourable one, in the paragraph condemning favourable assumptions.)* **Mismatched kinds of number, and the
mis-comparison is the entire reason the draft escalated past the ceiling.**

**So: as built is wrong either way, and that part is not in doubt.**

- **As built, J5 is a −70 to −87 mV corner** — worse than the −45 mV that got
  150 K / 220 K rejected outright at J4, and for the identical reason: fine
  nominally, fails on tolerance stacking. It is the one value in the table
  that clears the *strict* ceiling, which is the only thing to be said for it.
- **16.4 kΩ is the value to fit when something is fitted** — two 8.2 K in
  series, 25 in stock, loading essentially unchanged at 171 µA against 179 µA,
  and **16.0 % node disturbance against the built 16.8 %**, so invariant 1
  improves slightly rather than worsening.
- **Its margin is +18 mV, and that is thin by this file's own standards.**
  Quote that figure, not +43: the difference is only which way the unmetered
  4.7 K is assumed to err, and assuming in your own favour is how the 23.2 K
  recommendation happened.
- **17.4 kΩ is worse than 16.4 kΩ but not forbidden, and this section has
  said both.** Three drafts in sequence: "16.4 is the only value that clears
  both" (an artifact of a five-row table), then "17.4 buys five times the
  margin" (one-sided, floor only), then "17.4 is EXCLUDED" (a hard 0.4 V wall,
  in the same breath as calling 33-60 µA *"not destructive"* at 23.2 kΩ).
  **The last of those is the one to drop**: you cannot treat the clamp as a
  soft current limit where it rescues 16.4 kΩ and as a cliff where it
  excludes 17.4 kΩ. Carried through consistently 17.4 kΩ draws **~10 µA** —
  tolerable, worse than 16.4 kΩ, not forbidden. **16.4 kΩ is the choice
  because it is the only value needing no appeal to how much clamp current is
  acceptable.**

**Why it decodes anyway at a negative corner**: a corner is not an operating
point. The real rail is near 5.0 V, real 1 % parts are not all aligned, and
the bench is not a boiler housing. 363 clean frames say the nominal case
works; they say nothing about the corner, which is the whole reason J4's table
exists.

**THE 4.16 V IS NOT AN INDEPENDENT CHECK ON THIS, AND A DRAFT USED IT AS ONE.**
It wrote that 4.16 V *"is the figure this repo already records as measured"*,
so the model agreed with a measurement. **It is the same calculation.** It
entered at `7182c56` as *"4.7 K pull-ups, so a 23.2 K divider leaves them at
4.16 V"*, and no meter reading of that node exists — see invariant 1, where
the word "measured" has now been corrected. Checking 5 × 23.2/27.9 against
5 × 23.2/27.9 confirms arithmetic, not hardware.

**AND EVERY NUMBER IN THE TABLE ABOVE SCALES WITH A RESISTOR NOBODY HAS
METERED.** The 4.7 K pull-up is a **community-schematic value**. J2's and J4's
dividers were sized against resistances measured on this machine (10.04 kΩ,
147.4 kΩ, and J4's model then predicted 4.681 V against a metered 4.69 V);
J5's was not. **A draft concluded from J4's success that the J5 table was
"worth acting on without re-metering J5 first". That is the one claim this
section does not earn** — J4 validates the *method*, not this particular
input, and the method is only as good as the resistance fed into it.

**It does not change what to fit.** 16.4 K is bracketed on both sides by
constraints that do not depend on the pull-up at all: the push-pull ceiling
has no pull-up in it, and the open-drain floor moves the *same direction* as
any pull-up error for both candidate values. **Meter the 4.7 K next time J5
is unplugged** — it is one reading and it would retire the hedge.

**Why it presented as intermittent rather than dead**: at 1.94 V the pin sits
just under threshold, so polled `pins` reads flickered between 0 and 1 and
looked alive, while the ISR — which needs a real edge — never fired once.
**A polled read cannot distinguish a signal from a pin loitering near VIH.**

**The 8.2 kΩ is the J5 SERIES value**, fitted in the T-piece branch, and there
are 25 of them in stock. Putting one in the shunt position is an easy
substitution and it fails exactly this way. Series 8.2 kΩ at the T-piece,
shunt 15 kΩ at the board — the two are not interchangeable.

#### The switch poller has a resolution floor, and the counter that looks like it would warn you does not

Six 1Cup presses at 80-90 ms: **four logged, two not.** `worst poll gap` was
**~50 ms**, so a press is ~1.6 poll intervals.

**THE MECHANISM IS THE DEBOUNCE, NOT A PRESS FALLING BETWEEN POLLS, AND A
DRAFT SAID THE LATTER — WHICH IS ARITHMETICALLY IMPOSSIBLE HERE.** An 80 ms
press cannot fit between two polls 42-57 ms apart; it is always seen by at
least one. `SwitchBank` requires **two consecutive agreeing polls**
`DEBOUNCE_MS` (20 ms) apart before it sets `stable`, so a press caught by only
**one** poll is discarded.

**DO NOT DERIVE A RATE FROM THIS THE WAY A DRAFT DID.** It said "at ~1.6
intervals per press, roughly a third of presses get only one poll — which is
the observed 2-in-6 directly". That treats `sw_max_gap`, a **running
maximum**, as if it were the mean. The loop is nominally `delay(5)`, at which
an 80 ms press spans ~16 polls and confirms easily. **The misses are
excursions, not the steady state** — two presses happened to land in stalls.
The agreement with 2-in-6 was numerology.

**Two consequences the draft's version hid:**

- **The logged durations are biased LONG.** A confirmed press needed two polls
  HIGH, so its true width is at least one poll gap, and the short tail is
  systematically missing. The **[23, 137] ms** band treats the quantisation as
  symmetric; it is not.
- **It is the same mechanism as the guard-fails-to-arm exposure** recorded in
  CLAUDE.md under lever B — an unconfirmed press emits no `Event`, so nothing
  downstream of `Event` ever runs.

**THREE DIFFERENT POLL-GAP FIGURES APPEAR FOR THIS ONE SESSION. They are one
number at three times, and the load-bearing bound depends on which:**

| figure | when |
|---|---|
| **113 ms** | the bench-rig entry above — *"the last `status` reported worst poll gap = 113 ms"* |
| **57 ms** | flow tap ON — the GPIO6 stub's 1.45 M glitches were burning interrupt time |
| **42 ms** | after `flow off` |
| **50 ms** | the press run itself |

**FOUR, NOT THREE — and a draft listed only the lower three.** The 113 ms sits
in the bench-rig entry above and was left out, which matters because
CLAUDE.md's **[23, 137] ms** press-width bound and the plan's **"below
~140 ms"** are computed from 57 ms being the worst. At 113 the band would be
roughly [−33, 193] ms and bound nothing.

**BUT THE 113 ms ALMOST CERTAINLY DOES NOT APPLY TO THE PRESS RUN, AND A FIRST
DRAFT OF THIS PARAGRAPH RETRACTED THE BOUND ON THE ASSUMPTION THAT IT DID.**
`switchesSetEnabled()` in `main.cpp` zeroes `sw_max_gap` on every `switches
on`, so the counter is per-arming, not per-session. *(A draft cited
`main.cpp:321`, a comment line. A second draft "corrected" it to 328 — also a
comment line. A third said 336 — also a comment line. **Three attempts to
cite one assignment by line, all three landing on comments, inside the commits
that added the cite-by-section rule.** It is in `switchesSetEnabled()`; grep
`sw_max_gap`.)* The 113 ms belongs to whichever arming preceded
it; the press run was read at ~50 ms. **The bound survives.**

**What does NOT survive is being able to show that from the record.** Nothing
ties the ~50 ms reading to those six presses except the order they appear in
the notes — it is a running maximum read at some moment, not a figure latched
around the experiment. So:

- **[23, 137] ms stands**, and the files carrying it need no correction.
- **It stands on an argument about firmware behaviour, not on a measurement
  procedure**, which is weaker than it reads.
- **Fix it procedurally, not textually:** issue `switches on` immediately
  before the next press run and read `status` immediately after, so the gap
  figure provably belongs to that run. That is one line in the bench sequence.

*(Recorded in full because the first draft overcorrected — it widened the band
to [−33, 193], declared the RC question dead on that basis, and did not check
whether the counter resets. One grep for `sw_max_gap` would have shown it. This is the
same read-the-call-site failure this repo logs more than any other, committed
here in the direction of excessive caution rather than excessive confidence —
which is still wrong, because it would have thrown away a real bound.)*

**And the 10 ms in the Settled table is a different session (2026-10-08), not
a contradiction — but the 4-5× regression is unexplained.** The flow tap
accounts for 57 → 42; nothing accounts for 42 → 10. Candidates are the J5 tap
being live (0.8.0 was display-off) and the BLE/spool work at pour start that
already has an unexplained 6086-edge burst against it. **Print it again on the
next capture before trusting either number.**

**`events dropped = 0` throughout.** `machine_io.cpp:43` increments `lost`
only when the event **queue overflows** — events the poller *saw* but could
not store. A press the debounce never confirms is never seen, so it
cannot be counted. **Zero does not mean nothing was missed.**

That is the same shape as `dropped_edges` on the display bus, which counts
ring-buffer overflow rather than ISR firings, and which was misread the same
day. **When a counter's value is load-bearing, read the line that increments
it.**

Consequence: **the firmware's switch log is not an instrument below ~100 ms.**
It is sound above that — the 42-50 ms gap is well clear of the 195-295 ms
presses in the same session.

#### GPIO6 on an unterminated stub is an antenna — confirmed, with numbers

J4's T-piece was out but its breadboard circuit and GPIO6 jumper were left in.
With nothing driving that junction:

    flow: pulses=78488 glitches=1450274 dropped=834 last=8 ms ago

**1.45 million rejected edges and 78 thousand accepted ones, counting nothing
at all.** A code review predicted exactly this four days earlier, against a
draft that had called the floating junction *"a defined level, harmless"*. The
frozen-counter evidence in the 0d tables does not apply, because it was taken
with the T-piece **connected** and the sensor's 9.4 kΩ source holding the node.

`flow off` stops it. **Pull the GPIO6 jumper or disable the tap whenever J4 is
unplugged** — it also burns interrupt time, and `worst poll gap` improved from
57 ms to 42 ms once it was off.

**AND IT IS A DATA-INTEGRITY HAZARD, not just noise.** A shot pulled with
`flow on` while J4 is unplugged spools those antenna counts with
**`SPOOL_FLAG_FLOW_TAP` set**, because the flag records *"the tap was
enabled"*, not *"the tap was connected"*. The server then ingests pure noise
as a measurement — and this repo's own rule is that a stored value must be
distinguishable from "nothing was counting", which is exactly what the flag
exists to guarantee and exactly what it fails to do here.

Nothing in the format carries a quality field, so **the only mitigation today
is procedural**: `flow off` whenever J4 is out. That is the weak kind of
guarantee this file flags elsewhere — it depends on the operator remembering.
Worth a firmware fix: the tap could be treated as absent when the counter
runs with no J4-shaped signal, or the flag could require a plausibility check
rather than just the NVS boolean.

#### What this session closed

| | |
|---|---|
| Display tap after the 0.9.4 flash | `frames=177`, `mode=temp 95 C` |
| Lever A's log line | ten quoted `# switch 1cup` events |
| `BREW_GUARD_MS` | **probably too short for a lever-B draw** — the "couple of minutes" it rests on is operator recollection, not a measurement. And it can fail to ARM entirely; see CLAUDE.md |
| Minimum latching press width | **below ~140 ms**, observed to latch — see the caveat below |
| 2Cup stored duration | **45 s** (was UNKNOWN) |
| Lever B latching | yes — and `BREW_GUARD_MS` is too short |
| J2 divider margin | **2.89 V held, +415 mV over VIH 2.475 V** |
| Analyser loading on a J2 junction | **unmeasurable**, under ~10 mV |

**The press-width measurement is the one that unblocks work**: the one-shot RC
could not be chosen until it existed. **It still cannot be chosen**, and a
draft of this very entry said otherwise.

**The 80 ms is a POLLER reading, not a width.** `machine_io.cpp:102` takes
`cand_since - prev_since`, the difference of two poll timestamps, and the gap
was 42-57 ms — so the true width is anywhere in **[23, 137] ms**. A draft
claimed "a ~120 ms pulse has 1.5× margin over a demonstrated-latching 80 ms";
**137 ms exceeds 120 ms, so the margin may be negative**, and this same file
says a page earlier that the switch log *"is not an instrument below
~100 ms"*. **Redo it with the analyser.** CLAUDE.md is normative.

### The bench sequence, in order

Everything here runs on the breadboard with the machine closed.

1. `pio run -t upload`, then `pins`. With nothing connected the five machine
   inputs float and read arbitrarily — that is the expected result, and it
   is the baseline that makes step 2 mean something.
2. **Flow counting, the one thing host tests cannot prove.** Jumper
   **GPIO8 → GPIO6**, nothing else connected, then `flow on` and `selftest`.
   It drives 500 pulses at 100 Hz. **That is 6.3× the sensor's rated
   ceiling**, from the datasheet: 0.40 L/min max flow × 2382 pulses/L =
   **15.9 Hz**. That is the rate at the sensor's rated MAXIMUM FLOW, which is
   the range over which K is specified and **not a hydraulic limit** — this
   machine has already run it past. Two
   intermediate versions of this line said "four times" and then "2.3–2.5×";
   the second was computed against the disputed in-circuit 5915 and is
   withdrawn. It prints counted against expected. **Anything other than an
   exact match means the shot pulse count cannot be trusted**, and the
   glitch/drop counters say which half is at fault.
   *`selftest` is the only command that ever makes a pin an output. Unplug
   J4 first; invariant 1 is exactly about this.*
3. `switches on`, then jumper each of GPIO7/15/16/17 to 3V3 and back while
   watching the serial log. Each touch should print one `# switch …` line
   with a plausible duration, never several. Hold one past 2 s and confirm
   the `REPROGRAM` warning fires.

   **WHAT WAS ACTUALLY DONE ON 2026-10-08 WAS LEVER PRESSES, NOT JUMPER
   TOUCHES** — operator, confirmed 2026-10-12. This step is the *plan*; the
   levers are the *event*. A 2026-10-12 review read this text as evidence
   that the recorded 345–480 ms taps were header touches and concluded the
   figure said nothing about the machine, which would have undermined the
   one-shot pulse-width design. **Do not cite this procedure as a record of
   what happened.** Levers are the better test anyway — they exercise the J2
   conductor and the divider, which a header jumper skips.
4. Leave it running an hour with the scales connected and pull the usual
   shots. Then `status`: **worst poll gap** is the number that decides
   whether polling survives into the perfboard, and `dropped=0` on the
   display bus confirms the new ISR did not cost the old one anything.

### What still needs the machine — ALL BUT ONE DONE 2026-10-08

**This list was stale within one session and is kept as a record of that.**
Four of its five items were completed on 2026-10-08 and the first write-up
of that session left this section untouched, so one file asserted both that
lever B was plug-inspection-only and that it was functional, 200 lines apart.
That is the repo's documented recurring defect reproduced inside a single
file, by the commit that was documenting the fix for it.

| was | now |
|---|---|
| Meter lever B (positions 2/5 plug inspection) | **DONE** — functional, see the bench section above |
| Fit J2 and J4, then `pins` | **DONE** — all seven inputs read correctly |
| J4 first contact, watch for `E01` | **DONE** — energised, no fault |
| Calibrate against weighed water | **WITHDRAWN** — in-circuit counts are ~5× the datasheet and what they measure is unresolved |

**Still open, and it needs the flowmeter counting during a lever-B draw:**
whether a **hot-water draw** pulses the meter at all. CLAUDE.md records this
as a prediction. **The plumbing IS traced (2026-10-09)** — tank → T-piece →
flowmeter → pump, with the other T branch going to `I.2821`, an
over-pressure valve whose relief leg returns upstream of the meter; and the
datasheet confirms the part is designed to sit "between the water container
and the vibratory pump (on the suction side)". What remains open is only
whether a lever-B draw routes through it. 0d scopes its counting to
shot boundaries either way, so nothing depends on the answer — but it decides
whether a tap draw shows up as inlet flow.

**Both of these are now done** — the LED on 2026-10-15, the display tap on
2026-10-19:

- ~~**`status` with the machine powered.**~~ **DONE 2026-10-19:
  `frames=177`, `mode=temp 95 C`.** It took two faults to get there — the J5
  shunts were 8.2 kΩ instead of 15 kΩ, *and* `ESD4`/`ESD1` were swapped, with
  the first masking the second. See the 2026-10-19 session entry above.
- ~~**Does the onboard RGB LED light?**~~ **DONE 2026-10-15: it is GPIO48, so
  GPIO38 is not the LED** *(and was free until 2026-10-29, when CLAUDE.md's
  drive table allocated it as the arm gate — the proposed gate *enable*, the gate
  '221's `R̅`, since 2026-10-30)*. Board on USB, no machine connections, **yield Bookoo
  bound and awake** — that last clause is load-bearing and a draft omitted it,
  which would have had anyone reproducing this see a dark LED in both rows.
  Two points, each anchored to a `status` read at the same moment:
  `yield: state=3` with `detector=ARMED` → **dim green**; the scale then
  switched off, `yield: state=4` (LOST) → **off**. Those are precisely what
  `updateLed()` writes for the two states.

  **The first point alone was published as sufficient and was not.** A green
  power indicator beside a dark RGB LED reproduces it exactly; only the dark
  reading rules that out. Worth keeping, because the argument that it was
  sufficient — "the *colour* is specific to ARMED, so it must be the
  firmware" — is genuinely persuasive and still wrong.

  **And the test as this repo had written it was confounded.** "If it has
  always been dark, it is GPIO38" ignores that `updateLed()` writes `0,0,0`
  whenever the **yield** scale is disconnected, so a bench board with no
  scale awake is dark on either revision. A dark LED means nothing on its
  own — it meant something here only because `status` was read beside it.

**Three measurements that now need the rig rebuilt.** The breadboard was
disassembled on 2026-10-08 before these were taken, so they are no longer
two-minute jobs — fold them into **perfboard bring-up**, which re-creates the
same circuit anyway:

- ~~the **actual millivolts** at each divider junction with a lever held~~
  **DONE 2026-10-19 on 1Cup: 2.89 V held, 0 V at rest, +415 mV over the
  2.475 V VIH this repo uses.**
  Also closed the analyser-loading question — identical reading with the
  probe on and off, so under ~10 mV, against a review estimate of up to
  89 mV. The other three lines are still unmeasured, but they are the same
  divider off the same rail, so the remaining value is low.
- the **dropped-edge experiment** (above), which needs `display on` to zero
  the counters and one flush with the scale disconnected
- the **logic analyser on `#`** — and **this entry is the wrong instrument,
  which the same file says 400 lines above.** See *"Settling the K-factor
  without the analyser"*: the analyser tap on `#` produced the `E01` fault on
  2026-10-03, its corrected 8.2 K conditioning has never been re-tested, and
  the ESP tap at 100 K + 220 K now has been. *(A draft dated 2026-10-17
  promoted this to "the single most valuable measurement in the project" on
  the strength of a deduction about the OPV that was withdrawn the same day.
  Withdrawn with it. **The coarse-shot test that replaced it is also
  superseded** — 2026-10-19, once the OPV turned out to be adjustable and the
  2.5×/5× gap turned out to depend on an unmeasured yield split. The current
  first experiment is **weighing that split** (`inlet = yield + retention +
  3-way dump`), because every "~5×" in these documents inherits it.
  CLAUDE.md is normative and carries the arithmetic.)*

**The GPIO pin map is NOT one of them — it was closed on 2026-10-08 and
re-confirmed 2026-10-12.** CLAUDE.md carried "they have not been checked
against the clone DevKitC-1's silkscreen; do that before the perfboard is
drilled" for four days, 200 lines from the bench table that closed it.

The functional evidence and the visual check **combine** — neither closes it
alone. Functional: all four switches logged their own names, the bus decoded
frames (a swapped pair gives `frames=0`), GPIO6 counted 736. Each wire was
landed on a pin the operator picked by reading its label, so the chain fixes
label to die, not merely die to firmware. Visual: the operator confirmed the
silkscreen order on 2026-10-12, ruling out the one alternative the functional
chain cannot exclude — wiring by position rather than by label.

**Two caveats that an earlier draft of this paragraph left out.** `selftest`
through GPIO8 is **not independent** — it drives one die pin into another
through a jumper, so a 6/8 label swap passes it too; it is non-circular only
given GPIO6's 736 counts. And the functional chain is a *longer* evidential
chain than a silkscreen read, not a higher-graded one; calling it "a higher
grade" inverted this repo's own standard. See CLAUDE.md's GPIO budget table.

**GND is on the signal column — board reading, 2026-10-12.** This section
previously said Espressif's layout put it on the opposite header and called
that the layout's one awkward fact. It is on both. The signal column runs
**3V3 at one end, GND at the other, all seven taps between**, so no ground
return crosses the board — the one claim here that has survived every
revision.

**ROW-TO-ROW SPACING: 10 pitches, 1.0 in, 25.4 mm — MEASURED 2026-10-12.**
Seated in an 830-point breadboard, one header in column **A**, the other in
**I**; J stays free. That closes the last mechanical unknown, after the item
was briefly lost to a paragraph rewrite and restored.

**The header is 22 pins — rows 42 to 63, 3V3 at 42 — which is a third
independent confirmation of the 2.54 mm pitch**: 22 pins occupying 22 rows,
one per row, which no other pitch produces. *(A first reading gave 42–64 and
23 pins; corrected by the operator within minutes, after the 23-pin version
had reached a commit message.)*

**Two things the same seating did correct:**

- **Four GNDs, and the census is CLOSED** — both columns enumerated end to
  end on 2026-10-12 rather than counted by eye a fourth time. One on the
  signal column (position 22, next to 5Vin); three opposite (positions 1, 21,
  22). The count had moved twice from successive closer looks, which is why a
  review flagged it as a visual scan standing as the premise of a layout
  claim. **The claim survives**: the signal column is 3V3 at position 1 and
  GND at position 22, so no ground return crosses the board.
  **`docs/board-and-perfboard.md` carries the full board-read pinout** for
  both columns (it was in CLAUDE.md until 2026-10-19). It **corroborates**
  Espressif's published layout for this clone rather than replacing it —
  the two agree at all 44 positions, and the "replaces it" wording was
  retracted in that file.
- **`docs/shopping-list.md`'s "leaves no rows beside it" is refuted** — J is
  free. That line was used twice to argue the spacing was already partly
  constrained.

**And the pitch argument's stated evidence was wrong.** A pushed commit said
the devkit "has been seated in a 2.54 mm breadboard through every bench
session". Through the bench work the ESP was **off the breadboard, wired in**,
with only the divider junctions on it — which this repo's own shopping list
had recommended. The pitch conclusion stands on the 2026-10-12 seating alone.
An assumed history is not an observation.

## The J5 tap went live 2026-10-07, and two things came out of it

**A false `SETPOINT-TOUCHED`, and the cause is now MEASURED.** On first
enabling the reader the display showed a steady ` 95` and the latch was
set. Nobody had touched the setpoint.

**Reproduced on the bench**, which is what makes this a finding rather
than a story:

| Action, reader attached and running | Latch |
|---|---|
| unplug/replug **GPIO4** (the clock) | **nothing** |
| unplug/replug **GPIO5** (the data) | **SETPOINT-TOUCHED** |

**The mechanism, stated so it generalises.** A blank needs the **data**
line to read all-zero across all three digit fields. Only a floating data
pin produces that. With the **clock** pin floating, frames still assemble
— an unplugged ESP32 input on a breadboard beside a random-phase triac
picks up plenty of noise edges, so "the ISR stops" would be wrong — but
they sample *real* `ESD1` levels, so they land on non-decodable lengths or
`-88`-class readings and never on blanks.

So the rule is not "do not unplug GPIO5". It is: **any disturbance that
zeroes the data line while the clock keeps running will latch**, and two
such frames inside `BLINK_WINDOW_US` are enough. On 2026-10-07 both
jumpers were swapped with the reader **on**, which necessarily passed
through exactly that state.

Worth pairing with this: the J5 tap reads **~2.7 V** at the GPIO in the
open-drain case, about **210 mV over VIH**. On breadboard jumpers a
marginal data line is a standing risk, not only a rewiring one.

**Two wrong diagnoses were published before that, and both are instructive.**
First: an attach-time fragment decoding as blank. Refuted twice over — a
clean attach yields exactly ONE fragment and the latch needs two blanks
(now a test assertion), and 60000 simulated attach points against three
real captures produced **zero** blank-decoding fragments on a non-blinking
display. There is a structural reason: digit offsets 106/115/124 sit
exactly 67 bits — the button-frame length — above 39/48/57, so **the digit
frame is the last 66 bits of the merged 133**, and a 66-bit tail fragment
IS the aligned digit frame. The one length that reaches `readDigits()` is
self-correcting. Fragments decode as **`-88`-class garbage** instead,
which lands in `Mode::TEXT` and is ignored by `MachineContextTracker`.

**Exposure is bounded**: `clearSetpointTouched()` runs at pour start, so a
disturbance only matters *during* a pour. Between shots it is irrelevant.
**While on a breadboard, do not touch the wiring during a shot** — nothing
breaks, but the record would be silently marked as having had its setpoint
edited, which is the flag 0f uses to discard runs.

**Still open, and only one of the obvious hardenings actually works.**
Two blanks inside 600 ms is thin evidence. But **requiring three
consecutive blanks buys nothing**: a real setpoint blink produces runs of
~5 blank frames 41 ms apart (`display.h`), so it fires on both. The
discriminator the captures support is **alternation** — a real blink
*returns to a value* (204 ms blanks, 414 ms blank-to-blank, so a reading
sits between every pair), whereas a zeroed data line decodes blank
continuously and never shows a value at all.

So the rule to test is **"a blank run must be bounded by a non-blank
reading"**, with `2026-10-03-display-bus-buttons.sr` and bench garbage as
the two fixtures. Recorded explicitly because "require three" is the
cheaper-looking option and is the one someone will reach for.

**`dropped` counts edges, and serial polling is SUFFICIENT to cause them —
it is not established as the cause of any other drop event.** During a
polling loop `dropped` went 0 → 10266, then froze there across ~1500
further frames once polling stopped. That shows polling is **sufficient**
to cause drops; it does not show it is **necessary**, and because the brew
overlapped the polling, **a brew has not been shown clean with no serial
attached.**

**Do not carry this over to the pump-start burst.** That is a different
event — one burst of ~6086 edges at pump start, +164 across 30 s of
pumping — and `docs/taps-and-dividers.md` is normative on it and retracted
"serial polling" as its cause: a 3 s poll would drop *periodically*, not in
one burst, and idle rows polled at the same cadence had the counter frozen.
Its cause is **unidentified**; the candidates are the spool/BLE work at
pour start and the serial traffic, unseparated, and `worst poll gap` is the
discriminator. *(The headline here read "and serial polling causes them"
until 2026-10-27, which the paragraph under it then walked back. The flat
claim was the one a reader quoted.)*

The ring tolerates a longer stall than the code comment implies.
`DBUS_RING = 512` gives 511 usable slots, and the comment's "~50 ms" is the
*burst* figure (511/9900 Hz). The bus is not continuous — 133 edges every
41 ms is **3244 edges/s average**, so the main loop must stall **>158 ms**
to drop anything. 10266 edges is ~77 frames, ~3.2 s of bus traffic.

The one place a >158 ms stall is expected by design is the spool flash
write at shot end — and invariant 2 puts that *after* the pump stops, when
the display is back in temperature mode, so edges lost there cost nothing.

## What works end-to-end (all verified with real shots)

- Shot detection (0a.5: baseline arming — no tare; pour detected even
  without stillness; noise-robust stop (flat 0.3 g/2 s); cup-lift ends
  the shot — no 7 s wait; bump-tolerant; rejects uploaded flagged), spool to flash, upload with delete-on-confirm, idempotent
  ingest, automatic **dose attribution** from the grinder scale
  (first production shot: 37.4 g / 24.4 s, dose 18.2 g `measured`)
- Live pull screen (ghost trace, editable target box), shots list with
  overlay + detail cards, capture flow (grind/dose/preinfusion/temp sticky,
  taste chips), beans with autocomplete / "same again" / edit, shot delete
  (raw record always survives in `ingest_record`)
- Failure paths proven live: server outage → spool+backoff → self-delivery;
  duplicate uploads deduped; same-ms BLE samples collapsed
- **Forensics:** the server journals every live frame to
  `/var/lib/espressolog/live/YYYY-MM-DD.ndjson` (~1 MB/day). A missed or
  odd shot is replayable: extract `t,w` for the window → `firmware/test/host/replay`.

## Daily workflow (the only rules)

1. Wake the scales (they sleep ~9 min idle; reconnect is automatic).
2. Weigh the dose on the grinder scale, lift it off — that's the weighing
   (needs ~1.1 s on the scale since fw 0.6.5; was 2.2 s and missed a dose).
3. Cup on the drip-tray scale whenever — Lucas's actual workflow: cup
   preheated with water, portafilter in, machine started, cup emptied and
   set down DURING pre-infusion, then tared as the first drips fall. All
   of that is fine (0a.5/0a.6): no stillness needed, the tare is a rebase,
   pre-tare drips stay in the yield (logged yield reads ~1–2 g above the
   scale display). Board LED: blue = recording.
4. Pull the shot; lift the cup whenever it's done (0a.4 ends the shot on
   a ~2 s cup lift — no need to wait; a quick bump is ignored).
5. Tasting notes on the Capture tab (4 taps). Everything else is automatic.

## WHAT PHASE 0 STILL OWES — 2026-10-23

**Every decision the BUILD waits on is closed as of today** *(2026-10-23;
on 2026-10-30 the drive side gained a PROPOSED arm-window gate whose
footprint is reserved and whose design is open — see the Measurements
table)*. **What is left is
measurement and assembly, and no amount of discussion shortens either.**
Recorded as a split because "finalise Phase 0" is ambiguous and the two
halves have very different costs.

*(A draft said "every DECISION is closed", which is too strong. **Three are
still open and none blocks the build**: whether invariant 1 covers plumbing
(UNDECIDED, `docs/hydraulics.md`), the J4 10 nF candidate and the J5 470 pF
candidate — the last two untested remedies for symptoms whose cause is
unidentified, both marked "do not buy yet".)*

### Decisions — CLOSED

| | |
|---|---|
| J5 shunt | **16.4 K** (2 × 8.2 K), the as-built 15 K superseded |
| Board count | **one in service**, second as spare — the deciding argument is that two boards make J2 pos 1 hop between them |
| Box | **printed LAST**, after the board is laid out. The lead-time premise that forced it early was stale |
| One-shot part | **74HC221**, never the '123 |
| Drive scope | **sense four, drive two** (1Cup, 2Cup) |
| Phase 1 buttons | **scoped, not built** — see `docs/phase-0-plan.md` |

### Measurements — OPEN, and these gate things

| | needs | gates |
|---|---|---|
| **Minimum latching press width** | analyser on a J2 junction, machine on | **the one-shot RC.** Current bound is "under ~140 ms" from poller-quantised readings — [23, 137] ms true. Not enough to choose R and C |
| **The '221's pulse at 3.3 V** | the parts, a breadboard | the RC again. `t_W = 0.7·R·C` is characterised at **4.5 V**; this runs at 3.3 V where the coefficient is not guaranteed |
| **The proposed arm-window gate** *(added 2026-10-30)* | the same sweep, after the drive RC | whether the gate is kept at all, and if so its RC, the minimum gap between gate pulses, the margin ordering, and `t_min` against the latching width. `docs/phase-0-plan.md`, "OPEN — the gate, readback and `drivetest`", is the list; the layout reserves its footprint either way |
| **0d-i — weigh the split** | portafilter, scale, drip tray | **every "~5×" in this repo.** The figure divides by an *assumed* inlet |
| **`boiler_temp_start_c`'s meaning** | a capture with the boiler pulled off setpoint | 0e's open half |
| **0f re-run** | 30 shots carrying `machine_timer_s` | the Phase 3 deadband |

**Only the first is doable tonight** — the rig is semi-permanent and the
three T-pieces carry their series resistors.

**Grade the analyser clearance as `docs/taps-and-dividers.md` grades it, not
as "harmless", which a draft of this line wrote.** The reading is 2.89 V
probe-on and probe-off, a difference under meter resolution — but that file
is explicit that **one reading on one line at one temperature does not retire
a datasheet worst case** (1 µA of HC leakage into an 89 kΩ source is up to
89 mV), and what it clears is **bench** use. This measurement is machine-on,
which is not the case that was cleared. Very probably fine; not the same
claim.

### Assembly — unblocked, nothing waiting on it

Order per `docs/shopping-list.md`, solder, bench-test the drive block
**off-machine**, leave every drive-leg jumper **open**.

**PHASE 0 FITS DRIVE HARDWARE; IT DOES NOT ACTUATE.** Invariant 1 permits the
photoMOS, the '221 and the LED drivers on the board **only while an open
jumper in series with their OUTPUT leaves them unable to conduct** — "not
merely unpowered, not merely un-commanded". Populating and bench-testing them
is the 2026-10-08 build decision; closing a jumper is the thing Phase 0 never
does. **Recorded because "we are fitting actuation hardware, so we are
already actuating" is an easy slip and it dissolves the only mechanism
enforcing the goal.**

**THE JUMPER GOES IN FOR PHASE 1; IT IS NOT THERE IN PHASE 0 — and a draft of
this paragraph said the opposite** ("the jumpers come out when Phase 1 is a
deliberate decision"), inverting the safety state in the sentence meant to
protect it. The specified Phase 0 state is **a 2-pin header with NO shunt
fitted** — `docs/phase-0-plan.md` calls that the specified state, and
`docs/shopping-list.md` says *"buy no shunts — open is the specified state"*.
Phase 1 is what **fits** the shunt and closes the path. Open is the resting
state; closing it is the deliberate act, with the RC chosen first.

---

## Next milestones, in order

1. **0f — noise floor — DONE 2026-09-28.** `analysis/0f-noise-floor.ipynb`
   (run it against a fresh DB snapshot; the notebook says how to take one).

   **σ = 0.21 g/s on mean flow (12.2 %) and 3.43 s on shot time (14.2 %)**,
   over n=46: Moonwalker @ 7.9, one grind epoch, detector 0a.6 only, 46 of
   78 recorded shots. *(**That 78 is a transcription slip; the snapshot was
   80.** `analysis/0f-noise-floor.ipynb` prints `all shots 80`, and
   `009_unwrite_unconfirmed_recipe.sql`, `store.go` and `web/src/Capture.jsx`
   all say 80 for the same 2026-09-29 snapshot — as does every other figure
   in this paragraph. Left as written rather than silently corrected, per
   this repo's retraction convention. **And 80 is the SNAPSHOT, not the
   archive**: the *"The deploy and the flash — 2026-10-13"* section of this
   file counts **137** shots in production. Flagged 2026-10-27. (Cited as
   "line 75" in a draft — a line number into a markdown document, which
   this repo names as its own worked example of a citation that rots.)* Robust estimates agree (MAD × 1.4826), flow shows no
   drift across five weeks, and bean age is not a confound
   (corr = +0.13 over 16–39 days off roast).

   Two filters carry most of the weight and should not be dropped when this
   is re-run: pooling detector versions inflates σ for no physical reason
   (0a.4 recorded a cup lift as the stop), and shots 25/26 were one pull the
   detector split in two — now flagged `excluded` in the database.

   **Read for Phase 3:** a controller resolving less than ~12 % is chasing
   noise. But this bundles grind, distribution, tamp and puck with the
   machine, so the achievable improvement is a fraction of it, not all of
   it. And `total_s` is still first-drip-to-plateau rather than
   pump-to-pump — 0b's display timer now offers the real boundary, so
   **re-run the notebook once that timer is logged**: the flow figure should
   stand, the time figure may shrink.

   **That precondition is now MET (2026-10-07)** — `machine_timer_s` is
   landing in the database. Two caveats before anyone reaches for the
   notebook: the re-run needs **30 shots carrying the timer**, not one; and
   shot 109, the first of them, is hydraulically anomalous (4.31 g/s
   against the 0f mean of ~1.72 g/s — roughly 12 σ) and may not belong in
   the pool at all. `shot.excluded` exists for exactly this.

2. **0b — display bus — DONE 2026-09-28.** The bus is decoded and the
   display is readable from the wire. Protocol and probe point are in
   CLAUDE.md; **the bit offsets and the full decode are in
   `docs/machine-display-bus.md`** (they were in CLAUDE.md until
   2026-10-19); the decoder is `tools/decode-display-bus.py`
   and the raw captures are in `analysis/captures/`. Validation: the
   decoded shot timer ran `001`→`169` over 16.892 s of capture time.
   That is 168 displayed steps, i.e. **16.8 s against 16.892 s — +92 ms**.
   An earlier draft here said "8 ms of agreement" by comparing against the
   final displayed value, 16.9 s, instead of the interval; the same
   mistake is recorded — and corrected — in CLAUDE.md for the 2026-10-03
   captures, where the errors are +106 ms and +30 ms. All three are of the order of the display's 100 ms tick
   plus the ~41 ms frame period, so there is nothing to explain — but the
   agreement is tens of milliseconds, not single digits. The static
   capture rendered ` 95` in all 2267 frames, matching the display.
   Kit that worked: SeenGreat SG-NANO-DLA-A + `sigrok-cli`, four wire
   stubs soldered to the J5 pads, 8.2 K in series per channel and 1.5 K
   in the ground lead (no multimeter needed — see the note below).
   **The stubs were removed on 2026-09-28**, so any further capture needs a
   tap rebuilt first — the in-line T-piece (3S balance lead, see below) is now
   on the critical path for the `Disp-P4` button test, not just a tidiness
   improvement.

   Three things that cost time and should not cost it twice: the pad with
   thermal-relief spokes into the copper pour is ground *regardless* of
   wire colour and regardless of which pad is square; sigrok's `i2c`
   decoder will produce confident garbage (endless writes to address 0x00)
   on what is actually plain synchronous serial; and J5 carries **no +5 V**
   — it is ground plus three signals, so the constant-high channel is an
   idle signal, not a supply (pinout now in CLAUDE.md, from the traced
   schematic in `techdregs/Ascaso_Dream_PID_Electronics`).

   Ascaso's own part number for the display harness is **`I.4312`**
   (mainboard is `I.3957`, matching the board silkscreen) — worth knowing,
   but no longer the first choice for the T-tap. A **3S LiPo balance
   extension lead** is male-to-female XH on cable, wired 1:1, and 4-pin,
   so it drops straight in line at J5; `I.4312` is the fallback if a
   generic connector turns out not to mate. See `docs/shopping-list.md`,
   which is the buying document. (The repo's schematic uses generic KiCad
   symbols and names no connector part, which is why this took measuring.)

   Consequence for the roadmap: idle, the display shows boiler
   temperature — that is **0e** essentially solved, pending firmware to
   decode it live. During a brew it switches to a tenths-of-a-second shot
   timer, which is the machine's own pump-on/pump-off boundary and much
   of what **0c** was for. Reassess 0c's priority before ordering its
   parts.

3. **0e — temperature / display bus into the log — IN PROGRESS.**
   Firmware 0.8.0 onward reads the bus: `firmware/src/display.{h,cpp}` is pure
   logic in the ShotDetector mould (no I/O, no clock of its own), an ISR
   stamps clock edges into a ring and `loop()` is the single consumer, so
   framing is done by the same `feedEdge()` the host tests exercise and no
   flash read can happen in the ISR. `firmware/test/host/display_test.cpp`
   runs 18 scenarios against **real recorded frames** from
   `analysis/captures/`, not synthetic bit patterns.

   **Wiring — BUILT and live since 2026-10-07.** Two GPIOs, one divider per line, because the
   bus is 5 V and the ESP32 is not 5 V tolerant:

   ```
   J5 signal --[ 8.2K ]--+-- GPIO4  (clock)
                         +--[ 15K ]-- GND   <- SUPERSEDED: use 16.4K (2x8.2K)
   J5 signal --[ 8.2K ]--+-- GPIO5  (data)
                         +--[ 15K ]-- GND   <- SUPERSEDED: use 16.4K (2x8.2K)
   J5 ground ------------------- ESP32 GND
   ```

   **GPIO4 is the CLOCK and that is `ESD4`, J5 pin 3.** An earlier version
   of the table below had `ESD1`→GPIO4 and `ESD4`→GPIO5, contradicting the
   diagram above it and `firmware/src/main.cpp`
   (`DISPLAY_CLK_PIN = 4`, `DISPLAY_DATA_PIN = 5`). Wiring from that table
   would swap clock and data, which CLAUDE.md records as yielding **zero
   frames of 120 bits or more** — a board that looks fine and decodes
   nothing. The firmware is the authority; it is already written.

   **All four conductors are branched, but only three are terminated.**

   | J5 pin | Net | At the T-piece | At the box |
   |---|---|---|---|
   | 1 | `VSS` | branch, no resistor | ground |
   | 2 | `ESD1` — **data** | 8.2K | ~~15K~~ **16.4K** shunt -> **GPIO5** |
   | 3 | `ESD4` — **clock** | 8.2K | ~~15K~~ **16.4K** shunt -> **GPIO4** |
   | 4 | `Disp-P4` | 8.2K | **nothing — labelled test point** |

   **The 15K in the diagram above is what is FITTED; 16.4K (2 × 8.2K) is what
   to BUILD** — 15K fails its tolerance corner (2026-10-19). Marked inline
   because this table and that diagram are what the perfboard gets wired
   from. *(A draft put this note between rows 3 and 4, which broke the table
   and left the `Disp-P4` row rendering as literal text — the one row
   carrying the fit-nothing rule.)*

   An earlier draft left `Disp-P4` untapped as the cautious choice. It is
   the opposite: this T-piece is on the critical path for the `Disp-P4`
   button test, so a tap that omits it needs a *second* tap built into the
   only 3S lead there is. **Sensing it through a divider is NOT "explicitly
   safe per CLAUDE.md"**, which this said until 2026-10-12 — CLAUDE.md now
   says the opposite and `docs/shopping-list.md` agrees: fit no load at all.
   Branch it, leave the far end open.

   **Why it is branched but not terminated.** `ESD1`/`ESD4` idle on known
   4.7K pull-ups, so a 23.2K divider leaves them at 4.16 V and still
   reading high. **What pulls `Disp-P4` high is not recorded**, and the
   schematic path to the MCU runs through R32 = 100K. If that pull-up is
   weak, a 23.2K divider drags the line under 1 V — and **what the MCU then
   does with it is UNKNOWN.** (This said "the MCU sees a button held down
   permanently" until 2026-10-12, and was contradicted six lines below by this
   file's own "DONE 2026-10-03 — `Disp-P4` is not the button line". CLAUDE.md
   carries the retraction; the conclusion, fit nothing, is unchanged.) The 0b capture
   only proves the line tolerates an analyser's near-infinite input
   impedance; it says nothing about 23K.

   **DONE 2026-10-03 — the test was run.** `Disp-P4` is not the button line
   and not the display answering: across 344 s of live bus, including every
   button press, the whole menu and both brew directions, it moves only
   during the 204.5 ms after the supply rails come up. See CLAUDE.md for
   the full interval list and what is and is not explained by it. The
   branch stays an unterminated test point.

   **The buttons turned up in the 67-bit frames instead**, on the same two
   wires — bits 49 and 58, active low. See CLAUDE.md for the mapping and
   semantics. No extra GPIO, no divider on the bidirectional line.

   **`ESD4` (pin 3) is the clock, `ESD1` (pin 2) the data** — also settled
   2026-10-03, after being unrecorded since 0b.

   **Where each part physically sits:**

   ```
   inside the machine          |  cable out       |  project box, outside
   ---------------------------------------------------------------------
   J5 -[T-piece]- 8.2K inline  |  3 x 26 AWG      |  2 x 15K to GND
                                                     (BUILD: 16.4K)
        |                      |  silicone,       |  ESP32-S3 GPIO4/5/GND
        +- machine harness     |  twisted pairs   |  USB charger (isolated)
   ```

   **Route this cable AWAY from the mains loom**, and twist each signal
   with a ground return. Splitting the divider is right for fault
   protection but it moves the design's lowest-margin node onto 50 cm of
   unshielded wire: in the open-drain case the pin sits at 2.69 V against
   a VIH of 2.48 V, so **210 mV of headroom**, on a node whose Thevenin
   impedance is 8.2K ∥ 15K ≈ 5.3 kΩ. The machine's own loom carries the
   heater line and a random-phase BTA204S triac; a capacitively coupled
   commutation spike above 210 mV corrupts frames intermittently, which is
   the worst kind of fault to chase.

   **The first of these is NO LONGER CONTINGENT — 15 K fails its corner and
   16.4 K is now the specified value regardless of whether drops occur.** The
   second remains contingent. Read what follows with that split in mind; its
   sizing inputs (5.3 kΩ source, 210 mV margin) are **15 K figures** and would
   need redoing at 16.4 K.

   Two remedies if it happens, cheapest first. **Raise the shunt leg to
   16.4K (2 × 8.2K in series)**: the pin then sits at 2.80 V instead of
   2.69 V, so the margin goes 210 mV → 325 mV, loading is unchanged
   (171 µA against 179 µA), and the push-pull case is 3.33 V, well under the
   3.6 V absolute max. **Those are NOMINAL figures** — the corner is
   **+18 mV** and **3.52 V**, which is still the right answer but not by the
   width this paragraph implies. **This is no longer contingent: 15K fails
   its corner and 16.4K is the value to fit.** See the J5 corner section. Only then reach for **~470 pF from
   each GPIO to GND at the box**: 5.3 kΩ × 470 pF = 2.5 µs against a 36 µs
   clock high, so it settles in a fifth of the pulse and puts the corner at
   64 kHz, below triac spike content. Do not go much above 1 nF.

   The internal-pull-down warning applies to both: with a 16.4K shunt the
   45K pull-down drags the pin to 2.41 V, below VIH, exactly as it does
   with 15K.

   **The 8.2K series resistors go at the T-piece, not in the box.** Solder
   each into the stripped window so the branch is
   `[J5 conductor] - [8.2K] - [26 AWG out]`, heat-shrink over it. The
   failure to design for is a conductor inside the tap cable chafing
   through to the ground return running beside it in the same bundle, or
   to another conductor at a crushed cable gland. With the resistor at the
   far end that puts a J5 signal straight to ground — display stops, and
   if anything upstream is driving push-pull it is driving into a short.
   Disturbing the machine, which invariant 1 forbids.

   With 8.2K at the source the same fault is current-bounded: under 0.6 mA
   if the line is driven from 5 V, 0.39 mA if it is only held up by the
   board's 4.7K pull-up. **Not a non-event though** — in the pull-up case
   the J5 node becomes a divider and sags to 5 × 8.2/(4.7+8.2) = **3.18 V**
   against the 3.5 V the display needs, so it may misread until the fault
   is found. The point is that it is a soft fault instead of a short on the
   bus. Series resistance belongs at the source because part of its job is
   protecting what is upstream from everything downstream, and the same
   8.2K also bounds clamp-diode current to ~0.5 mA if the ESP32 is
   unpowered while the machine is on. The 15K shunts stay in the box,
   where they are reworkable.

   **Ground takes no series resistor.** It is the reference both shunts
   return through, so an 8.2K there lifts the ESP32 ground ~1.9 V (1.85 V
   open-drain, 2.07 V push-pull, solving the two parallel 23.2K branches
   against the common 8.2K) and cross-couples the two channels. An earlier
   draft said 3.5 V, computed from the no-lift current and ignoring that
   the lift itself reduces that current.

   **Sharing ground is safe** because the machine's 5 V comes from an
   isolated IRM-03-5 and the ESP32 runs from its own isolated supply —
   neither is earth-referenced, so there is no loop. The realistic way to
   break that is **not** the power supply but the **USB data cable**:
   plugging the ESP32 into a laptop on a 3-pin adapter, or a dock, earths
   its ground while the tap is connected. Low hazard, since the machine's
   5 V is an isolated SMPS secondary, but it completes a loop straight
   into the noise problem above — so reflash with the tap unplugged.
   Note also that a Class II charger's Y-capacitor leaves the ESP32 ground
   floating near half-mains with a small leakage current: "isolated" is
   true, "quiet" is not.

   **SUPERSEDED 2026-10-09 — one connector per harness, not one shared.**
   This used to say "cut the 6S lead in half… use three conductors; cut the
   other four back flush". That was sized when 0c was two lines. It is now
   four, plus J2 position 1 for Phase 1, and the count no longer fits:

   | harness | to the box |
   |---|---|
   | J5 | `VSS`, `ESD1`, `ESD4` — 3 |
   | J2 | Steam, 2Cup, 1Cup, Water, **+5 V common** — 5 |
   | J4 | `#`, `T` — 2 |
   | | **10** |

   A 6S balance lead carries **seven**, and the instruction to cut four back
   flush would have destroyed capacity that is now needed.

   **The scheme instead: a separate keyed connector per harness, and the pin
   counts do the keying for free.** J5 is 4-pin, J2 is 5-pin, J4 is 3-pin, so
   the three cables are **physically incapable of being cross-plugged** —
   which matters, because plugging J2 into the J4 socket would put 5 V where
   a flowmeter signal belongs. Standardising on one connector type would
   throw that away.

   Each cable carries its own series conditioning in its T-piece, so the box
   side is always high-impedance and a swapped cable is a swap, not a
   rebuild. **The 5S and 6S become practice pieces** for the
   strip-and-solder — the pack holds one of each, so write the allocation
   down rather than rediscovering it with a bag of cut leads.

   **Size the box for three harnesses.** 0c adds J2 and 0d adds J4; they
   all land here. A box that fits only this one is a job done twice.

   **J2 needs FOUR dividers, not two.** An earlier version of this line said
   "two dividers, on PB5 and PB6 only — the other three pins of J2 need not
   reach the box", which is the superseded two-line plan: the operator asked
   for all four functions on 2026-10-04 and CLAUDE.md, the firmware pin budget
   and `SwitchBank::N_LINES` have all said four ever since. **Four of J2's five
   conductors reach the box** (Steam, 2Cup, 1Cup, Water); only position 1, the
   +5 V common, stays behind. Building to the old sentence gets you a harness
   with three conductors too few.

   More than convenience: **J2 carries no ground** (positions 1–5: +5 V common, Steam,
   2Cup, 1Cup, Water), so 0c's dividers have to reference the ground that comes
   back on J5's `VSS`. That makes this tap's ground conductor load-bearing
   for 0c as well as 0e, and it is why one box is the right shape rather
   than three.

   **SHUNT VALUE SUPERSEDED 2026-10-19: use 16.4 K (2 × 8.2 K), not 15 K** —
   the 15 K fails its tolerance corner. The diagram and table above record
   what is currently fitted. See the J5 corner section.

   Safe either way the line is driven *at nominal*: 3.23 V at the pin if
   push-pull, 2.69 V if open-drain via the board's 4.7K pull-ups (VIH 2.48 V),
   and the machine's own high never drops below 4.16 V against a display
   needing 3.5 V. **Do not enable an internal pull-down** — 45K across the 15K
   leg drops the open-drain case to 2.33 V and the pin stops reading high.

   **"SAFE EITHER WAY" IS A NOMINAL CLAIM AND THIS ENTRY DID NOT SAY SO.**
   The corner was first run 2026-10-19 and the as-built 15 K **fails the
   open-drain corner at −70 to −87 mV**. It decodes, so nothing is on fire;
   but the perfboard must not copy it, and the 4.16 V quoted here is
   calculated rather than metered. See the J5 corner section above.
   **The T-piece is built and verified** (2026-10-03). The machine runs
   normally through it and the tap decodes the live display with 0 %
   corrupt frames — which holds with or without the shunts; see
   `analysis/captures/README.md`.

   **BUILT WITH 22 AWG, NOT THE 26 AWG THE DIAGRAM ABOVE SPECIFIES — operator,
   2026-10-29.** No 26 AWG was owned until 2026-10-28 (`docs/shopping-list.md`
   §1 has the history), so the "3 x 26 AWG" in the layout and the
   `[26 AWG out]` branch were the specification, and this paragraph let them
   read as the as-built. Thicker than specified, so the chafe argument is
   unaffected. **These are test articles: the T-pieces that go into the
   machine will be rebuilt from the 26 AWG silicone now in hand**, and the
   verification in this paragraph belongs to the 22 AWG set, not to them.

   Then `display on` (persisted in NVS) and check `status`.

   **Firmware caught up in 0.8.0** (`display.{h,cpp}`), against the
   2026-10-03 captures as fixtures:

   - Parses the real structure — a 67-bit button frame and a 66-bit digit
     frame — instead of only the merged 133-bit form, so the 1–8 % of
     digit frames that arrive split are no longer dropped. That includes
     the timer's own `000`, which the old decoder never once saw.
   - Reads the **timer/temperature mode bit** instead of guessing from how
     long a reading had held still. `100` is now unambiguous.
   - Decodes the **button state**, and the **letters**, so `PrG` is a
     reading rather than a garbled frame — `DisplayBus::programming()` is
     true while the brew temperature is being changed.
   - Matches frame lengths **exactly**, which stops the ~564-bit power-on
     initialisation burst being clamped to `MAX_BITS` and decoded as a
     reading at all.
   - Treats an **all-blank display as its own mode**, not as 0 °C. This was
     wrong in the first draft of 0.8.0 and caught in review: `' '` was
     skipped in the numeric accumulate, so `"   "` came out as a confident
     0 °C — 99 times in one 30 s capture, because the display blanks
     constantly while a value is being edited.
   - Reads that blink as the signal it is. Two blank frames inside 600 ms
     mean a value is being **adjusted**, so a number shown then is the
     **setpoint being dialled in**, not the boiler — a ramp to 120 used to
     look like a boiler at 120. A lone blank, which is what a cold boot
     emits, is not a blink.
   - Latches `setpointTouched()`. `PrG` is a transient banner — 2 frames in
     372 while an adjustment ran for seven seconds — so a consumer that
     samples a level misses the event entirely. The latch is what belongs
     on a shot record.

   All four are mutation-tested: breaking any one of them fails a named
   test rather than passing quietly.

   **Still to do:** nothing consumes any of this yet. The shot record has
   no field for the machine's own timer, no `setpoint_changed` flag, and
   `sample_t.temp_dc` is still never written. That is the next decision,
   and it is a schema question rather than a firmware one.

   **Still to do after that:** decide what gets stored. `sample_t` already
   reserves `temp_dc`, and the shot timer is a truer boundary than the
   scale's plateau — but temperature is *not* readable during a brew
   (the display is showing the timer), so brew-temperature telemetry is
   not available from this source. Re-run `analysis/0f-noise-floor.ipynb`
   once the timer is logged; the shot-time figure may shrink.

4. **0c — switch sensing. All four lines are tapped because the operator
   asked for all four functions — NOT because Steam is needed to protect
   `boiler_temp_start_dc`.** That argument was made on 2026-10-04 and
   **disproved on 2026-10-05**: captured through a steam cycle, the display
   blinks **`165`** for 49 s and then blinks **` 95`** while cooling
   (`analysis/captures/2026-10-05-display-bus-steam.sr`). The bus already
   reports steam, and better than a switch line could — it says how hot and
   when the boiler came back, not merely that a press happened. Fit the
   Steam divider by preference; do not cite necessity.

   **J2 PINOUT MEASURED 2026-10-04.** The harness is
   **J2**, silkscreen `BOTONE`, **5 pins**. Mapped by unpowered continuity
   through the switches, then a powered DC check — no parts, no risk,
   about twenty minutes:

   | Position | Net | MCU pin | Evidence |
   |---|---|---|---|
   | **1** | **+5 V, switch common** | — | present in **all four** continuity pairs; **5.0 V** measured |
   | 2 | Steam | PA10 | **FUNCTIONAL 2026-10-08** — lever B down, logged `steam` through the fitted tap |
   | 3 | 2Cup | PB6 | lever A down |
   | 4 | 1Cup | PB5 | lever A up |
   | 5 | Hot Water | PB4 | **FUNCTIONAL 2026-10-08** — lever B up, logged `water` through the fitted tap |

   **Two findings that correct standing assumptions:**

   - **There are TWO levers, not one.** Lever A is 1Cup/2Cup, lever B is
     Hot Water/Steam, and they share conductor 1 as a common. CLAUDE.md
     described a single SCI R13-29.
   - **All four function lines are wired.** CLAUDE.md called Steam and
     Water *"present on board, likely unwired on a Dream"*. They are
     wired. Same category as the J4 census: an assumption sitting in the
     hardware-facts section until someone measured.

   Note also that **2Cup sits before 1Cup** in position order, while the
   old list read "+5 V, Steam, 1Cup, 2Cup, Water". That list was never
   positional; reading it as positional swaps the shot with the clean
   cycle.

   Grades of evidence differ and are recorded as such: **1Cup/2Cup is
   functional** (lever operated, continuity observed), **Steam/Water was
   identified by inspecting the plug.**

   **That justification is now void.** An earlier version said "neither is
   on the Phase 0 path, so inspection is proportionate" — and the same
   change put both squarely on it, since 0c now taps all four lines. An
   inspection-grade mapping is load-bearing for the design built on it: if
   positions 2 and 5 are swapped, a steam event is logged as a water draw
   and vice versa, which breaks precisely the two problems the four-line
   tap exists to solve.

   **Two minutes settles it**, next time the machine is open: J2
   unplugged, hold lever B each way, watch which continuity pair closes —
   exactly what was done for lever A. Observing that lever B *does things*
   (boiler to 165 °C one way, pump the other) does **not** establish which
   conductor carries which.

   **Polarity is established by measurement, not assumed.** The continuity
   test shows a lever closes common to its direction line; the common
   measures 5.0 V; the four direction lines measure 0 V at rest; and
   **across the closed contacts (1→4, 1Cup held) the meter reads 4.95 V
   idle and drops to zero when pressed.** That last one is the same
   physical fact as the one before it, read differentially rather than to
   ground — a corroboration, not an independent third route. So "pressing pulls high, idle
   is low" rests on measurement rather than on the traced schematic — the
   source that was wrong about J4's net count and about Steam/Water being
   unwired.

   **The board's pull-down value is NOT determined.** The drop across the
   closed switch is the drop across R45 (100 Ω), so
   `R_pulldown = 100 × (5 − V)/V` would give it — but the reading was
   "zero" at the meter's resolution, not a millivolt figure. Anything from
   ~50 kΩ upward is consistent, assuming the pen meter resolves 10 mV on
   its 20 V range; at 100 mV resolution the bound would be ~5 kΩ. The
   formula is right if anyone wants to finish the job with a millivolt
   reading. It does not matter for 0c (a 370 K divider
   in parallel with any of those shifts nothing), and it is recorded only
   so nobody later mistakes "drops to zero" for a measured pull-down.

   <!-- fact:divider-j2 150 K / 220 K -->
   Divider **150 K / 220 K** into the GPIO (5 V → 2.97 V, 13.5 µA,
   dropping **1.35 mV** across R45). **The 5 V is measured, which is the
   step skipped at J4.**

   **0d rejects this same pair and 0c keeps it — not a contradiction.**
   0c's corner is **+203 mV** (rail −5 %, VIH +5 %, 1 % resistors), where
   J4's is −45 mV. The difference is entirely source impedance: **100 Ω
   here against 9.4 kΩ at J4.** Do not "correct" 0c to J4's 100 K / 220 K on
   the strength of the J4 warning. And unlike
   J4's open-collector output, the switch line is driven through R45's
   **100 Ω**, so a 370 K divider loads it by about a millivolt — the
   unloaded arithmetic is valid here, where at J4 it was not.

   **NOT 220 K / 220 K**, which an earlier note here recommended: that
   gives 2.50 V against an ESP32-S3 VIH of 2.475 V — 2.60 V if the 3.3 V
   rail sits 5 % high — so a pressed button could read as not pressed.

   Gives real shot boundaries and `first_drip_ms` — but that is the small
   half. **J2 is the only actuation path on the machine**, so it is also
   how a shot ever gets stopped automatically. 0b's display timer does make
   0c redundant for *boundaries*; it does not make J2 redundant, and an
   earlier note here wrongly implied it might.

   Phase 1 stop-at-weight works by pulsing the 2Cup line when the scale
   hits target. Two constraints from CLAUDE.md that shape the hardware:
   there is **no fail-safe-open** (the board latches, so stopping needs an
   active pulse, never an interrupter), and **anything that can hold the
   line silently reprograms the machine** (hold >2 s rewrites that
   direction's stored timer). Hence the mandated hardware one-shot — the
   firmware must be physically incapable of holding.

   **Feasibility — measured, `analysis/overshoot.py`.** Overshoot after the
   stop is most predictable at **+2 s: +0.20 g, σ 0.071 g** (n=38).
   Trigger 0.2 g below target, read at +2 s, land inside **±0.14 g at 2σ**
   — a third of a ±0.5 g tolerance. Stop-at-weight is viable.
   It is a *threshold trigger*, not a control loop, so 0f's 12 % flow
   noise floor does **not** gate it.

   Measured later the spread doubles (σ 0.12 at 5 s) because the Bookoo's
   load cell drifts under a hot cup.

   **DECIDED 2026-09-28: `SETTLE_MS` stays at 5000 ms — don't relitigate.**
   Shortening it to 2500 would tighten `yield_final_g` (σ 0.07 vs 0.12) but
   redefines the field and breaks comparability across the 46-shot clean
   set (a filtered cohort, not the archive — see CLAUDE.md),
   for a metric nobody is optimising. It costs nothing for stop-at-weight:
   `SETTLE_MS` governs what is *logged*, while a Phase 1 controller reads
   the scale at +2 s for its own trigger. The two are independent.

   The earlier note here (median +0.40 g over n=46) was computed on a
   contaminated population and is superseded. **`overshoot.py` records
   seven shots with no samples after `stop_ms` at all** — a count over the
   45 rows its filter admits, not a rate over the archive — the cup was lifted and the
   detector finalised at the peak, so their `settle_offset_g = 0` is
   structural, not measured. `overshoot.py` detects them by requiring a
   ≥4 s tail rather than trusting the field. Shot 37 is also excluded:
   a −3.10 g step then +2.00 g inside 180 ms (something knocked the cup)
   followed by a slow lift the 5 g threshold never caught.

   **Connector census** (from the traced schematic, so one order covers
   the rest of phase 0):

   | Ref | Silkscreen | Pins | Carries |
   |---|---|---|---|
   | J5 | — | 4 | GND, `ESD1`, `ESD4`, `Disp-P4` — **JST XH, `XHP-4`** (verified) |
   | J2 | `BOTONE` | 5 | **positional: 1 = +5 V common · 2 = Steam · 3 = 2Cup · 4 = 1Cup · 5 = Water** — `XHP-5`. *(Read "+5 V, Steam, 1Cup, 2Cup, Water" until 2026-10-29 — the non-positional order CLAUDE.md's J2 table warns swaps the shot with the clean cycle if read as positions, on the one row a builder reads with the plug in hand.)* |
   | J4 | — | 3 | flowmeter — `XHP-3`. **Measured 2026-10-04: ground (`T`), +5 V (`+`), ONE signal (`#`)** — not two nets. |
   | J3 | — | 2 | NTC thermistor |
   | J1 | — | 9 | **mains and loads — never touch** |
5. **0d — flowmeter — FIRST ATTEMPT FAILED, 2026-10-03.** Recorded because
   it is the **only time Phase 0 has violated hard invariant 1**, and the
   mechanism is still not established.

   **What was measured, and nothing beyond it:**

   - 2 s idle on D0/D1 (`2026-10-03-flowmeter-j4-idle.sr`): **0 edges**,
     D0 steady low, D1 steady high.
   - 90 s spanning a 2Cup cycle (`2026-10-03-flowmeter-j4-null.sr`):
     **0 edges on both channels**, same levels.
   - The machine stopped with **`E01`** on the display.
   - **J4 disconnected, machine power-cycled, 2Cup run again: no error.**

   Those last two lines are the finding: **the tap caused the fault.** The
   *mechanism* is not established and must not be written down as though
   it were. Note the capture contains no transitions at all, so nothing in
   it timestamps the cycle or the fault — that correlation rests on the
   operator's account, not on the file.

   **The captures say almost nothing else, and an earlier draft of this
   section claimed they did.** It argued that the absence of 50 Hz hum
   proved the probes were not floating. **That is refuted by this repo's
   own files.** fx2lafw delivers all eight hardware channels regardless of
   how many are enabled, and these captures store them at `unitsize=1`, so
   the unprobed ones are readable:

   | Capture | D2 | D3 | D4 / D5 | D6 / D7 |
   |---|---|---|---|---|
   | `-flowmeter-j4-idle.sr` | **0 edges, flat** | 0 edges, flat | 50 Hz | 50 Hz |
   | `-flowmeter-j4-null.sr` | **50 Hz hum** | 0 edges, flat | 50 Hz | 50 Hz |
   | `-display-bus-flush-timer.sr` | 0 edges, steady high | 0 edges, flat | **30 / 32 kHz** | 50 Hz |

   An unconnected input picks up whatever is near it — **50 Hz mains
   through to tens of kHz**, the latter almost certainly crosstalk from
   the 9.9 kHz display clock and its harmonics. An earlier version of this
   table said "D4–D7 hum", which is wrong: in the flush capture D4 and D5
   run at 30 and 32 kHz.

   **The decisive row is D2.** It is connected to nothing in either J4
   capture, and it reads **dead flat in one and 50 Hz in the other**, 88 s
   apart in the same session. The signature is not stable on one channel
   within one sitting, let alone diagnostic. Both of the J4 capture's
   signatures — steady low, steady high, no edges — are reproduced here
   by channels on nothing at all. The captures cannot distinguish "probes
   on the wrong nets" from "probes on nothing".

   Two further traps in that reasoning, both worth keeping:

   - On a purely digital capture, **"zero edges" and "no hum" are the same
     measurement, not two.** A line sitting at a rail with 200 mV of
     ripple produces no threshold crossings. The earlier draft counted one
     observation twice and read it as corroboration.
   - The capture was taken with **all eight channels live**, against this
     repo's own written procedure — `tools/decode-display-bus.py` says
     "enable ONLY the channels in use". Five of the six unused channels
     are full of hum. Anyone opening the file in PulseView sees that.

   **The sensor is a DIGMESA** (2026-10-03, read off the body — the label
   is worn and was first read as "DIMASE PSACH"). Its three conductors are
   marked:

   **The three ROLES were measured on 2026-10-04.** The conductor→marking
   mapping is corroborated but still rests on re-reading the worn label;
   `T` is anchored independently by continuity. So the
   symbol-reading below is now a record of how it was guessed, not of what
   is known:

   <!-- fact:j4-conductor-map ground | yellow;;green | 5.0 V, steady through a flush;;black | 0.3 V parked;;continuity beep to J5 pin 1 -->

   | Marking | Role | On the 2S T-piece lead | Measured |
   |---|---|---|---|
   | `T` (middle) | **ground** | yellow | continuity beep to J5 pin 1 `VSS`; **no resistance value recorded** |
   | `+` | **+5 V supply** | green | 5.0 V, steady through a flush |
   | `#` | **signal** | black | 0.3 V parked, **2.5 V averaged during flow** |

   **Normative home for the conductor map is CLAUDE.md**, which carries the
   same three rows beside the J4 conditioning. This copy is the **bench
   record** and keeps what CLAUDE.md does not: that `T` is the middle
   conductor, which symbols were guessed right, and the confidence-label
   lesson below. The three roles and the three colours are tagged on both
   sides, so `tools/check-facts.py` fails if they ever diverge — which is
   the one divergence here that could put a probe on the wrong wire.

   The guesses that turned out right: `T` as the IEC earth symbol ⏚ (a
   stem on a bar), `#` as a square-wave symbol ⌍. Both were labelled
   "plausible" and "a guess" before the meter went on them, which is the
   correct amount of confidence to have had.

   Earlier confidence labels here read "high" for both. They were not
   earned: the `#` cell's own text describes a guess, and the label came
   from the same worn plastic that produced "DIMASE PSACH".

   **How it was measured (2026-10-04), with a plain DC pen meter** — no
   frequency mode, no scope, and nothing ever left attached:

   1. **Continuity identifies `T`.** No convention, no power. It also
      cross-checks the markings: the ground *position* matching where `T`
      sits is what lifts that row above a label-reading.

      **This failed at the board header and worked at the T-piece
      window.** Probing all three `B3B-XH-A` header pins against the J5
      pin 1 pad gave **no beep at all**; the same test at the window in
      the 2S lead gave a clean beep on yellow. The failure is
      **unexplained** — plausibly the shrouded header pins or an occupied
      J5 pad, but that is a hypothesis, not a finding. Recorded because
      the retry plan prefers back-probing, and back-probing the header is
      precisely what did not work.
   2. **DC volts during a flush separates `+` from `#`.** While water
      flows `#` averages toward mid-scale and `+` holds at full supply.
      That is what was seen: 5.0 V against 2.5 V.

      An earlier draft said both would **idle near the supply** and that
      `#` would land at exactly half. Neither holds. An open collector at
      rest sits wherever the turbine parked — this one parked **low, at
      0.3 V** — and the mid-scale value depends on a duty figure that is
      **not known for this model**. The contrast is robust; the arithmetic
      on top of it is not.
   3. **`+` is whatever is left.** It **was** probed on 2026-10-04 with a
      20 MΩ DMM — that is how the 5.0 V above was read — and nothing
      happened. So the rule is not "never touch `+`":

      > **`+` is read-only, through a high-impedance meter, and nothing
      > else.** Nothing with an input clamp — no analyser, no GPIO, no
      > divider — **and nothing that sources or sinks current: never
      > power the ESP32 from it, never feed it from another supply, never
      > hang a pull-up on it.** It is the machine's own sensor rail off a
      > 3 W IRM-03-5.

      <!-- fact:j4-plus-readonly read-only, through a high-impedance meter, and nothing else|nothing that sources or sinks current|never power the ESP32 from it|3 W IRM-03-5 -->

      **THIS IS THE DELIBERATE SECOND STATEMENT OF THE RULE. Normative
      home: `CLAUDE.md`, the standalone block directly under the six
      invariants.** Duplication is this repo's signature defect and is
      collapsed everywhere else; here it is kept on purpose, because the
      same rule has been lost from CLAUDE.md three times (see the note
      further down this entry). If the two ever disagree, CLAUDE.md wins —
      and the disagreement is itself the bug. Changing either means
      changing both in the same commit.

      **It is deliberately NOT byte-identical, and a draft of CLAUDE.md's
      pointer called it "verbatim".** This copy omits the closing *"A 20 MΩ
      DMM is fine, and is how its 5.0 V was read on 2026-10-04"*, because
      the sentence introducing this blockquote already says exactly that.
      The four clauses that carry the rule are fact-tagged on both sides —
      read-only through a high-impedance meter and nothing else, nothing
      that sources or sinks current, never power the ESP32 from it, and the
      3 W IRM-03-5 rail — so CI fails if either side loses one. The
      trailing sentence is provenance, not rule.

      Two earlier drafts were wrong in opposite directions. "Must not be
      probed" is contradicted by the step above and discards the evidence
      that a high-impedance probe is safe. "Nothing with an input clamp"
      enumerates only *sensing* devices and leaves a **power tap** open —
      the single most tempting thing to do with a 5 V pin on an accessible
      connector. The rule is about **current**, not clamps, which also
      stops it depending on what voltage `+` happens to sit at.

   Two earlier drafts got step 2 wrong in opposite directions: the first
   said `#` would "flicker", which it will not at ~4.8 Hz; the second
   concluded from that it needed Hz mode, which does not follow — the
   mid-scale average *is* the signature. The third draft's error — that
   both non-ground conductors would idle near the supply — is recorded at
   step 2 above, together with the measurement that refutes it.

   **The census row above is wrong to put both nets on this connector.**
   It says J4 carries `Flow-P1`/`Flow-P2` — PA8 *and* PA9, two MCU pins.
   Measurement says ground, supply, and **one** signal. The census is
   traced from `techdregs/Ascaso_Dream_PID_Electronics`, which already
   ships the wrong MCU datasheet; the markings are read off the
   physical part and the meter agrees with them, which is the best
   evidence available even though the label is worn.

   So the standing **"quadrature, or two independent meters?" question is
   a false dichotomy and is now closed: neither.** One sensor, one pulse
   train, one GPIO, one counter.

   **The part, and its electrical data. Read the source column** — the two
   most load-bearing numbers are NOT in hand as primary sources. Every
   Digmesa PDF URL for this family 301-redirects to HTML; the datasheet
   could not be retrieved on 2026-10-03.

   | Spec | Value | Source |
   |---|---|---|
   | Model | **Digmesa FHKSC**, three-pin | Ascaso-USA listing |
   | Ascaso part | `I.2560` | vendor listings |
   | Digmesa part | `932-9525-B` | vendor listings |
   | Orifice | 1.0 mm, 5 mm barbed | vendor listings |
   | Flow range | **0.033–0.40 L/min** | **the datasheet, retrieved 2026-10-09.** Note this is the range over which K is *specified*, not a hydraulic limit |
   | Output | **NPN open collector**, 0 VDC active, saturation <0.7 V | Daitron + Digmesa product pages, corroborated |
   | Signal load | max 20 mA | as above |
   | Supply | **+3.8 to +20 VDC**, <8 mA | **the datasheet, retrieved 2026-10-09.** The "+2.8 V" that contradicted it was the 9NB nano brass — a different Digmesa product |
   | K-factor | **2382 pulses/L** for `#932-9525-B` Ø1.00 mm 0° <!-- fact:digmesa-k-factor 2382 pulses/L --> | **the datasheet itself, retrieved 2026-10-09** |
   | Accuracy | ± 2.0 % | product pages |

   **The datasheet (2026-10-09) says 2382 and the in-circuit measurement said
   5900–6500, and the shots ~5×. **What the counts measure is unresolved**,
   and CLAUDE.md's 0d entry is the live account — the 50 Hz reading quoted in
   earlier versions of this line is dead. Two
   weighed flushes into a jug on the yield Bookoo, free flow, no basket:
   **164 counts / 25.2 g** and **1209 counts / 204.4 g**, i.e. 6508 and 5915
   pulses/L. **Both rows are UNUSABLE as a K-factor** and are annotated to that effect in
   the database's `notes` column. An earlier version said "the larger
   preferred"; neither is preferred, because what the in-circuit counts
   measure is unresolved. They are kept as a record of what was observed.

   **Provisional.** 1209 counts is equally consistent with ~490 real pulses
   each ringing past the 2 ms filter and scoring extra counts. Against that:
   the rejected:counted ratio held at **3.32 and 3.34 across an 8× volume
   change**, which is what correlated ringing looks like and not what
   independent noise looks like. Suggestive, not proof — put the analyser on
   `#` and compare edge counts before calling this the sensor's K-factor.

   **The search-summary figures are superseded and the framing was wrong.**
   Summaries gave 2386 at Ø 1.00 mm, and one gave "2386 / 2476 / 2436 for
   0° / 90° / 180° **mounting orientation**". Those are not orientations of
   one part — they are **separate part numbers** (`-B`, `-B90`, `-B180`,
   `-B270`), each with its own measurement curve. Ours carries no suffix, so
   0°, so **2382**. Recorded because the wrong framing made a ~4 % "spread"
   out of four distinct SKUs.

   **The 3.8 V minimum is now SETTLED, and it settles in favour of the
   brownout hypothesis.** This entry called it "the most load-bearing
   unverified number" and warned that a secondary +2.8 V figure would remove
   the mechanism. The datasheet says **+3.8 to +20 VDC**, and the +2.8 V came
   from the 9NB nano brass, a different product. So 3.3 V IS below this
   sensor's minimum and the brownout mechanism stands.

   An earlier draft of this section used **1300 pulses/L**, taken from a search summary and never
   checked against a datasheet — that is the Ø 1.50 mm figure (Digmesa's
   FHK table gives 1386 p/L / 0.7216 g/pulse for 1.50 mm against 2223 for
   1.00 mm). Everything derived from it was wrong by ~1.8×. **This is the
   same failure the STM32 datasheet note warns about**: the sourcing was
   flagged as secondary and honest, and then the freely available primary
   source was never fetched. Flagging weak sourcing is not a substitute
   for replacing it — which is why the table above now names which rows
   are still only flagged. **SUPERSEDED 2026-10-09 — the datasheet was retrieved.** What follows is the
   record of why it took so long, and the "treat as permanently secondary"
   instruction at the end of it no longer applies: the K-factor (2382) and
   the supply minimum (+3.8 V) are now datasheet-grade.

   **Retrieving this datasheet was BLOCKED, not cheap.**
   Every published URL for the `932-952x` family serves HTML rather than a
   PDF (`digmesa.com`, `shop.digmesa.com`), the Christian Berner mirror
   404s, and the Google cache is HTML — checked 2026-10-03 and again with
   browser headers on 2026-10-04. The remaining routes are a distributor
   request or `info@digmesa.com`. Until one works, treat the K-factor and
   the supply minimum as permanently secondary.

   **`E01` is the dose-control fault — confirmed from the manual.**
   Ascaso's own `MAN.29-V10` alarm table gives, across four languages:
   "Dose control fault" / "Fallo control volumétrico" / "Fehler
   Fassungsvermögenskontrolle" / "Défaut du contrôle de volume", with
   `E02` the probe fault. So `E01` is the volumetric path, which is the
   flowmeter.

   Note the manual renders it **`E01`, with no decimal point**, so the dot
   in the operator's "E.01" is probably punctuation rather than a lit
   segment. CLAUDE.md's open decimal-point question stands unanswered.

   A note on how that was argued before: an earlier draft paired a dealer
   customer review with our own tap-on/tap-off test and called the two
   together "persuasive". They are not two views of one claim — the
   review is evidence about *what the code means*, the controlled test is
   evidence about *what caused this particular fault* and would have
   "corroborated" any code equally. The conclusion survived; the reasoning
   did not.

   **Open collector changes what the capture should look like.** The
   sensor only ever pulls *down*; the high level comes from a pull-up the
   board must provide, and `#` **pulses LOW**.

   An earlier draft added "so `#` should idle HIGH — if a capture shows
   the opposite, suspect the tap, not the sensor." **The 2026-10-04
   measurement refutes that**: `#` parked at **0.3 V**, i.e. low, because
   an open collector at rest sits wherever the turbine stopped. The
   heuristic would have told a reader to blame their tap for the one thing
   now shown to be normal. It also
   means the line is only as stiff as that pull-up when high, which is the
   one way a careless load can hold it low and suppress pulses without
   breaking anything visibly.

   **The leading hypothesis is that the sensor lost its supply, and the
   2026-10-04 measurements make it stronger — but not as much as an
   earlier draft of this paragraph claimed.** The supply is **5 V,
   measured** (12 V was never a reading, only the top of a datasheet
   range), so the arithmetic below is the ~12 mA case rather than the
   ~82 mA one. That makes the analyser's survival more likely; **D1 is
   still unchecked.**

   **The number the hypothesis most needs HAS now checked out — 2026-10-09.**
   The datasheet gives **+3.8 to +20 VDC**, so 3.3 V is below this sensor's
   minimum and the brownout mechanism stands. The 2.8 V figure that would
   have killed it came from the **9NB nano brass**, a different Digmesa
   product. Two earlier drafts of this paragraph got it wrong in opposite
   directions — one claimed "every number it needs has now checked out" on no
   evidence, the other that it had not checked out at all. It had not, until
   the PDF arrived; now it has.

   **A second probe arrangement now fits, and it is the more informative
   one.** `#` parks at **0.3 V — LOW**. In `-j4-null.sr`, D0 was steady
   low and D1 steady high, which is equally consistent with **D0 on `#`
   (parked low, never pulsing) and D1 on `+`** as with the `+`/`T` reading
   below. Under that arrangement the sensor produced no pulses across a
   full 2Cup cycle, which is *direct* evidence it produced nothing — not yet that it was
   dead, since "the turbine never turned" and "D0 was not on `#`" both
   survive — rather than the
   mere consistency conceded below. Nobody recorded the probe positions,
   so both remain live.

   If instead the probes sat on `+` and `T`, neither is the signal,
   so neither could ever move. The analyser is **not 5 V tolerant**
   (74HC245PW on a 3.3 V rail behind 100 Ω), so clipped to `+` its input
   clamp conducts and pulls the sensor supply toward 3.3 V — **below the
   datasheet's 3.8 V minimum.** Without an external series resistor the
   analyser's own 100 Ω bounds that current at roughly
   (5 − 3.3 − 0.5)/100 ≈ **12 mA**, which is larger than the sensor's
   entire <8 mA consumption; with a 1 kΩ fitted it is ~1.1 mA and
   probably harmless. **Which of those applied was never recorded**, and
   that is the single thing that would have made this diagnosable.

   The idle levels are *consistent* with this hypothesis but do not
   support it, for the reasons in the hum section above. Competing
   explanations the data cannot exclude: the probes were on nets that
   never pulse, or a conductor was shorted. **Check D1 still reads
   correctly before trusting the analyser.**

   **What 0d can and cannot deliver. These figures were derived from the
   sourced ~2400 and the datasheet now says 2382 — so they stand.** Each
   pulse is **0.41 ml** (datasheet exactly), an espresso shot at ~2 ml/s
   pulses at **~4.8 Hz**, and the ceiling is **15.9 Hz** at the rated max
   flow of 0.40 L/min. An intermediate version of this paragraph recomputed
   them against the disputed in-circuit 5915 and got ~12 Hz; that figure is
   withdrawn. (The 4 % K-factor spread shifts these
   by far less than the retention estimate below, so it changes nothing
   here.) Inlet volume for a 36 g shot is
   *not* 36 ml — it is output plus puck retention plus the solenoid dump,
   realistically 55–75 ml, so very roughly **130–180 pulses**. That is
   enough to be a useful total and still far coarser than the Bookoo at
   ~10 Hz and 0.01 g, which resolves the pour better. **What the flowmeter
   uniquely gives is inlet volume** — water going *in*, against the
   scale's water coming *out*. The difference is exactly what the puck
   absorbs and the solenoid dumps, which no scale can see.

   For a pulse counter the binding constraint is minimum pulse *width*,
   not rate: at ~50 % duty and a 16 Hz ceiling the half-period is ~31 ms,
   so even 1 kHz sampling is ample and 100 kHz is ~6000× the rate.

   Per invariant 3 the K-factor is never baked in: log raw pulses and
   calibrate ml/pulse by weighing water on a Bookoo. The datasheet itself
   says the same — "we recommend to calibrate the number of pulses per
   litre".

   **A standing habit, from three repeats.** Every table in this entry has
   at some point claimed more than the prose beside it: a confidence
   column reading "high" over a cell whose own text described a guess; a
   header saying "If J4's rail is 5 V" when `+` and `#`'s pull-up rail are
   different nets; and an Evidence column asserting "lever B down" when
   the operator had said they identified those two by looking at the plug.
   The prose was right all three times. **Write the table cell last, from
   the prose — not the prose as a hedge on a cell already written.**

   Related: the `+` safety rule has been lost from CLAUDE.md **three
   times**, each time because it lived as a row in the conditioning table
   and the table was rewritten when the divider changed. **As of 2026-10-19
   it is no longer beneath that table at all** — it is a standalone `##`
   block directly under the six invariants at the top of CLAUDE.md, ~2300
   lines away from the divider that kept eating it. Rewrite the divider
   freely; the rule is not in that neighbourhood any more.

   **Carry into the retry:**

   - **Record the as-built tap before powering anything.** Series value,
     whether a shunt is fitted, which conductor each probe is on. Not
     knowing this is why the incident above cannot be diagnosed, and it
     costs one line in a notebook.
   - **One channel, `#` only, 8.2 kΩ series into the analyser's clamp**
     — the 0b arrangement, ~146 µA. That part is safe at any plausible
     rail.
   - **ESP32 divider: 100 K series + 220 K shunt → 3.13 V. SOLVED
     2026-10-04 by measurement.** The node is a **10.04 kΩ pull-up to
     5 V** against a **147.4 kΩ pull-down** — both read unpowered in Ω
     mode at the T-piece window. That predicts an idle-high of
     5 × 147.4/157.44 = **4.681 V** against **4.69 V measured**, so the
     model is right and the source impedance is R_pu ∥ R_pd = **9.4 kΩ**.

     **The corner analysis for this divider lives in
     `docs/taps-and-dividers.md` and is NOT repeated here — it used to be,
     verbatim, and the two copies had already drifted.** *(It was in
     CLAUDE.md when this was written and moved out on 2026-10-19; CLAUDE.md
     still carries the fitted value and the 134 mV.)* This file said the chosen divider disturbs the node by
     *"126 mV (2.7 %)"*; CLAUDE.md says **134 mV (2.9 %)**, and invariant 1
     quotes the 2.9 % as the J4 figure. **134 mV / 2.85 % is correct**
     (unloaded 4.681 V against 4.548 V loaded). The 126 mV was wrong, in the
     one number the project's top-line invariant cites.
     <!-- fact:j4-node-disturbance 134 mV|2.9 % -->
     *(WEAK ANCHOR, left so deliberately. This paragraph is the drift
     narrative itself: it quotes the wrong rendering, the right one, and the
     exact ratio in three consecutive sentences, so every window either has
     several satisfiers or isolates the WRONG sentence. `@3` was tried and
     reverted — it leaves exactly one satisfier, which is why the weak-anchor
     report went quiet, but that satisfier is the "is correct" line rather
     than the one this marker is for. (This note deliberately does not quote
     the figures: a draft did, and became a satisfier in the very window it
     describes.) Corrupting the
     marker's own sentence still passed. Multiplicity is machine-checkable;
     which sentence is authoritative is not. Contorting this paragraph would
     delete the record it exists for.)*

     **The mechanism is worth more than the fix.** The duplicated block ended
     with the sentence *"CLAUDE.md carries the corner table"* — it deferred to
     CLAUDE.md **and kept its own copy anyway**. That is this repo's
     signature defect, stated by the file, operating on the file. The copy is
     deleted rather than corrected, because correcting both leaves two things
     to keep in step.

     **Normative home: `docs/taps-and-dividers.md` for the arithmetic,
     CLAUDE.md for the fitted value and the 134 mV (2.9 %).** What stays here is
     the *measurement* above (10.04 kΩ, 147.4 kΩ, 4.69 V) — this file is the
     bench record — and the fitted value: **100 K + 220 K since 2026-10-08**,
     the 120 K it was designed around never having been in stock.
     <!-- fact:divider-j4 100 K + 220 K -->
     **Never 150 K**: in the drawer, fine nominally, fails at −45 mV.

     The earlier derivation here assumed a push-pull source and quoted
     5 × 15/23.2 = 3.23 V. That was the wrong model, and the right one was
     already in this file for J5: "3.23 V if push-pull, **2.69 V if
     open-drain**". It is kept in the history because the measurement that
     refuted it only happened because the error was caught first.

     **The 1 kΩ remains wrong for this connector** — CLAUDE.md's
     "conditioned to 3.3 V logic" is the *board* side between J4 and PA8.
   - **Ground to `T` directly** — the yellow conductor on the 2S lead,
     confirmed by continuity beep to J5 pin 1 (no value recorded). J4 is
     self-sufficient for ground and
     no longer needs to borrow J5's `VSS`.
   - **`+` (green) is read-only, high-impedance meter only** — no clamp,
     and **nothing that sources or sinks current**, which includes
     powering the ESP32 from it. That is the specific rule the hypothesis
     supports. See step 3 above; an earlier version of this bullet said
     "put nothing on `+`", which the measurement retracts.
   - **Prefer back-probing to interposing**, on general grounds — a
     seated harness has nothing to fail to reconnect. Note this is a
     preference, not a conclusion from the incident: the evidence is
     tap-present → fault, tap-absent → no fault, which does not
     distinguish *opening* the connector from *loading* a net. Under the
     leading hypothesis, back-probing `+` would fail identically.

   **0e — temperature** (if 0b cracked it), per docs/phase-0-plan.md.

## Known quirks (details in the auto-memory notes)

- Opening the **COM/UART port resets the board**; the native USB port
  doesn't. Never reflash mid-brew-session (cost one shot already).
- Chip claims 16 MB flash and truly has it, but **partitions must stay
  below 8 MB** (boot-loops otherwise) — documented in CLAUDE.md.
- Scale-only `settle_offset_g` is often ~0 (solenoid dump kills the tail);
  its Phase-1 meaning needs 0c's pump-off reference.
- A missed shot is recoverable if a serial logger was running:
  `firmware/test/host/replay` (did it once, shot 3).
- CF token renewal path: token lives only in `/etc/caddy/env`; update via
  a real terminal (not the `!` prefix — no TTY), then `systemctl restart caddy`.
