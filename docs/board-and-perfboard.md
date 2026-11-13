# The board and the perfboard

> **Normative on:** the ESP32-S3-DevKitC-1 clone's physical pinout as read
> off the board in hand, the evidence behind each GPIO assignment, and every
> mechanical dimension the perfboard layout depends on.
>
> **CLAUDE.md carries** the live conclusions in its GPIO-budget section: the
> board check is done, GPIO48 is the LED so GPIO38 is not (it is the arm gate
> since 2026-10-29 — the proposed gate *enable*, the gate '221's `R̅`, since 2026-10-30), Espressif's
> published v1.1 layout is correct for this clone, the seven taps are
> contiguous at positions 4–10 with 3V3 at 1 and GND at 22, 5Vin-beside-GND
> is a solder hazard, no tap draws from 3V3, 2.54 mm pitch, 10-pitch row
> spacing, an 11 × 22 pin footprint in a ~25-row body, the board is
> 10 × 10 FR-4, 35 × 38 COUNTED on the delivered board 2026-11-03. Those are
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
| **4** | **GPIO4** — `ESD4` clock | | **GPIO1** — `Q` readback |
| **5** | **GPIO5** — `ESD1` data | | **GPIO2** — `Q` readback |
| **6** | **GPIO6** — flow `#` | | **GPIO42** — trigger, 1Cup photoMOS 1 |
| **7** | **GPIO7** — 1Cup | | **GPIO41** — trigger, 1Cup photoMOS 2 |
| **8** | **GPIO15** — 2Cup | | **GPIO40** — trigger, 2Cup photoMOS 1 |
| **9** | **GPIO16** — Steam | | **GPIO39** — trigger, 2Cup photoMOS 2 |
| **10** | **GPIO17** — Hot Water | | **GPIO38** — gate enable, proposed (gate '221 `R̅`) |
| 11 | GPIO18 | | GPIO37 ‡ |
| **12** | **GPIO8** — selftest | | GPIO36 ‡ |
| 13 | GPIO3 † | | GPIO35 ‡ |
| 14 | GPIO46 † | | GPIO0 † |
| 15 | **GPIO9** — gate `Q` readback, proposed | | GPIO45 † |
| 16 | **GPIO10** — gate trigger, proposed | | **GPIO48 — onboard RGB LED** ◆ |
| 17 | GPIO11 | | **GPIO47** — `Q` readback, 2Cup photoMOS 1 |
| 18 | GPIO12 | | **GPIO21** — `Q` readback, 2Cup photoMOS 2 |
| 19 | GPIO13 | | GPIO20 † |
| 20 | GPIO14 | | GPIO19 † |
| 21 | **5Vin** | | **GND** |
| 22 | **GND** | | **GND** |

† strapping or native-USB, avoid. ‡ **GPIO35-37 are octal-PSRAM internal on
this N16R8 module** — physically present on the header and unusable; do not
even probe them. GPIO33 and 34 are internal too and simply not broken out,
so the full unusable set is **33–37**, as CLAUDE.md states.

38–42 are the drive allocation, decided 2026-10-29 — 38 became the
**gate enable** on 2026-10-30, proposed, footprint reserved — and 1–2 the first two `Q` readbacks,
decided 2026-10-30; since the drive layout (2026-11-04) the gate trigger is
GPIO10 and the last three readbacks are GPIO47, 21 and 9 (CLAUDE.md's DRIVE
table; "Drive-side layout" below). *("chosen at layout" until then.)*
None is named in `main.cpp` yet.

◆ **THE ONBOARD RGB LED IS ON GPIO48 — SETTLED 2026-10-15 BY A TWO-POINT
TEST. So GPIO38 is NOT the LED** *(this said "is FREE" until 2026-10-29 — it
was, and CLAUDE.md's drive table has since spent it on the '221 arm gate —
the proposed gate *enable* since 2026-10-30, not the drive channels' reset)*.
Espressif put it on **GPIO48 on v1.0 and GPIO38 on
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
**machine** signal and returns to GND (J2: 150 K from the direction line; J4: 100 K from `#`; J5: 8.2 K from
each bus line — all on the board for the build, J2's from 2026-11-03,
J4's and J5's from 2026-11-05; *"J5: 8.2 K at the T-piece" until then*). 3V3 carries zero *tap* current.

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
| 5 × 7 cm — *nominal* | 19 × 27 = 513 | 11 × 22 | **8 × 27 = 216** | 9 × 22 = 198 |
| 5 × 7 cm — **as the hedge below expects** | 18 × 24 = 432 | 11 × 22 | **7 × 24 = 168** | 9 × 22 = 198 |
| 7 × 9 cm — *nominal, and not what is sold* | 27 × 35 = 945 | 11 × 22 | **16 × 35 = 560** | 9 × 22 = 198 |
| 7 × 9 cm — **as actually sold** | 24 × 30 = 720 | 11 × 22 | **13 × 30 = 390** | 9 × 22 = 198 |
| **10 × 10 cm FR-4 — RECEIVED, counted 2026-11-03** <!-- fact:perfboard-grid 35 × 38 = 1330 --> | **35 × 38 = 1330** *(35 × 37 = 1295 on the listing's photo, 2026-10-29)* | 11 × 22 | **24 × 38 = 912** | 9 × 22 = 198 |

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
rested on, and below the "often ~24 × 34" hedge under *"Count the holes on
the board you actually receive"*. The
board bought is **35 × 38 = 1330** as counted on delivery, **912 beside**
in the orientation this table uses *(35 × 37 = 1295, 888 beside, from the
listing photo until 2026-11-03)*, and the argument that
settles it is `docs/phase-0-plan.md`'s requirement that pos 1 keep
*"maximum spacing from the four direction lines"* on the board:
**58.4 mm there against 30.5 mm on 7 × 9**.

**Those two figures were ~61 mm and ~33 mm until 2026-10-29, and they broke
the rule stated just above the hole-count table they come from** — *"Quote
the pitch count, never the column count"*. The free field
beside the devkit is **24 columns** on 10 × 10 and **13** on 7 × 9, and a
24-column field spans **23** pitches, not 24: 23 × 2.54 = 58.42 mm, and
12 × 2.54 = 30.48 mm. Both were overstated by exactly one pitch — the
column count quoted as a dimension, which *"Quote the pitch count, never
the column count"* exists to prevent. The *"Footprint is the pin field, not
the outline"* paragraph below gets the same conversion right for the devkit
(11 × 22 pins → 25.4 × 53.3 mm), so the convention was not unknown here,
merely not applied.

**All three pointers in this section were positional — "three paragraphs
down", "nine lines above", "four below" — and all three were wrong on
2026-10-29: the first was correct when written and was broken by the
paragraphs inserted above it; the other two were wrong from the moment they
were written. Replaced by quoting the lead-in phrases they point at.** `CLAUDE.md` already records that a line number in a document
is wrong by the time the document is saved; the same is true of a paragraph
count, and this file managed to prove it within one commit.

**The conclusion is unchanged**, which is why this is a correction and not
a retraction: 58.4 / 30.5 is 1.9×, against 1.85× for the wrong pair. 10 × 10
still buys roughly twice the separation. **And both numbers are spans of a
hole field, not a designed placement** — no layout has been drawn, so they
bound what the requirement *could* be given, not what it will get.

**AND 1295 = 36² − 1, WHICH IS WHY THE FACTORISATION IS UNIQUE — it is not
corroboration.** The board is sold as **square** 10 × 10, and **36 × 36 =
1296** is one hole away on a board sold square — the competing reading,
favoured by nothing here, and it needs the vendor's count to be off by one;
a 35 × 37 field on a square board implies edge margins differing by ~2.5 mm
between the axes. If the grid is 36 × 36 then free-beside is **25 × 36 = 900**, not
888, and the span is **24 pitches = 60.96 mm** — which would put back the
~61 mm corrected above. Nothing here distinguishes them, so **counting the
delivered board settles two questions, not one.**

**COUNTED ON THE DELIVERED BOARD 2026-11-03: 35 × 38 = 1330.** 35 along one
axis, A–Z then A–L (26 + 12 = 38) along the other. The photo count below was
one letter short and the vendor's 1295 is wrong for this board. With the
devkit's long side along the 38-hole axis, the free field is still 24
columns, **58.4 mm**, and free-beside becomes **24 × 38 = 912**, not 888.
**Turned 90°** — long side along the 35-hole axis, which its ~25-row body
fits — it is **27 columns = 26 pitches = 66.0 mm, 27 × 35 = 945 beside**,
and that is the most spacing pos 1 can get. Every figure in this file
before 2026-11-03 assumed the first orientation without saying so; the
choice is the layout's, and it was made the same day: the 66.0 mm
orientation, see "Sense-side layout" below. The two questions
"counting the delivered board settles" are settled: not 36 × 36, and not
35 × 37 either.

**PHOTO-COUNTED 2026-10-29: 35 × 37** *(superseded 2026-11-03 by the count
above)*. The operator counted 35 along one
axis and the letters A–Z then A–K — 26 + 11 = 37 — along the other, **on the
listing's product photo; the pack had not arrived.** That is a grade of its
own: stronger than the vendor's bare number, because the photo and the
number now agree to the hole and 36 × 36 would need both wrong; weaker than
a board in hand, because a listing photo can be a stock image shared across
sizes. On that evidence 36 × 36 is retired, 888 and 58.4 mm stand, and the
~61 mm stays corrected — and "counting the delivered board settles two
questions" above is still owed. *(This paragraph said "COUNTED" without the
"photo" for part of that day.)*

But "5 × 7 does not fit" is still not what the numbers say, and that
correction is the reason this table exists.

**Grades, moved here from CLAUDE.md on 2026-10-29, which keeps only the
conclusion.** 912 and 58.4 mm are **COUNTED** grade since 2026-11-03 (888
and 58.4 mm were **PHOTO-COUNTED** grade from the 35 × 37
count above until then *(INFERRED for the hours between the move and the count, and
"COUNTED" for the hours after it until the photo was named as the source)*;
30.5 mm is
**LISTING**, since 24 × 30 is stated outright and no 7 × 9 was ever
received — so the 1.9× ratio is a count against a vendor figure. The listing
side is the same evidence class as the "XH 2.54mm Vertical" listing that was
wrong about the parts it described: different vendor and order, shared
class. And the one hole count ever read off a
listing (24 × 30) disagreed with this repo's own *derived* nominal (27 × 35),
not with the vendor's "7 × 9", which a 24 × 30 grid fits; 18 × 24 for a
5 × 7 is a generic hedge, not a listing. *(The block that first carried this
restated the 36² − 1 reading, the 58.4 / 30.5 figures and the ~61 / ~33
retraction a second time in this file, 70 lines under the first; collapsed
the same day to the three sentences the file did not already have.)*

**Footprint is the pin field, not the outline.** 11 × 22 holes is 25.4 ×
53.3 mm of *pins*; the PCB overhangs at both ends (module/antenna one end,
USB and buttons the other) by roughly 5 mm each, so the body covers ~25 rows.
Budget rows against the outline.

**Count the holes on the board you actually receive — done for the
10 × 10 on 2026-11-03: 35 × 38 = 1330**, one row more than the photo count
*(until then "still owed … photo-counted, not counted")*. *(This said "done … inferred to counted" for part of that day.)*
Commodity "7 × 9"
protoboards are often ~24 × 34 rather than 27 × 35, and "5 × 7" ones 18 × 24 —
and the hedge should be applied to both, not just to the one being rejected.
**It was not, until 2026-10-29** — every table above carried 7 × 9 both
nominal and as-sold while leaving 5 × 7 nominal, and **216 is the baseline
the whole argument compares against** (*"560 contiguous holes against 216"*,
*"fewer than the 216 a 5 × 7 offers"*). Both rows now appear. The conclusion
is unchanged and strengthened: against 168 the rejected board is worse still
— and 168 flatters it, because a 24-row board is shorter than the devkit's
~25-row BODY, so on the as-sold 5 × 7 the devkit overhangs the board and
"budget against the outline" has nothing left to budget.

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

## Sense-side layout — DRAWN 2026-11-03

**The first hole-level layout in this repo, for the sense side; the drive
side follows in "Drive-side layout".** *("the drive side is not drawn yet"
until 2026-11-04.)* Coordinates are the delivered board's own marks:
**letters A–Z then A–L again across (38), numbers 1–35 down (35)**. The
board prints the second run without primes; **here A′–L′ means the second
run**, and a bare letter always means the first. Components
on top, every link insulated wire on the underside unless a row says
"bridge".

**Checked on the parts before drawing:**

- **Devkit orientation: long side along the 35-hole axis**, the 66.0 mm
  orientation, so pos 1 gets the larger free field. **Pin 1 (3V3) is at the
  module/antenna end** (operator, on the devkit in hand), so the module faces
  the top edge and USB the bottom.
- **Under-devkit clearance is 10 mm** with the devkit in its female headers,
  measured by the operator. A flat ¼ W resistor needs ~2.5 mm, so the sense
  shunts go **under** the devkit.
- **Two of the 220 K lie in adjacent rows without touching**, so the shunts
  sit in consecutive rows at 2.54 mm.

**Devkit.** Signal column **A9–A30** (pos 1 = A9 … pos 22 = A30), opposite
column **K9–K30**. The taps sit at A12 GPIO4 (`ESD4`), A13 GPIO5 (`ESD1`),
A14 GPIO6 (flow `#`), A15 GPIO7 (1Cup), A16 GPIO15 (2Cup), A17 GPIO16
(Steam), A18 GPIO17 (Water); GPIO8 is A20. **A29 is 5Vin — nothing is
connected at or beside it.**

**Harness headers, top edge, side-entry XH, plugs entering from the top:**

| Header | Pins | Notes |
|---|---|---|
| J5, 4-pin | B3 `VSS` · C3 `ESD1` · D3 `ESD4` · E3 *empty* | **`Disp-P4` is not branched** (2026-11-05): it passes through the T-piece only, and the plug's cavity 4 is left **without a contact** — kept 4-pin so it cannot go into the 3-pin J4 header. E3 connects to nothing. **Both signals arrive RAW** and meet their 8.2 K on the board, below the header ("Series legs", below) — so a bridge between C3 and D3 joins the bus lines directly, and one from B3 to either holds that line at ground — and if J5 is push-pull (untested, `docs/taps-and-dividers.md`), either bridge shorts a display-driver output, into ground or into the other output *(until 2026-11-05 E3 took a `Disp-P4` branch behind 8.2 K, terminated at nothing; and both signals arrived behind their 8.2 K in the T-piece)* |
| J4, 3-pin | I3 `#` · J3 *empty* · K3 `T` *(G3–I3 until 2026-11-09: two side-entry XH bodies do not fit side by side there — J5's body covers G3)* | **`+` never enters the box**: the T-piece's branch to the board carries `T` (yellow) and `#` (black) only — **`#` arrives RAW** and meets its 100 K on the board ("Series legs", below) — and the plug's **middle** cavity is left **without a contact**. **`T` and `#` take the two outer pins so a hole separates them**: a bridge between them now **shorts the machine's `#` node to ground**, holding the flowmeter signal low — the `E01` dose-control class. *(With the 100 K in the T-piece, until 2026-11-05, the same bridge hung 100 K to ground on the node, ~4.68 V → ~4.28 V, ~8.6 %.)* `+` (green) runs machine-to-sensor inside the T-piece. J3 is soldered and connects to nothing; I4, I5 and J4–K5 stay empty — `#` at I3 sits in the bus column three holes above the bus's top end at I6, so **the bus must not be extended upward**. A branch wrongly built with all three wires is caught by the pre-power check below, which requires `+` to read open *(T sat in the middle, beside `#`, for part of 2026-11-03)* |
| J2, 5-pin | E′3 Water · F′3 1Cup · G′3 2Cup · H′3 Steam · **I′3 +5 V (pos 1)** | in the top-right corner since 2026-11-04, with every machine-voltage net of the drive side — see "Drive-side layout". Pos 1 at the right end, beside the drive-leg jumpers *(P3–T3 until then)* |

**There are no 2-pin XH parts in stock**, which is why J4 is a 3-pin header
with one cavity unpopulated rather than a 2-pin one. The keying still fixes
the orientation.

**Crimp every branch plug by HOLE, not by JST pin number.** All three
headers are the same side-entry part facing the same way, so the tables
above name the net at each board hole; J2's pos 1 sits at I′3 because of
where its wire is crimped, not because of the part's pin-1 mark. A plug
seen from the wire side is mirrored, which is how a cavity gets swapped:
on J5 a swap puts `VSS` on the empty E3, leaves the ground bus without its J5 anchor and swaps `ESD1`/`ESD4`; on
J2 it puts +5 V into Water's 150 K; **on J4 it puts `#` on K3 — the ground
bus — and the machine faults `E01`. That happened, 2026-11-13**
(`docs/phase-0-status.md`): the swapped plug read "straight" by colour,
because the board header faces the other way from the machine's. **The
correct J4 branch plug looks mirrored against the machine-side plug**; trust
the meter, not the colour order. **Before first power, check each conductor by resistance, not continuity**
*(this said "meter continuity" until 2026-11-04)* — the by-value checks of the on-board series resistors below need Ω
mode. *(Until
2026-11-05 the reason given was that the J5 signals and J4 `#` sat behind
T-piece resistors, which a beeper does not sound through.)* **Disconnect the
T-piece on both sides** — from the machine's header and from the original
harness plug (flowmeter, display, switches) — and leave only its branch
plug mated to the board, with the devkit out and the board unpowered.
Anything still attached adds a parallel path. **Confirm by eye that the
flowmeter's plug is off**: no meter row reliably reveals it now. `#` reads
~0 Ω whatever else is attached *(until 2026-11-05, with the 100 K in the
T-piece, a forgotten flowmeter pulled the `#` reading toward ~69 kΩ)*, and
the `+` row may still read open with the flowmeter on —
`docs/taps-and-dividers.md` treats the 10.04 K pull-up as the machine
board's own, so with the machine disconnected the only path is through
the sensor's electronics, which may read finite or OL. Then meter
from each conductor at the T-piece's crimped plug that mates the machine header (the soldered header end reads the same) to the board hole
this layout names:

| Conductor | Board hole | Expect |
|---|---|---|
| J5 `VSS` | B3 | ~0 Ω |
| J5 `ESD1`, `ESD4` | C3, D3 | ~0 Ω each *(8.2 kΩ until 2026-11-05)* |
| **J5 `Disp-P4`** | **B3, C3, D3 and E3** | **open on all four** — it is not branched |
| J4 `T` (yellow) | K3 | ~0 Ω *(I3 until 2026-11-09)* |
| J4 `#` (black) | I3 | ~0 Ω *(G3 until 2026-11-09; 100 kΩ until 2026-11-05)* |
| **J4 `#` (black)** | **K3** | **~320 kΩ, never ~0** — through the 100 K and the 220 K shunt to the bus. ~0 Ω is the swapped plug of 2026-11-13 |
| **J4 `+` (green)** | **I3, J3 and K3** | **open on all three** |
| J2 pos 1 … pos 5 | I′3, H′3, G′3, F′3, E′3 | ~0 Ω each |

**No T-piece carries a resistor** — J2's since 2026-11-03, J4's and J5's
since 2026-11-05. Then, T-piece unplugged, check every on-board series
leg header hole to tap row: C3 → B13 and D3 → B12 **8.2 kΩ**, I3 → B14
**100 kΩ**, and J2's E′3 → B18, F′3 → B15, G′3 → B16, H′3 → B17
**150 kΩ each** — the 100 K and 150 K bags look identical, and the repo
has had to meter to tell them apart before. Do it with the T-piece and
devkit off and the board unpowered, both drive jumpers (a) open: then the
header holes have no other path to the ground bus, so the shunts hanging
off the tap rows are not in parallel with the reading, and F′6/G′5 reach
only open photoMOS outputs. **Then meter every shunt by value**, tap row
to the ground bus: B12 and B13 **16.4 kΩ** each (a reading of 8.2 kΩ is a
single resistor, an open reading a half left unfitted), B14–B18 **220 kΩ**
each. *(Added 2026-11-05: a missing J5 shunt half very probably still
decodes — inferred, not tried — so nothing else catches it. Said "still
decodes" unqualified until 2026-11-06.)*

**Ground bus:** bare wire down **column I, rows 6–30**, fed from J5 `VSS`
(B3) and J4 `T` (K3, by an insulated link to the bus's top end at I6), and tied to the devkit at **K30**, the opposite
column's GND, so no ground joint sits beside the 5Vin pin at A29.

**Shunts, under the devkit.** Each tap pin A_r is joined to B_r, and the
shunt runs from B_r to the bus. **Make that join with the resistor lead or a
short wire, not a solder blob**: the seven joins sit in consecutive rows on
seven different nets, and a blob that spreads one row joins two of them:

| Row | Tap | Shunt | Holes |
|---|---|---|---|
| 12 | `ESD4` | 8.2 K + 8.2 K | B12–E12, E12–I12 |
| 13 | `ESD1` | 8.2 K + 8.2 K | B13–F13, F13–I13 — midpoint offset from row 12's |
| 14 | flow `#` | 220 K | B14–I14 |
| 15 | 1Cup | 220 K | B15–I15 |
| 16 | 2Cup | 220 K | B16–I16 |
| 17 | Steam | 220 K | B17–I17 |
| 18 | Water | 220 K | B18–I18 |

Signals in, each through its series leg: J5 `ESD4` (D3) → B12, J5 `ESD1` (C3) → B13, J4 `#` (I3) → B14.

**Series legs for J4 and J5 — on the board since 2026-11-05**, laid out as
J2's 150 K are: flat, running down the board under the header, the
resistors' raw leads offset by a row so no two of them are neighbours:

| Line | Raw node = top lead | Series | Sense end → tap |
|---|---|---|---|
| `ESD1` | C5 | 8.2 K, C5–C9 | C9 → B13 |
| `ESD4` | D6 | 8.2 K, D6–D10 | D10 → B12 |
| `#` | G5 | 100 K, G5–G9 | G9 → B14 |

Each raw node is linked by wire to its header pin (C3 → C5, D3 → D6, and
I3 → G5, which runs two columns across since the J4 move of 2026-11-09);
C4, D4, D5 and G4 stay empty. **The offset covers the resistor leads
only**: the header pins C3 and D3 are adjacent to each other and to B3
`VSS`, as any XH header's pins are, and the C3 → C5 and D3 → D6 links lie
in neighbouring columns. Make both links insulated wire, never bare
jumpers, and inspect C3–D3 and B3–C3 for bridges after soldering the
header. **These three raw nodes are machine nets**,
as J2's are: a bridge from one to the ground bus (column I) or to its
neighbour bypasses the resistor. *(Until 2026-11-05 these resistors lived
in the T-pieces and the signals ran from the header holes straight to the
tap rows.)*

**J2's four 150 K, lying flat and running down the board under the J2
header**, alternately starting at row 5 and row 6, so that no two raw nodes
are neighbours — a solder bridge between raw direction lines would join two
machine signals with no 150 K in the path:

| Line | Raw node = top lead | 150 K | Sense end → tap |
|---|---|---|---|
| Water | E′5 | E′5–E′9 | E′9 → B18 |
| 1Cup | F′6 | F′6–F′10 | F′10 → B15 |
| 2Cup | G′5 | G′5–G′9 | G′9 → B16 |
| Steam | H′6 | H′6–H′10 | H′10 → B17 |

Each raw node is linked by wire to its J2 pin above it (E′3, F′3, G′3,
H′3); F′5 and H′5 stay empty. **F′6 and G′5 are the raw 1Cup and 2Cup
nodes the photoMOS outputs join** (see "Drive-side layout"). Pos 1 (I′3)
goes straight right into the drive-leg jumpers. *(The block sat at P3–T3
with raw nodes P5/Q6/R5/S6 until 2026-11-04, when it moved to the corner
before anything was soldered.)*

**Selftest header:** 2-pin at **L33–M33**, GPIO8 (from A20) and GPIO6
(from A14), below the drive field and clear of the devkit's USB end. Its
jumper is never fitted in service.

## Drive-side layout — DRAWN 2026-11-04

**Phase 1 hardware, laid out now so it is built and bench-tested behind open
jumpers, as `docs/phase-0-plan.md` decided.** Same coordinates as the sense
side. The machine-voltage parts are drawn **hole by hole**; the '221 area,
3V3 logic that reaches the machine only through the photoMOS — which the
open (a) jumpers block in Phase 0 — is given as **placement plus netlist**.
**That does not make its bridges harmless**: see "Bridges that can arm or
light a channel" below. Every link is insulated wire on the underside
unless it joins two holes of the same net.

**Zones.**

- **Top-right corner — machine voltage:** J2, its four 150 K, pos 1, the two
  drive-leg jumpers (a) and the photoMOS outputs. Everything that can press
  a lever is in this corner.
- **Right edge, rows 12–28 — the photoMOS stack**, outputs on the edge
  column L′, LED side facing left. The isolation inside each AQY212EH is the
  boundary between machine voltage and our 3V3.
- **Columns C′–I′, rows 12–31 — four LED-driver cells.**
- **Columns M–Y, rows 12–30 — the three '221s**, next to the devkit's drive
  column K.
- **Bottom-right — jumper (b)**, the LED supply, at the far end of the board
  from the (a) pair.

### Machine corner

| Part | Holes | Net / note |
|---|---|---|
| J2 header | E′3–I′3 | see "Sense-side layout"; **I′3 is pos 1, +5 V** |
| J2's four 150 K | E′5–E′9, F′6–F′10, G′5–G′9, H′6–H′10 | see "Sense-side layout"; **raw 1Cup = F′6, raw 2Cup = G′5** |
| **Jumper (a1), 1Cup** | **K′3** (pos 1) – **K′4** (1Cup leg) | 2-pin header, **OPEN through Phase 0** |
| **Jumper (a2), 2Cup** | **K′6** (pos 1) – **K′7** (2Cup leg) | 2-pin header, **OPEN through Phase 0** |

**Pos 1** is I′3, K′3 and K′6 and nothing else: I′3 → K′3 → K′6 by
insulated wire, skipping J′3, K′4 and K′5. **With both jumpers open, the
live machine holes on the drive side are exactly these: pos 1 (I′3, K′3,
K′6) and the two raw-line returns (L′18 → F′6, L′28 → G′5)**, which sit on
the 1Cup and 2Cup direction lines and go to 5 V on every press. Everything
else is dead to the machine in Phase 0. *(Until review, 2026-11-04, a draft
listed only one of those two groups at a time.)*

### PhotoMOS stack (AQY212EH in DIP-4 sockets)

Each sits **vertically**: pin 1 (LED anode) at (I′, r), pin 2 (cathode) at
(I′, r+1), pin 3 at (L′, r+1), pin 4 at (L′, r) — counter-clockwise seen
from the top, as a DIP is numbered. **Pin 1, the dimple, faces left and
up.** Outputs 3 and 4 are interchangeable; only the LED has a polarity to
get wrong.

| Device | r | Pins 1 / 2 | Pins 4 / 3 |
|---|---|---|---|
| U4 — 1Cup, photoMOS 1 | 12 | I′12 / I′13 | L′12 / L′13 |
| U5 — 1Cup, photoMOS 2 | 17 | I′17 / I′18 | L′17 / L′18 |
| U6 — 2Cup, photoMOS 1 | 22 | I′22 / I′23 | L′22 / L′23 |
| U7 — 2Cup, photoMOS 2 | 27 | I′27 / I′28 | L′27 / L′28 |

**Drive chains, all insulated wire:**

- **1Cup:** K′4 → L′12 · L′13 → L′17 · **L′18 → F′6** (raw 1Cup)
- **2Cup:** K′7 → L′22 · L′23 → L′27 · **L′28 → G′5** (raw 2Cup)

Three empty rows separate each device's outputs from the next, and the 1Cup
chain's last node (L′18) from the 2Cup chain's first (L′22). Keep the two
return wires, L′18 → F′6 and L′28 → G′5, by these rules — they are
direction lines:

- at least one hole from I′3, K′3 and K′6 (pos 1) and from K′4 and K′7;
- never past the G′ GND bus, the C′ LED rail or the row-35 3V3 bus — a
  chafe to ground loads R45 and suppresses the lever, a chafe to 3V3 is a
  phantom press;
- the 2Cup return kept clear of the 1Cup chain's holes (L′12–L′18), and the
  two returns not run as a pair.

*(A draft said "along the right edge", which puts them beside pos 1.)*

### LED-driver cell, ×4

**Rails:** **C′12–C′34 is the LED rail** (our 3V3 after jumper (b)),
**G′12–G′32 is the drive GND bus**, both bare wire. D′–F′ lie between them.

Each cell occupies rows r to r+4, with r = 12, 17, 22, 27, matching its
photoMOS. All resistors stand upright (hairpin, two adjacent holes).

| Part | Holes | Connects |
|---|---|---|
| 330 Ω | C′r – D′r | C′r on the LED rail; **D′r → I′r** (pin 1) |
| BC547B | D′(r+2) C · E′(r+2) B · F′(r+2) E | **flat face toward the bottom edge**, so C–B–E run left to right. **D′(r+2) → I′(r+1)** (pin 2); F′(r+2) → G′(r+2), the GND bus |
| 100 K | E′(r+3) – F′(r+3) | E′(r+3) joined to E′(r+2), the base; F′(r+3) joined to F′(r+2), the emitter |
| 8.2 K | E′(r+4) – F′(r+4) | E′(r+4) joined to E′(r+3), the base; **F′(r+4) is the cell's `Q` input** from its '221 channel |

**The part is a BC547 (C–B–E). A 2N3904 is E–B–C and would go in reversed**
— the shopping list allows either, but the order bought BC547B.

**Neighbouring holes on different nets in each cell, and what a bridge does:**

| Bridge | Effect |
|---|---|
| D′r anode – C′r rail | that is the 330 Ω itself |
| D′(r+2) collector – C′(r+2) rail | LED shorted out: never lights. Fail-safe |
| E′(r+2) base – F′(r+2) emitter, or E′(r+3)–F′(r+3) | transistor held off. Fail-safe |
| F′(r+4) `Q` – F′(r+3) emitter | `Q` shorted to GND: the channel never fires. Fail-safe |
| **D′(r+2) collector – E′(r+2) base** — adjacent TO-92 legs | **lights the LED**: the BC547 becomes a diode, ~4.4 mA flows through the LED, above the AQY212EH's 3 mA operate current, and that photoMOS stays closed whenever (b) is fitted, invisible to the `Q` readback |

**So one bridge in a cell CAN light an LED: collector to base.** Inspect
those two legs under the loupe on every cell. *(Said "no bridge between neighbours in a cell can turn an LED on"
until review, 2026-11-04.)*

### The '221s — placement and netlist

| Chip | Pins 1–8 | Pins 16–9 | Channels |
|---|---|---|---|
| U1 | N12 → N19 | Q12 → Q19 | ch 1 = 1Cup photoMOS 1, ch 2 = 1Cup photoMOS 2 |
| U2 | N23 → N30 | Q23 → Q30 | ch 1 = 2Cup photoMOS 1, ch 2 = 2Cup photoMOS 2 |
| U3, the **PROPOSED** gate | T12 → T19 | W12 → W19 | ch 1 = gate, ch 2 unused |

Pin 1 at the top of each N/T column (row 12 or 23), DIP-16 sockets.
Pinout per `docs/phase-0-plan.md`: 1 1A̅ · 2 1B · 3 1R̅ · 4 1Q̄ · 5 2Q ·
6 2C_X · 7 2C_XR_X · 8 GND · 9 2A̅ · 10 2B · 11 2R̅ · 12 2Q̄ · 13 1Q ·
14 1C_X · 15 1C_XR_X · 16 V_CC.

**Netlist:**

| Net | Connects |
|---|---|
| 3V3 | devkit A9 (by wire under the devkit) → a bare bus along **row 35, M35 to C′35** → every '221 pin 16, every pin 2 and 10 except U3's pin 10, every 10 K pull-up, every `R_X`, and jumper (b)'s supply side |
| drive GND | devkit **K29** → bus G′12–G′32 and every '221 pin 8. Separate from the sense ground tie at K30 |
| Triggers (`A̅`) | GPIO42 K14 → U1 pin 1 · GPIO41 K15 → U1 pin 9 · GPIO40 K16 → U2 pin 1 · GPIO39 K17 → U2 pin 9 — each with a **10 K pull-up to 3V3** |
| Tied `R̅` (arm) net | U1 pins 3 and 11, U2 pins 3 and 11, **U3 pin 13 (gate `Q`)**, and a **10 K pull-down to GND** |
| Gate enable | GPIO38 K18 → U3 pin 3, with a **10 K pull-down** |
| Gate trigger | GPIO10 A24 (wire under the devkit) → U3 pin 1, with a **10 K pull-up** |
| U3 ch 2, unused | pins 9, 10 and 11 → GND (held in reset, so `A̅` may be low), so no 3V3 sits between U3's GND pins; pins 5, 6, 7 open (12 is under "Not connected") |
| Timing, drive | per channel: **330 K** from pin 15 (or 7) to 3V3, **1 µF** between pins 14 and 15 (or 6 and 7) — soldered, never socketed |
| Timing, gate | **470 K / 1 µF** on U3 pins 14/15 |
| `Q` → cells | U1 pin 13 → F′16 (cell r 12) · U1 pin 5 → F′21 (r 17) · U2 pin 13 → F′26 (r 22) · U2 pin 5 → F′31 (r 27) |
| `Q` readbacks, 1 K each | U1 pin 13 → GPIO1 K12 · U1 pin 5 → GPIO2 K13 · U2 pin 13 → GPIO47 K25 · U2 pin 5 → GPIO21 K26 · U3 pin 13 → GPIO9 A23 (wire under the devkit) |
| Decoupling | 100 nF from each pin 16 to GND, at the chip |
| Not connected | every pin 4 and 12 (`Q̄`) |
| Jumper (b) | **C′34** (LED rail) – **C′35** (3V3 bus); open until the bench test |

GPIO18 stays unused, as `docs/phase-0-plan.md` advises. The values marked
open there — the readback series value, the gate's timing — are drawn at
their proposed values and are not settled by this layout.

### State in Phase 0

- **Jumpers (a1), (a2) and (b) all open.** The photoMOS outputs see no
  voltage and the LEDs no supply.
- **U3 left out of its socket, AND its readback 1 K (U3 pin 13 → GPIO9) left
  unfitted**, while the gate is PROPOSED. Only then does the tied `R̅`
  net's 10 K pull-down hold all four drive channels in reset. With the
  1 K fitted and the chip out, GPIO9 mis-set as an output HIGH puts ~3.0 V
  on the arm net and arms all four channels, with GPIO38 wired to an empty
  socket and unable to disarm them — the pulled-'221 hazard
  `docs/phase-0-plan.md` already records. *(Said "nothing fires even if a
  trigger pin moves" without the 1 K condition until review, 2026-11-04.)*
- The order from `docs/phase-0-plan.md` stands: fit (b) first, on the bench,
  watching the '221 outputs with J2 unplugged; fit the (a) jumpers last, at
  the machine. **For that bench step U3 and its 1 K must go in**, or every
  channel stays in reset and the board looks dead, with J2 unplugged.
- **U3 and its readback 1 K always go in or come out together.** Never one
  without the other.

### Bridges that can arm or light a channel

Single adjacent-pin bridges on the '221 sockets that are **not** fail-safe:

| Bridge | Effect |
|---|---|
| U1/U2 pins 2–3 or 10–11 (`B` = 3V3 beside `R̅`) | the tied arm net held HIGH: all four channels permanently armed |
| U1/U2 pins 3–4 or 11–12 (`R̅` beside `Q̄`) | `Q̄` is HIGH while disarmed and drives the arm net HIGH: all four armed |
| U1/U2 pins 4–5 or 12–13 (`Q̄` beside `Q`) | `Q̄` drags `Q` toward mid-rail; ~0.1 mA through the 8.2 K saturates the BC547 and lights that LED, while the readback reads LOW |
| U3 pins 12–13 (spare `Q̄` beside the gate `Q`) | the arm net held HIGH: all four armed |
| any pins 7–8 (timing node beside GND) | that one-shot holds |

**None of them can reach the machine while the (a) jumpers are open — but
most of them are invisible to a static check.** An armed board and a held
one-shot both look normal with nothing firing. **So inspect every '221
socket's adjacent pins under the loupe**, and treat the drive as unverified
until the bench test of `docs/phase-0-plan.md` — which is OPEN, with
`drivetest`, pending the bench sweep — has passed.

**One static check does work, after (b) is fitted and before any (a)
jumper:** with J2 unplugged and every trigger idle, **pins 3 and 4 of each
AQY212EH read open.** A closed one is an LED lit by a fault: the
collector–base bridge in its cell, or a `Q̄`–`Q` bridge. That is all it
catches. *(A draft said this check "finds each one" of the bridges above and
added a fire-each-channel step that named no instrument and no way to arm;
both withdrawn after review, 2026-11-04.)*
