# Drive side — bench bring-up and the gate sweep

**DRAFT, 2026-11-16.** The procedure for bringing up the drive block
(`docs/board-and-perfboard.md`, "Drive-side layout") on the bench and for the
sweep that settles the OPEN gate questions in `docs/phase-0-plan.md`
("OPEN — the gate, readback and `drivetest`"). Nothing here closes a
drive-leg jumper: **Phase 0 never actuates** (CLAUDE.md, invariant 1), and
everything below runs **off the machine** with jumpers (a1) and (a2) open.

The firmware side is the `drive` command (firmware 0.13.2,
`firmware/src/drive_bench.h`, `runDrive()` in `main.cpp`). It fires one
'221 channel at a time and times its `Q` through the readback, continuously
sampled at a few µs, so the logic analyser is optional.

**This is not `drivetest`.** It reads no machine line and decides nothing
about whether a press would latch. The on-machine `drivetest` still carries
the open defects listed in `docs/phase-0-plan.md`: no positive control for
the sense line, discrete sampling, and the half-failed photoMOS. It is not
built.

## What the sweep has to settle

| Question | How the bench answers it |
|---|---|
| **Keep the gate (U3) or drop it** | the gate's measured spread against the drive's, below; the stuck-high-gate cost is a judgement the numbers inform but do not make |
| **The gate's RC** — candidate 470 K / 1 µF, 315 ms on one breadboard channel | `drive gate 20` on the board: min, mean and max on the real chip, at room temperature and warm |
| **The margin ordering**: drive `t_max` < gate min width; gate max ≤ 500 ms | drive max from `drive fire` against gate min from `drive gate`. Prerequisites for the firmware cap that `drivetest` needs |
| **The readback series value** (1 K proposed) | the same runs with a 10 K in one readback: if the widths agree to within a sample, 10 K reads cleanly and closes most of the pulled-'221 hazard |
| What it does **not** settle | `t_min` against the latching press width (needs the analyser on a J2 junction, machine on); the minimum gap between gate pulses (a design question) |

## Preconditions — every one, every time

1. **Off the machine.** J2, J4 and J5 T-pieces unplugged from the board (on
   the PCB, `hardware/pcb`: the J22, J24 and J25 OUT cables off — the harness
   plugs alone are not enough, the OUT cables are the machine side). The
   firmware watches J5's clock and data, J4's `#` and the four J2 lines
   for 50 ms and refuses if any reads HIGH, but that is a backstop, not the
   check: it cannot see a harness plugged into a machine that is off, nor
   J4 with the turbine parked closed, nor J2 unless a lever is held (J2 has
   no ground of its own, so its lines read LOW unplugged or not).
2. **Drive-leg jumpers (a1) and (a2) OPEN**: bare pins, no shunt. Keep spare
   shunts out of the box.
3. **LED jumper (b) open** for the timing runs. It is fitted only in step 6.
4. **U3 and its readback 1 K in, together** (board doc: "U3 and its readback
   1 K always go in or come out together"). Without U3 every channel stays
   in reset and the board looks dead.
5. **The loupe inspection** of every '221 socket's adjacent pins
   (board doc, "Bridges that can arm or light a channel") is done.
6. USB from the Mac only. No other supply.
7. **`display off` and `flow off`.** The sampler is a busy-wait and their
   interrupts would stall it; the tool refuses with either on. **Both
   settings are saved**, so procedure step 7, *Back on*, turns them back on. Spool writes
   and uploads cannot run during a sweep (it blocks `loop()`). What is left:
   the USB-serial interrupt, the scheduler tick and the Wi-Fi event task on
   the sampler's own core, and a Bluetooth or Wi-Fi flash write on the
   other. They show as an outlier run; a sweep that ran across a Wi-Fi
   reconnect is run again.

## Procedure

**0. Flash and look.** Flash the bench firmware. `drive status` must read:
GPIO38 = 0, every `A̅` = 1, every `Q` = 0. Anything else is a wiring fault:
stop and meter.

**1. The gate alone.** `drive gate 20`. Expect about 315 ms per run, with
"ok" on every run and no drive channel listed as having moved. The arm net
goes high for the gate's pulse while every drive `A̅` is high, so a drive
channel that moves here is a bridge.
- `NO PULSE` → U3 not seated, its 1 K missing, GPIO10 or GPIO38 not wired.
- `HELD` → the gate's `R_X` is open or high. The run's 1 s cap dropped GPIO38
  to end it.
- Record min, mean and max. A sweep that aborts still prints the runs it
  finished. One run well off the rest is a sampler stall until it repeats:
  run the sweep again rather than recording it.

**2. Each drive channel.** `drive fire 1 20`, then 2, 3 and 4. Expect about
220 ms each. **A channel counts as watched for cross-fire only once its own
`fire` run has shown a pulse**: with its readback 1 K open, its pin's
pull-down reads a steady 0, and it can neither show a pulse nor show that
it moved during another channel's run.
- `NO PULSE` → open `C_ext`, the chip not seated, or the readback 1 K open.
- `ENDED WITH THE GATE` → the drive pulse ended when the gate's did, so the
  gate's reset ended it, not the drive's own RC. **Two causes look the same**:
  the drive's `R_ext` open or high (a HOLD that the gate caught), or a gate
  shorter than the drive. **Meter the drive channel's R and C first**, and
  compare the run's drive width with `drive gate`'s: at about 315 ms it is
  the drive RC. Only a drive RC that meters right, with a gate measured
  short, means the ordering fails and the gate RC must grow (or the gate
  goes). Growing the gate first would let a held drive pulse run longer.
- `ANOTHER CHANNEL MOVED` → a bridge or a cross-fire: stop.
- `RETRIGGERED` → noise on `A̅` or `R̅`: check the 10 K pulls and the
  decoupling.
- Record min, mean and max per channel.

**3. Warm.** Repeat 1 and 2 with the board warm: in its box, beside or on top
of the machine with the machine at temperature, but **still unplugged from
it**. RC and the '221 drift with temperature; the margins are taken from
the worse of the two sets.

**4. The readback value.** The firmware keeps the internal pull-down on
**all five** readbacks, and the value chosen here applies to all five, so
10 K is tested as it would be built: with the pull-down. Swap one drive
channel's 1 K readback for 10 K (a 2-pin header for the bench, as the RC
was) and repeat its `drive fire <ch> 20`; then do the same for the gate's
readback with `drive gate 20`. With 10 K against the ~45 K pull-down a HIGH
sits at about 2.7 V, 0.2 V over the input threshold, so **accept 10 K only
if both runs match the 1 K widths within a few µs with every verdict "ok"**,
and record the margin. `drive fire <ch> 20 nopd` repeats the drive run
without that channel's pull-down: a comparison that says whether a
disagreement is the pull-down's, not a configuration the build runs. If
the runs with the pull-down are not clean, keep 1 K.

**5. The decision record.** In `docs/phase-0-status.md`, record the measured
widths, warm and cold:
- **keep the gate** if its min is above every drive channel's max by a
  clear margin and its max is at most 500 ms, and you accept that a
  stuck-high gate `Q` arms all four channels;
- **otherwise drop it**: the board doc's rework (U3 and its 1 K out
  together, one wire GPIO38 → the tied `R̅` net), then `drive direct <ch>`
  replaces `drive fire`.

Solder the chosen timing pairs then, **before any drive-leg jumper is
ever fitted** (`docs/phase-0-plan.md`).

**6. The LED jumper, on the bench.** Fit (b). With J2 still unplugged and
every trigger idle, AQY212EH pins 3–4 read open on each device (the static
check in the board doc). Then `drive fire <ch>` with a meter in continuity,
or the analyser through a pull-up, on that device's pins 3–4: they close for
one pulse and open again. This is where a firmware fault gets found under
observation, which is the reason (b) is fitted first.

**7. Back on.** `display on`, `flow on`, then `status`: both taps on.
Every `drive` summary ends with this reminder, because the board boots with
whatever was saved. A board back on the machine with both off logs shots
with no flow count, no boiler temperature and no setpoint, silently — and
**with the display off the brew guard loses its display-timer branch**, so
a 1Cup or 2Cup shot whose press the poller missed is unguarded and flash
can be written during it (invariant 2).

**Stop there.** Fitting (a1) or (a2) is Phase 1, a separate decision, and
the machine-side `drivetest` has to exist first.

## What the `drive` command guarantees, and what it does not

- **Every drive pin is an INPUT from the first line of `setup()` and after
  every run**, with GPIO38's output latch written LOW first. Before that,
  through the bootloader, only the external pulls hold the lines: in the
  gate build GPIO38's 10 K holds the gate's `R̅` low, and the drive
  channels' tied `R̅` (arm) net is held low by its own 10 K (and by U3's
  `Q`, when U3 is fitted);
  only in the no-gate rework is GPIO38's 10 K the arm net's. The external
  pulls hold `A̅` high and `R̅` low.
- **Triggers are written HIGH before they become outputs**, and every `A̅`
  is read HIGH before GPIO38 rises.
- **Every run ends by dropping GPIO38**, which ends any pulse still running,
  at the latest **1 s** after the trigger.
- **It refuses** with the scale armed, with the display or flow tap on, or
  with any J5, J4 or J2 line HIGH during a 50 ms watch. None of these sees a
  harness plugged into a machine that is off.
- **Before each run every `A̅` reads HIGH and every `Q` LOW.** The five `Q`
  readbacks sit on the ESP32's internal pull-down, so a chip that is not
  fitted (U3 on the Phase-0 board) reads LOW and shows as NO PULSE, not as
  noise. The price: an open readback 1 K reads LOW too, so it is caught only
  by that channel's own `fire` run (step 2). `nopd` drops the pull-down on
  the fired channel for step 4; with its chip or 1 K out that pin then
  floats and can abort the idle check at random rather than show NO PULSE.
- **Arguments are whole words**: `drive fire 3 20 nopd`. The first number
  is the channel (not for `gate`), the next the run count, and `nopd` may
  stand anywhere. Anything else is refused by name.
- **Widths are readback rise to fall**, sampled in a busy loop every few
  µs. The 5 µs trigger pulse moves only the reported rise time, not the
  width. A stall that hits every run alike would skew them all and is not
  visible as an outlier; hardware edge capture is left for `drivetest`.
- **A run count above 50 is refused**, not clamped: 50 runs already hold
  `loop()` for about 75 s.
- **A sweep that aborts prints `ABORTED`** in its summary, with the runs it
  finished.
- **It is not a bound.** The 1 s cap is a busy-wait in `loop()`, not the
  IRAM timer ISR `docs/phase-0-plan.md` calls for. A hang inside the run
  leaves GPIO38 high until the gate's own RC, or with no gate, until reset.
  On the bench with (a) open that costs nothing; it is one of the reasons
  this is not `drivetest`.
- **`loop()` blocks** for up to about 1.5 s per run, so scales and uploads
  stall for the duration.
- **A retrigger is caught within 20 ms of the fall.** A second pulse later
  than that is not seen.
