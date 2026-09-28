# espressolog

Instrumentation for an **Ascaso Dream PID** espresso machine and a **Eureka
Mignon Single Dose** grinder. An ESP32-S3 reads two Bluetooth scales, detects
shots from the weight curve alone, spools them to flash, and uploads them to a
small Go service that stores them in SQLite and serves a PWA.

Currently **Phase 0: logging only**. Nothing actuates the machine — every tap is
high-impedance and read-only. That is a hard invariant, not a stage we are
waiting to leave.

## Two results worth reading even if you don't own this machine

**The front display bus is decoded.** The Ascaso's mainboard talks to its
display over a plain synchronous serial link — *not* I²C, whatever sigrok's
decoder claims when you point it at the same pins. ~9.9 kHz clock, data sampled
on the rising edge, 133-bit frames every ~41 ms, three 7-segment fields at known
bit offsets. Idle it carries boiler temperature; during a brew it carries the
machine's own shot timer in tenths of a second. Protocol and probe point are in
[CLAUDE.md](CLAUDE.md); the decoder is
[tools/decode-display-bus.py](tools/decode-display-bus.py) and the raw logic
analyser captures it was derived from are in [analysis/captures](analysis/captures).
As far as I can tell nobody had published this.

**The noise floor is measured, not guessed.** 46 shots on a fixed recipe, one
grind epoch, one detector version: **σ = 0.21 g/s on mean flow (12.2 %)**.
That number is the honest go/no-go on closed-loop control, and it is demanding.
Workings in [analysis/0f-noise-floor.ipynb](analysis/0f-noise-floor.ipynb),
including why pooling detector versions inflates it and why shot time correlates
with yield. A companion measurement,
[analysis/overshoot.py](analysis/overshoot.py), finds that weight overshoot
after the pump stops is **+0.20 g with σ 0.071 g at the 2 s mark** — which makes
stop-at-weight viable even though continuous flow control is not.

## Layout

```
firmware/   ESP32-S3, PlatformIO. BLE scales, shot detector, flash spool,
            WiFi upload, display-bus reader. Detector and decoder are pure
            logic and host-testable — see firmware/test/host.
server/     Go. SQLite via modernc (no CGO), embedded migrations, PWA served
            from embed.FS, WebSocket live stream.
web/        Vite + React PWA.
analysis/   Notebooks and scripts reading the SQLite file directly.
docs/       Phase plan and the current status document.
tools/      Display-bus decoder.
```

Start with [docs/phase-0-status.md](docs/phase-0-status.md) — it is the resume
point, and it records the traps as well as the results.

## Running it on your machine

Hardware-specific and not packaged for reuse, but nothing is hidden:

- Copy `firmware/include/secrets.h.example` to `secrets.h` and fill it in. Those
  are seed defaults only — on first boot they go into NVS, which is the runtime
  truth thereafter and editable over the serial CLI without reflashing.
- Hostnames, IPs and MAC addresses in the docs and configs are **placeholders**
  (`espressolog.lan`, `espresso.example.com`, `aa:bb:cc:dd:ee:01`). Substitute
  your own. `server/Makefile`'s `HOST` is `?=`-overridable.
- **Do not copy the wiring without reading the warnings.** The J5 display tap is
  5 V and the ESP32 is not 5 V tolerant; the divider values in
  `firmware/src/main.cpp` are chosen to be safe whether the bus is driven
  push-pull or open-drain. There is mains inside this machine.

## Credits

The reverse-engineered mainboard schematic comes from
[techdregs/Ascaso_Dream_PID_Electronics](https://github.com/techdregs/Ascaso_Dream_PID_Electronics)
— note it ships the wrong MCU datasheet (the 20-pin TSSOP variant); the part is
an STM32F030R8T6.

`firmware/lib/remote_scales/` vendors a pruned copy of
[Zer0-bit/esp-arduino-ble-scales](https://github.com/Zer0-bit/esp-arduino-ble-scales)
(MIT), Bookoo driver only. Provenance in that directory's `VENDORED.md`.
