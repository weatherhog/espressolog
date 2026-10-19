# Hydraulics — the pressure-gauge path

> **Normative on:** the Ascaso Dream PID's water circuit as far as anyone
> has looked at it, which is the pressure-gauge branch and nothing else.
> Part numbers are from Ascaso's own parts diagram.
>
> **CLAUDE.md carries** the live conclusions in its `### Hydraulics` stub:
> everything is 1/8, the thermoblock port is M6 female, the gauge end
> cannot be teed, there are no spare ports, there is no plumbing census,
> and whether hard invariant 1 covers water is UNDECIDED. Those matter to
> anyone planning a tap; the measurements and part numbers below do not,
> until someone has the case off.
>
> **Changing a number here means grepping CLAUDE.md for it in the same
> commit.**

---

### Hydraulics — the pressure-gauge path

**This is the first hydraulic entry in this file.** Everything above it is
electrical: the connector census (J1–J5) has no plumbing counterpart, and
building one is a next-time-in-there job. What follows covers the gauge
branch only, because that is the only part anyone has looked at.

Note also that every tap described above is a resistor on a logic line.
Plumbing is a different risk class, and **whether invariant 1 covers it is
undecided** — the invariant was written about electrical taps, and a tee,
a leak or added dead volume all change what the machine does. Do not read
"every sense tap is high-impedance or passive" as settling anything about
water. **The 2026-10-12 amendment does not change this** — it recast the
invariant around current paths, which is still an electrical notion. Water
remains outside it.

Part numbers below are from **Ascaso's own Dream PID parts diagram**,
which is document-grade and the best mechanical source this project has
had:

| Part | Spanish | What it is |
|---|---|---|
| `I.4220` | MANOMETRO DREAM/STEEL | the gauge, 0–16 bar |
| `I.4238` | SOPORTE MANOMETRO DREAM PID | its bracket |
| **`I.4234`** | **RACOR CONEXION INTER.-MAN. M6-1/8** | thermoblock → manometer fitting |
| `I.3934` | RACOR T 1/8 H-H-M | a 1/8 tee, female-female-male |
| `I...90` | RACOR CODO MACHO 1/8 LATON | brass 1/8 male elbow |
| `I..259` / `I..706` | ELECTROV. 3 VIAS 1/8-1/8 0-15BAR | 3-way solenoid |
| `I.2237` / `I.2238` | CJTO INTERCAMBIADOR INOX | the thermoblock |

**The whole circuit is 1/8.** Even the solenoid is specced `1/8-1/8`.
There are no odd threads in this machine.

**`I.4234` is M6 at one end and G1/8 at the other, oriented thermoblock =
M6, gauge = G1/8.** The three parts of that claim have different grades
and the difference matters:

- The **G1/8 end is MEASURED: 9.55 mm**, operator's calipers, 2026-10-05.
  That sits inside G1/8's tolerance band (min ≈ 9.51 mm) and **below
  M10×1's minimum** (≈ 9.79 mm), so the identification is quantitative
  rather than a judgement call. The threads are visibly parallel, which
  also rules out 1/8 NPT.
- The **M6 end is NOT measured.** It is the catalogue designation plus
  photo proportions. `M6×1.0` in particular is assumed, not established.
- The **orientation** is the operator's account of the part in hand, not
  an independent check.

So the thermoblock presents an **M6 female gauge port**. M6 is also
reported to be the E61 group-head pressure-port thread, which would make
E61 gauge adapters the right category to shop in — but that is **vendor
listing grade** (a sensor maker's compatibility page naming an M6 adapter
for Lelit/Rocket/Profitec/ECM), not a spec, and **the pitch is not stated
anywhere**. Do not buy an adapter on it without checking pitch and seat
form, which is the same class of source that has twice misled this
project.

**An inference that was wrong, recorded so nobody re-derives it:** that
the orientation must be the reverse (thermoblock 1/8, gauge M6), on the
grounds that M6 is the natural size for a compression nut on 2 mm copper
and 1/8 the natural size for a port in a block. The reasoning is plausible
and the conclusion is false. The catalogue name lists the two threads in
the same order as the two words before them, and reading it literally was
right.

**THE GAUGE END CANNOT BE TEED.** Observed 2026-10-05: the gauge's rear
stem is a brass male thread with a **plain steel hex nut clamping it to
the bracket**, and the copper capillary is **soldered at the base of that
thread**. The thread holds the gauge up; pressure arrives through the
capillary. A nutted-to-a-bracket thread is structural, not hydraulic.
Anything that wants pressure has to go to `I.4234`.

**The circuit is rigid stainless tube with compression fittings, not
screwed pipe.** Every joint photographed is a compression nut swaging an
olive onto rigid tube, threaded onto an elbow or tee body. Two
consequences: opening a joint means a fresh olive and possibly a re-cut
tube end, and **there are no spare ports** — every port in the areas
photographed is occupied.

The capillary is **~10 cm** (operator measurement) and ends in a
compression nut with a **separate conical olive**, confirmed by Ascaso's
own product photo of `I.4220`. So the joint is re-makeable and the gauge
assembly comes off intact — keep it, with its olive and the M6 seal, and
the machine goes back to stock.

**Still unknown, all of it cheap to settle next time the machine is open:**
whether the M6 is ×1.0; what seals the M6 at the block (there will be a
washer — note what it is before it is lost, because an M6 *parallel*
thread does not seal on its threads and PTFE tape is not a substitute);
whether `I.4234`'s G1/8 male has a cone seat for the olive, which decides
whether a plain G1/8 female can seal against it; where `I.3934` physically
sits; and the bezel hole diameter.
