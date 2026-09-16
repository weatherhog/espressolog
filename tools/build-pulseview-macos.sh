#!/usr/bin/env bash
#
# Build PulseView (the sigrok GUI) from source on macOS/Apple Silicon.
#
# WHY THIS EXISTS — you cannot `brew install pulseview`:
#
#   * There is no PulseView formula or cask in Homebrew, and the official
#     macOS bundle on sigrok.org is an ancient x86 build.
#   * PulseView master needs libsigrok/libsigrokdecode MASTER, not the 0.5.x
#     releases Homebrew ships. Two hard breaks, not one:
#       - libsigrok 0.5.2's C++ API takes Glib::TimeVal where master takes
#         Glib::DateTime (patchable, one call site), AND
#       - libsigrokdecode 0.5.3 has no decoder logic-output API at all
#         (SRD_OUTPUT_LOGIC, srd_proto_data_logic,
#         srd_decoder.logic_output_channels). Not patchable — the feature
#         simply does not exist in the release.
#   * sigrok's own cross-compile/macosx/sigrok-native-macosx script rebuilds
#     the entire dependency world into a bundle and is years stale. This
#     script instead borrows Qt/glib/boost/python from Homebrew and builds
#     only the three sigrok components.
#
# The result installs into its own prefix and does NOT touch Homebrew's
# sigrok-cli, which keeps running against the 0.5.2 release. The two are
# independent: `brew upgrade` cannot break PulseView and vice versa.
#
# Verified 2026-09-15 on macOS 26 (Tahoe) / M-series, Qt 6.11.2,
# python@3.14, against a SeenGreat SG-NANO-DLA-A (FX2LP, USB 1d50:608c).

set -euo pipefail

PREFIX="${PREFIX:-$HOME/.local/opt/sigrok}"
WORKDIR="${WORKDIR:-$(mktemp -d -t pulseview-build)}"

# Commits this recipe was verified against. Unset a variable (or set it to
# an empty string) to build that component from current master instead.
LIBSIGROK_REF="${LIBSIGROK_REF-0bc2487}"        # 2024-03-29
LIBSIGROKDECODE_REF="${LIBSIGROKDECODE_REF-71f4514}"  # 2024-10-01
PULSEVIEW_REF="${PULSEVIEW_REF-af02198}"        # 2025-11-10

say() { printf '\n==> %s\n' "$*"; }

say "prefix:  $PREFIX"
say "workdir: $WORKDIR"

# --- Homebrew dependencies -------------------------------------------------
# qt         Qt 6 — PulseView master falls back to Qt6 when Qt5 is absent,
#            so we avoid the deprecated keg-only qt@5 entirely.
# doxygen    REQUIRED by libsigrok to generate its C++ bindings. Without it
#            configure silently disables libsigrokcxx and PulseView won't build.
# python@3.14 provides python3-embed.pc for libsigrokdecode. A pyenv python is
#            NOT enough — it usually lacks the shared library to embed.
say "installing Homebrew dependencies"
brew install --quiet \
  qt cmake boost glib glibmm@2.66 libzip libusb libserialport libftdi hidapi \
  autoconf automake libtool pkgconf doxygen python@3.14 sigrok-cli

export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:/opt/homebrew/opt/python@3.14/lib/pkgconfig:/opt/homebrew/lib/pkgconfig:/opt/homebrew/share/pkgconfig"
JOBS="$(sysctl -n hw.ncpu)"

clone() {  # clone <repo> [ref]
  local repo="$1" ref="${2:-}"
  rm -rf "$WORKDIR/$repo"
  git clone "https://github.com/sigrokproject/$repo.git" "$WORKDIR/$repo"
  if [ -n "$ref" ]; then git -C "$WORKDIR/$repo" checkout --quiet "$ref"; fi
  git -C "$WORKDIR/$repo" log -1 --date=short --format="    $repo @ %h %ad %s"
}

# --- libsigrok -------------------------------------------------------------
# Bindings we do not need are disabled: Python/Ruby/Java bindings would drag
# in swig. The C++ binding (libsigrokcxx) is the one PulseView links against.
say "building libsigrok"
clone libsigrok "$LIBSIGROK_REF"
cd "$WORKDIR/libsigrok"
./autogen.sh
./configure --prefix="$PREFIX" --disable-python --disable-ruby --disable-java
make -j"$JOBS"
make install

# --- libsigrokdecode -------------------------------------------------------
say "building libsigrokdecode"
clone libsigrokdecode "$LIBSIGROKDECODE_REF"
cd "$WORKDIR/libsigrokdecode"
./autogen.sh
./configure --prefix="$PREFIX"
make -j"$JOBS"
make install

# --- PulseView -------------------------------------------------------------
# CMAKE_POLICY_VERSION_MINIMUM: PulseView still declares
# cmake_minimum_required(VERSION 2.8.12...), which CMake 4 refuses outright.
say "building PulseView"
clone pulseview "$PULSEVIEW_REF"
mkdir -p "$WORKDIR/pulseview/build"
cd "$WORKDIR/pulseview/build"
cmake .. \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX"
make -j"$JOBS"
make install

# --- launchers -------------------------------------------------------------
# A minimal .app bundle so PulseView is reachable from Spotlight. It is
# unsigned: the first Spotlight launch needs right-click -> Open.
say "installing launchers"
mkdir -p "$HOME/.local/bin"
ln -sf "$PREFIX/bin/pulseview" "$HOME/.local/bin/pulseview"

APP="$HOME/Applications/PulseView.app"
mkdir -p "$APP/Contents/MacOS"
cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleName</key><string>PulseView</string>
	<key>CFBundleDisplayName</key><string>PulseView</string>
	<key>CFBundleIdentifier</key><string>org.sigrok.pulseview</string>
	<key>CFBundleExecutable</key><string>pulseview</string>
	<key>CFBundlePackageType</key><string>APPL</string>
	<key>NSHighResolutionCapable</key><true/>
	<key>LSMinimumSystemVersion</key><string>11.0</string>
</dict>
</plist>
PLIST
ln -sf "$PREFIX/bin/pulseview" "$APP/Contents/MacOS/pulseview"
touch "$APP"

say "done"
"$PREFIX/bin/pulseview" --version 2>/dev/null | head -5
printf '\nbuild tree left at %s (safe to delete)\n' "$WORKDIR"
