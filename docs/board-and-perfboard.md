# The board and the perfboard

> **Normative on:** the ESP32-S3-DevKitC-1 clone's physical pinout as read
> off the board in hand, the evidence behind each GPIO assignment, and every
> mechanical dimension the perfboard layout depends on.
>
> **CLAUDE.md carries** the live conclusions in its GPIO-budget section: the
> board check is done, GPIO48 is the LED so GPIO38 is free, Espressif's
> published v1.1 layout is correct for this clone, the seven taps are
> contiguous at positions 4–10 with 3V3 at 1 and GND at 22, 5Vin-beside-GND
> is a solder hazard, no tap draws from 3V3, 2.54 mm pitch, 10-pitch row
> spacing, an 11 × 22 pin footprint in a ~25-row body, the board is
> 10 × 10 FR-4 and count the holes. Those are
> what a reader acts on; the enumeration and the retraction history below are
> what they rest on.
>
> **Changing a number here means grepping CLAUDE.md for it in the same
> commit.** The GPIO assignments themselves are additionally pinned by
> `firmware/src/main.cpp` (0.9.0 names all seven), which wins over both.

---

**The board check is DONE — 2026-10-12 — and it was already done on
2026-10-08 without anyone noticing.** This paragraph carried "they have
**not** been checked against the clone DevKitC-1's silkscreen; do that
before the perfboard is drilled" for four days while the bench section 200
lines away recorded the evidence that closed it. Same stale-line defect this
file documents elsewhere.

What closes it is **functional evidence combined with the visual check** —
neither alone. The operator chose each header pin by reading its label, so if
the pin marked `16` were not die GPIO16, the firmware (which names GPIO16
steam) would have been polling an unconnected pad and lever B down could not
have logged `steam`. It logged.

**Do NOT call the functional chain "a higher grade" than the silkscreen read,
as a first draft of this paragraph did.** By this repo's own standard — see
the lever A/B grades in `docs/machine-switches-j2.md` — a silkscreen read is
a **one-link**
observation of exactly the claim being made (what is printed beside this
pin). The functional chain is longer: operator picked by label (recalled, not
recorded) → wire landed → firmware name → log line. It exercises more, and
that makes it a *longer* evidential chain, not a better-graded one. The
conclusion is sound because the two **combine**; the grade language was
inflated.

| GPIO | What confirmed it, 2026-10-08/09 |
|---|---|
| 4, 5 | the bus **decoded frames**. A swapped pair gives `frames=0` — the signature CLAUDE.md documents — so this fixes *which is which*, not merely that both are right |
| 6 | **736 counts** across shot 2, and `selftest` 500/500 at 100 Hz |
| 7, 15 | **lever A**, logged as "correct GPIO mapping" — same full chain as 16/17. *(This row quoted "taps of 345-480 ms" as evidence until 2026-10-19; that is one session's presses, superseded above. The GPIO conclusion is unaffected — it never depended on the width.)* |
| 16, 17 | lever B down logged `steam`, up logged `water` |
| 8 | `selftest` into GPIO6 — **not independent, see below** |

**Two honest limits on that table.**

**Row 8 is not independent evidence.** `selftest` drives one die pin into
another through a physical jumper; if the pins labelled 6 and 8 were swapped,
the jumper still joins die 6 to die 8 and 500/500 still passes. Row 8 is
non-circular only given row 6. The rows are a mutually-supporting system, not
six parallel confirmations.

**Rows 7/15 were briefly "corrected" to jumper touches, and that was wrong.**
A 2026-10-12 review read the written bench procedure — *"jumper each of
GPIO7/15/16/17 to 3V3 and back"* — and inferred the taps were header touches.
I applied that as a correction. **The operator confirms they were lever
presses on the machine.** The procedure records what was planned; the levers
are what happened. Restored, and the divergence is now recorded in
`docs/machine-switches-j2.md`, with the pulse-width record, so the procedure
is not cited this way again.

So GPIO7/15 were exercised the same way as 16/17. **But the record is one
link short of saying so**, and `docs/machine-switches-j2.md` spends a
paragraph insisting on exactly that distinction: lever B has its own row quoting what was logged
(`B up logged water, B down logged steam`), while lever A's evidence is the
summary row "All four switches | correct GPIO mapping" — a conclusion, not an
observation. **CLOSED 2026-10-19 — ten of them, verbatim from the live tap:**

    # switch 1cup 220 ms    # switch 1cup 245 ms
    # switch 1cup 220 ms    # switch 1cup 285 ms
    # switch 1cup 295 ms    # switch 1cup 230 ms
    # switch 1cup 235 ms    # switch 1cup 275 ms
    # switch 1cup 240 ms    # switch 1cup 195 ms

Lever A up, logged as `1cup`, on GPIO7. Row 7/15's evidence is now the same
kind as lever B's — a quoted log line rather than a summary conclusion.

**The visual check's job**: the functional chain alone cannot distinguish
wiring by *label* from wiring by *position*. The operator confirmed the
silkscreen order on 2026-10-12, and since each GPIO exists at exactly one
header position, the two together identify every pin both ways round.

**The reference used for that check was Espressif's published layout, and it
holds up.** A draft here said the layout had been "overturned the same day on
GND placement" and graded it "a weak reference *for this clone*". Wrong on
both counts — the full 44-position enumeration **agrees with the published
table everywhere**, so the comparison is sound and this check is better
supported than that draft claimed.

**The seven are contiguous on one header, which the perfboard layout should
exploit.** Confirmed on the board in hand, 2026-10-12:

**FULL PINOUT — READ OFF THE BOARD 2026-10-12 AND CONFIRMED AGAINST
ESPRESSIFS PUBLISHED v1.1 TABLE.** Both columns enumerated end to end, 22
positions each, position 1 at the 3V3 end (breadboard row 42) and position 22
at the far end (row 63). **The two agree at all 44 positions.** A draft of
this heading said the board read "replaces Espressif's published layout —
use nothing else"; see the retraction below.

| Pos | Signal column | | Opposite column |
|---|---|---|---|
| 1 | **3V3** | | **GND** |
| 2 | 3V3 | | GPIO43 / TX |
| 3 | RST | | GPIO44 / RX |
| **4** | **GPIO4** — `ESD4` clock | | GPIO1 |
| **5** | **GPIO5** — `ESD1` data | | GPIO2 |
| **6** | **GPIO6** — flow `#` | | GPIO42 |
| **7** | **GPIO7** — 1Cup | | GPIO41 |
| **8** | **GPIO15** — 2Cup | | GPIO40 |
| **9** | **GPIO16** — Steam | | GPIO39 |
| **10** | **GPIO17** — Hot Water | | GPIO38 |
| 11 | GPIO18 | | GPIO37 ‡ |
| **12** | **GPIO8** — selftest | | GPIO36 ‡ |
| 13 | GPIO3 † | | GPIO35 ‡ |
| 14 | GPIO46 † | | GPIO0 † |
| 15 | GPIO9 | | GPIO45 † |
| 16 | GPIO10 | | **GPIO48 — onboard RGB LED** ◆ |
| 17 | GPIO11 | | GPIO47 |
| 18 | GPIO12 | | GPIO21 |
| 19 | GPIO13 | | GPIO20 † |
| 20 | GPIO14 | | GPIO19 † |
| 21 | **5Vin** | | **GND** |
| 22 | **GND** | | **GND** |

† strapping or native-USB, avoid. ‡ **GPIO35-37 are octal-PSRAM internal on
this N16R8 module** — physically present on the header and unusable; do not
even probe them.

◆ **THE ONBOARD RGB LED IS ON GPIO48 — SETTLED 2026-10-15 BY A TWO-POINT
TEST. So GPIO38 is FREE.** Espressif put it on **GPIO48 on v1.0 and GPIO38 on
v1.1** and both revisions are sold, so this had to be measured. Open since
2026-10-12.

**The two points, each anchored to a serial reading taken at the same
moment.** Setup: board on USB, **no machine connections**, and — the part a
draft of this paragraph left out — **the yield Bookoo bound and awake**. Row 1
is impossible without it, so "nothing else connected" read literally
reproduces a dark LED in both rows, which is the very trap this entry warns
the old one-second test fell into. Row 2 is produced by switching that scale
off:

| `status` | LED | what `updateLed()` writes |
|---|---|---|
| `yield: state=3` (CONNECTED), `detector=ARMED` | **dim green** | `mode 2` → `neopixelWrite(…, 0, 14, 0)` |
| `yield: state=4` (LOST) | **off** | `mode 0` → `neopixelWrite(…, 0, 0, 0)` |

**ONE POINT WOULD NOT HAVE BEEN ENOUGH, and a draft of this entry published
it as though it were.** That draft argued the *colour* proved the firmware
was driving the LED, because green is specific to ARMED. It does not: a
**green power indicator** beside a dark RGB LED produces the first row
exactly, and `docs/phase-0-plan.md` was about to free GPIO38 on the strength
of it. A power LED cannot produce the second row. Only the pair does.

**How the pin resolves — and a draft got the mechanism wrong while reaching
the right answer.** `RGB_BUILTIN` is **not** 48. The `esp32s3` variant (which
`esp32-s3-devkitc-1.json` selects) defines it as
`SOC_GPIO_PIN_COUNT + PIN_NEOPIXEL` = **49 + 48 = 97**, a sentinel;
`esp32-hal-rgb-led.c:12` subtracts `SOC_GPIO_PIN_COUNT` back off to reach
GPIO48.

**A draft then drew a false consequence from those two true facts**, warning
that *"`digitalWrite(RGB_BUILTIN, …)` addresses pin 97 silently and would not
do what it looks like it does."* **It does what it looks like.**
`cores/esp32/esp32-hal-gpio.c:138` special-cases `pin == RGB_BUILTIN` inside
`__digitalWrite` and routes it to `neopixelWrite(RGB_BUILTIN, c, c, c)` with
`c = RGB_BRIGHTNESS` for any non-zero value — so it lights the LED white.
`__pinMode` carries the same special case at line 95. The sentinel is handled
at every call site that matters. Mechanism right, consequence invented: the
call site was never read, which is this repo's most-logged error and was
committed three times in one session.

**This fixes the LED net only. It does NOT make the board "v1.0".** Every
other position in the table above agrees with Espressif's published v1.1
layout, and a clone can mix revisions. Claim the pin, not the revision —
which is also why the table cell above carries ◆ rather than standing as part
of the board read: **an LED net is not silkscreened and is not in the v1.1
table either** (that document says 38), so that one cell comes from this
measurement and from no other source anywhere in this repo.

**THE ONE-SECOND TEST THIS ENTRY USED TO GIVE WAS WRONG, and would have
produced the opposite answer.** It read: *"if the onboard status LED lights on
the current firmware, it is GPIO48; if it has always been dark, it is
GPIO38."* The second half is false. `updateLed()` sets `mode = 0` whenever the
**yield** scale is not connected, so a board sitting on a bench with no scale
awake is dark on *either* revision. Run as written it concludes GPIO38 and
sends someone to invert a free-GPIO list that was already right. **A dark LED
is informative only alongside `status`** — which is exactly what made the
second row above evidence rather than noise.

**A RETRACTION OF A RETRACTION, recorded because the second error is the one
this repo keeps making.** A draft of this entry deleted the old sentence
*"CLAUDE.md and `main.cpp:377` both assert 48 flatly"*, calling it "a line
number cited without being read". **That accusation was false and the original
was correct.** `git show bb3ed26:firmware/src/main.cpp | sed -n '377p'` — bb3ed26
being the commit that wrote the citation — returns exactly
`// output full stop — updateLed() drives the onboard RGB LED on GPIO48, which`.
The line was read, was right, and has since drifted repeatedly — 445, then
472, and moving again within the same day's commits — while still asserting
GPIO48. The draft checked the *current* file
instead of the file at the commit that made the claim — the same
read-a-later-version-and-call-it-history error logged against `pendingCount()`
earlier the same day. **When a line number here looks wrong, check it at the
commit that wrote it before retracting anything.**

**AND DO NOT RE-CITE IT BY LINE, WHICH A DRAFT DID — it said "Cite
`main.cpp:445`" and that was stale within the week.** This paragraph has now
carried three numbers for one comment (377, 445, 472), each correct when
written. It is the `updateLed()` note in the GPIO8 block; `grep` finds it in
one step and cannot go stale.

*(Position 19 carried a "◦" flag until 2026-10-12: the operator's transcription
read "12 12" at positions 18-19 and GPIO13 was inferred. **Espressif's table
gives position 19 = GPIO13**, so the inference is confirmed — by the very
document a draft of this entry told the reader to stop consulting.)*

**Two things this corrects about earlier working notes**, both from the
published layout rather than the board: GPIO19 and GPIO20 are on the
**opposite** column, not at signal-column positions 13-14 (which carry GPIO3
and GPIO46), and the signal column ends **5Vin, GND** rather than running
GPIO14 to the last position. The repo's own table only ever claimed positions
1-12, which are correct — the error was in the fuller recollection used to
reason about free pins.

**GND is on the signal column — and the published layout said so all along.**
A draft of this entry claimed the board read "overturns the published layout"
and called it "a board reading beating a document-grade source". **It did not
beat it; it reproduced it.** Espressif's own v1.1 user guide gives J1-22 = `G`.
The enumeration above agrees with that document at all 44 positions.

**NO TAP DRAWS FROM 3V3.** The position table calls positions 1-2 "the
divider supply" and that is wrong — every divider here is fed from a
**machine** signal and returns to GND (J2: 150 K from the direction line;
J4: 100 K from `#`; J5: 8.2 K at the T-piece). 3V3 carries zero *tap* current.

**The rail is still needed, and a draft of this paragraph said it was not.**
The perfboard carries a populated 74HC221, which needs VCC and GND, and the
photoMOS LED supply runs from our 3.3 V logic. "A rail nothing connects to"
was true of the taps and false of the board.

**ROW-TO-ROW SPACING: 10 pitches — 1.0 in, 25.4 mm. MEASURED 2026-10-12** by
seating the devkit in an 830-point breadboard and reading the columns: one
header in **A**, the other in **I**. A→E is 4 pitches, the 0.3 in channel is
3, F→I is 3. **Column J stays free; nothing is free on the A side.**

That closes the last mechanical unknown. It had been recorded as open on
2026-10-12, deleted the same day by a paragraph rewrite, and covered over by a
claim that nothing mechanical remained — net information loss by rewrite,
restored, and now answered.

**`docs/shopping-list.md` predicted "no rows beside it" and that is REFUTED** —
one row (J) is free. The line is corrected there. It had been used twice to
argue the spacing was already partly constrained.

**The header is 22 pins, spanning breadboard rows 42 to 63** — one pin per
row, 3V3 at row 42. **Call this a consistency check, not a third independent
confirmation**, which a draft did: a header that is not 2.54 mm cannot seat in
the breadboard at all, so the row count is undefined in the counterfactual.
It is the *mating* argument restated from the same seating, on the same day.
Worth having; not a separate observation.

*(A first reading of this gave rows 42–64 and a 23-pin header, which would
have made the board depart from the published 2 × 22 layout. The operator
corrected it to 63 within minutes. Recorded only because the 23-pin reading
briefly reached a commit message.)*

**FOUR GND pins, and the census is now CLOSED by full enumeration** rather
than by a fourth count-by-eye. It had moved twice (three, then four) from
successive closer looks, which is why a review flagged it as a visual scan
standing as the premise of a layout claim. From the table above:

| | Position | Neighbour |
|---|---|---|
| **signal column** | **22** | next to 5Vin at 21 |
| opposite column | 1 | opposite 3V3 |
| opposite column | 21 | next to GPIO19 at 20 |
| opposite column | 22 | the far corner |

**The load-bearing claim SURVIVES.** The signal column runs **3V3 at position
1, GND at position 22, all seven taps between** — so no ground return has to
cross the board. That was in four documents on the strength of a visual scan,
and an intermediate description of the GNDs ("one opposite 3V3, two at the
other end of that row") read as though all of them were on the *opposite*
column, which would have refuted it. The enumeration settles it: there is a
GND at the far end of the taps' own column.

**5Vin is at signal-column position 21.** It is *our* 5 V rail, not the
machine's — do not confuse it with the J4 `+` net, which is read-only. (A
draft called this "not previously recorded anywhere"; it is in Espressif's
published table as J1-21.)

**It sits directly beside the GND at 22, and that is a solder hazard, not a
convenience.** On hand-wired perfboard with a ground bus down that column, one
bridge between 21 and 22 shorts the devkit's USB 5 V to ground. Leave the gap
or route the bus away.

**THE PUBLISHED PINOUT IS CORROBORATED, NOT REPLACED — and a draft of this
entry said the opposite.** It instructed the reader to *"stop citing
Espressif's published pinout for this board… Neither direction is reliable
here."* That rested on two supposed departures and **both were mine**:

| Alleged departure | Actually |
|---|---|
| a 23-pin header (rows 42-64) | a misread of row 63, retracted in the same commit |
| GND on the signal column | **J1-22 is `G` in Espressif's table.** The "departure" was my recollection of the layout, which put GPIO19/20 at J1 positions 13-14 where the real document has 3/46 |

The published layout therefore has a **clean record** against this clone, and
the 2026-10-12 enumeration **strengthens** the pin map by agreeing with it.
The lesson is the inverse of the one that got written down and pushed: **the
unreliable source was my memory of the document, not the document.**

Checked against `docs.espressif.com/projects/esp-dev-kits/` →
`esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html`, 2026-10-12.

The signal column runs **3V3 at position 1, GND at position 22, all seven taps
between**, so **no ground return has to cross the board** — the claim that has
survived every revision above.

**But "between" does not mean "adjacent".** The taps are at positions 4-10 and
that column's GND is at 22, so each divider's ground leg runs 12-18 pitches
(~30-46 mm). The nearest ground to the taps is the *opposite* column's position
1. The no-crossing claim is literally true; do not read it as a short return.

**COUNTING TRAP: breadboard columns are not perfboard holes, and the
difference is the channel.** A to I looks like 9 columns and *is* 9 columns —
but it is **10 pitches**, because the single visual step from E to F is the
0.3 in channel — **worth 3 pitches, not 1** (it spans 3 pitches and contains
2 missing hole positions). Perfboard has
no channel, so the same devkit lands on columns 0 and 10 there:

| | columns you can see | pitches |
|---|---|---|
| breadboard, A to I | 9 | **10** |
| perfboard, continuous | **11 inclusive** | **10** |

Quote the **pitch count**, never the column count, when carrying a dimension
from one board to the other.

**FOOTPRINT ON PERFBOARD: 11 columns × 22 rows**, with 9 free columns running
underneath the devkit between the two pin rows. That underneath space is real
estate — the dividers can live there if the devkit is socketed high enough,
which is the reason `docs/shopping-list.md` specifies **female** headers.

**Size the board from the hole count, not the centimetre label**, and do the
accounting properly — **a draft of this paragraph got it backwards**:

| | holes | devkit pin field | free beside it | free underneath |
|---|---|---|---|---|
| 5 × 7 cm | 19 × 27 = 513 | 11 × 22 | **8 × 27 = 216** | 9 × 22 = 198 |
| 7 × 9 cm — *nominal, and not what is sold* | 27 × 35 = 945 | 11 × 22 | **16 × 35 = 560** | 9 × 22 = 198 |
| 7 × 9 cm — **as actually sold** | 24 × 30 = 720 | 11 × 22 | **13 × 30 = 390** | 9 × 22 = 198 |
| **10 × 10 cm FR-4 — BOUGHT 2026-10-28** | **35 × 37 = 1295** | 11 × 22 | **24 × 37 = 888** | 9 × 22 = 198 |

That draft said 5 × 7 "does not fit" because "the devkit eats 22 of the 27
rows, leaving five" — which treats the devkit's **11-column** footprint as if
it spanned the full 19-column width. It does not. And it quoted 7 × 9 as
"16 × 13 clear" — a corner rectangle of 208 holes, **fewer than the 216 a
5 × 7 offers beside the devkit**. By its own metric the draft argued against
its own conclusion.

**7 × 9 WAS the right buy on these numbers, and it is SUPERSEDED BY 10 × 10
FR-4 as of 2026-10-28** — `docs/shopping-list.md` §5 is normative on the
replacement. The reasoning here was *"soldering room rather than
necessity: 560 contiguous holes against 216"*, and **that 560 assumed a
nominal 27 × 35**. Commodity 7 × 9 boards are **24 × 30 = 720**, giving
**390 beside the devkit** — a 30 % cut in the only quantity the choice
rested on, and below the "often ~24 × 34" hedge three paragraphs down. The
board bought is **35 × 37 = 1295, ~888 beside**, and the argument that
settles it is `docs/phase-0-plan.md`'s requirement that pos 1 keep
*"maximum spacing from the four direction lines"* on the board: ~61 mm
there against ~33 mm on 7 × 9.

But "5 × 7 does not fit" is still not what the numbers say, and that
correction is the reason this table exists.

**Footprint is the pin field, not the outline.** 11 × 22 holes is 25.4 ×
53.3 mm of *pins*; the PCB overhangs at both ends (module/antenna one end,
USB and buttons the other) by roughly 5 mm each, so the body covers ~25 rows.
Budget rows against the outline.

**Count the holes on the board you actually receive.** Commodity "7 × 9"
protoboards are often ~24 × 34 rather than 27 × 35, and "5 × 7" ones 18 × 24 —
and the hedge should be applied to both, not just to the one being rejected.

**Pitch is 2.54 mm (0.1 in) — and the proof is that the board has been in a
BREADBOARD since 0a.** A caliper reading of **1.9 mm** was taken 2026-10-12
and briefly recorded as possibly the pitch. It is the **gap**: 2.54 mm pitch
less a 0.635 mm square post (0.025 in) is **1.905 mm**. *(An earlier draft
rounded the post to 0.64 first and then called the 1.90 agreement "to the
hundredth" — a precision the inputs do not carry.)*

The arithmetic is not what settles it. **Breadboards are 2.54 mm and the
devkit seats in one — observed 2026-10-12.** A 1.9 mm part cannot mate with a
2.54 mm socket strip, so the pitch is settled by mating, which is this repo's
own maxim.

**Mind the provenance, because a pushed commit got it wrong.** That commit
said the devkit "has been seated in a 2.54 mm breadboard **through every bench
session**". It has not: through the bench work the ESP was **off the
breadboard, connected by wires**, and only the divider junctions were in it —
which `docs/shopping-list.md` had already recommended. The evidence is the
2026-10-12 seating and nothing earlier. Conclusion unchanged, evidence
corrected: an assumed history is not an observation, which is the same error
this repo logged twice on 2026-10-12 alone.

Recorded because the trap is live for anyone measuring a header: **calipers
laid between two pins read the gap, and the gap on 0.1 in hardware is a
plausible-looking 1.9 mm.** If a span ever does need checking, measure across
**ten pitches** rather than one gap, which multiplies the difference tenfold.
**Measure outer edge of pin 1 to outer edge of pin 11**, not centre to centre:
calipers land on pin faces and cannot find centres, so a centre-to-centre
instruction reproduces the very jaw-placement ambiguity being warned about.

| | 0.1 in pitch | a true 1.9 mm pitch |
|---|---|---|
| centre to centre (not measurable) | 25.40 mm | 19.00 mm |
| **outer edge to outer edge** | **26.04 mm** | **19.64 mm** |

*(10 × pitch + one 0.635 mm post. An earlier draft deleted the outer-edge row,
leaving only the instruction that calipers cannot execute.)*
