# Getting started

Clone-to-running for each of the four components. **Three of the four need
no espresso machine and no hardware at all** — only the firmware *flashing*
step does.

If you only want to read about the project, start with [the overview](overview.md).
If you are about to touch the machine, read `CLAUDE.md`'s invariants first —
they are safety rules, not style.

## Prerequisites

| Component | Needs | Version |
|---|---|---|
| server | Go | **1.27+** (`server/go.mod`) |
| web | Node + npm | **22** (what CI uses) |
| firmware (build) | PlatformIO | `pip install platformio` |
| firmware (host tests) | a C++ compiler | `clang++` by default; `make CXX=g++` works |
| analysis | Python 3 + `pandas numpy matplotlib` | no manifest yet — install by hand |

## Server + web UI (the usual entry point)

The Go service embeds the built PWA, so the web build runs first.

```bash
cd web && npm ci && cd ../server && make web && go run ./cmd/espressolog
```

That listens on `:8080` and creates `espressolog.db` in `server/`, applying
all nine migrations. Both are flags: `-addr`, `-db`.

**It will be an empty database.** There is no seed or demo data in the repo,
so the UI comes up working but with nothing in it. Shots arrive from the
firmware over HTTP POST; there is currently no way to load a sample shot
without hardware.

## Web UI with hot reload

```bash
cd web && npm ci && ESPRESSOLOG_API=http://localhost:8080 npm run dev
```

**The `ESPRESSOLOG_API` part is not optional for outside contributors.** The
default proxy target is `espressolog.lan`, a scrubbed placeholder for the
author's LAN host, and without the override the UI loads but reports the
server link as down.

## Firmware

Building needs no board and no secrets:

```bash
cd firmware && pio run
```

The host-compiled logic tests are the fast way to work on the detector, the
display decode, the spool or the brew guard — no board, no machine:

```bash
cd firmware/test/host && make
```

**Flashing is the one step that needs hardware**, and specifically this
hardware: an ESP32-S3-DevKitC-1 clone, N16R8 (16 MB flash, 8 MB octal
PSRAM). `platformio.ini` sets `memory_type = qio_opi` for it; on a different
module that setting boot-loops. You also need `firmware/include/secrets.h` —
copy `secrets.h.example` and fill in WiFi and the endpoint. It is gitignored
and must stay that way.

## Analysis notebooks

```bash
pip install pandas numpy matplotlib
cd analysis && jupyter lab 0f-noise-floor.ipynb
```

The notebook expects `analysis/espressolog.db`. Its first cell fetches that
from the author's deployment over `ssh`/`scp`, which will not work for you —
**point it at a local database instead**, e.g. the `server/espressolog.db`
the steps above create. The standalone scripts (`overshoot.py`,
`backfill_recipe.py`) use only the standard library but need the same file.

## What CI checks

Five jobs on every push to `main` and every PR: `server` (vet + test),
`firmware-logic` (the host tests), `firmware-build` (`pio run`), `web`
(lint with `--max-warnings 0`, then build), and **`facts`**.

That last one is unusual and worth knowing about before you edit a
document. `tools/check-facts.py` asserts that every value carrying a
`fact` marker — an HTML comment naming an id and the value, which you will
see beside load-bearing numbers in `CLAUDE.md` and the hardware docs —
agrees everywhere it appears, across docs *and* source. Changing a tagged
number in one file and not the others fails the build.

(Do not paste a literal example of the marker into a file: the checker
scans raw text and does not care that it is inside a code span, so an
illustrative one registers as a real tag. That happened while this page was
being written.)

Run it locally:

```bash
python3 tools/check-facts.py
```

## A note on how this repo is written

Superseded claims are kept, marked as superseded, together with the
reasoning that retired them — so a passage reading *"this said X until
<date>, which was wrong because Y"* is doing a deliberate job and should not
be tidied away. Claims also carry evidence grades (measured / datasheet /
inferred / assumed), and flattening those loses the point.

The reason is in `CLAUDE.md`: this project's most-logged defect is a fact
corrected in one place while a stale copy survives in another. The retraction
history and the fact tags are the two mechanisms against it.
