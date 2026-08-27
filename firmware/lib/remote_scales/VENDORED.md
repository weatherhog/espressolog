# Vendored: esp-arduino-ble-scales

Vendored rather than pulled so a library update can never silently change how
shots are measured mid-dataset (see CLAUDE.md). Any update is a deliberate
re-vendor: bump the commit here, diff the sources, re-run the stage-1 serial
test against a real shot before trusting it.

- Upstream: https://github.com/Zer0-bit/esp-arduino-ble-scales
  (fork of https://github.com/kstam/esp-arduino-ble-scales, MIT)
- Commit: 38c2ebdde4bd35bae346b3e015ff672d79b9a2bf (vendored 2026-08-27)
- Requires: NimBLE-Arduino 2.x (uses the 2.x scan-callback and connect APIs)

## Local changes

- **Pruned `src/scales/` to Bookoo only.** Deleted plugins: acaia, decent,
  difluid, eclair, espressiscale, felicitaScale, ikape, insmart, myscale,
  precisa, tencent, timemore, timemore_new, varia, weighmybru. Registration is
  explicit (`BookooScalesPlugin::apply()` from our code), so nothing else
  references them. Restore from upstream at the commit above if ever needed.
- Added `library.json` (upstream has none) so PlatformIO builds this as a
  `lib/` library; carries upstream's `-std=gnu++2a` build flag.
- No source-code modifications.
