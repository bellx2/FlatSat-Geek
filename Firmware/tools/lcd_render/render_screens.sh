#!/bin/sh
# Regenerates the LCD screenshots (docs/screen-*.png) from lcd_status.cpp.
# Needs a host C++ compiler, zlib, and the Adafruit GFX Library that PlatformIO
# downloads for pico2_geek_sim.
#
#   tools/lcd_render/render_screens.sh [out_dir]    (out_dir defaults to ../docs)
#
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu

here=$(cd "$(dirname "$0")" && pwd)
fw=$(cd "$here/../.." && pwd)
out=${1:-$fw/../docs}
gfx="$fw/.pio/libdeps/pico2_geek_sim/Adafruit GFX Library"

if [ ! -f "$gfx/Adafruit_GFX.cpp" ]; then
  echo "Adafruit GFX Library not found. Run in Firmware/: pio pkg install -e pico2_geek_sim" >&2
  exit 1
fi

build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT

${CXX:-c++} -std=c++17 -w -DARDUINO=10800 \
  -DPICO_RP2350=1 -DFLATSAT_LCD_GEEK=1 -DFLATSAT_NO_RADIO=1 -DFLATSAT_GPS_OVERRIDE=1 -DFLATSAT_SIM_SENSORS=1 \
  -I "$here/stub" -I "$fw" -I "$gfx" \
  "$here/render.cpp" "$gfx/Adafruit_GFX.cpp" -lz -o "$build/render"

mkdir -p "$out"
"$build/render" "$out"
