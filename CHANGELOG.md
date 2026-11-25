# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Added
- `Firmware/tools/board_check.py`: functional check over USB (identity and
  version, command replies, IDS events, PING round trips, periodic telemetry,
  freshly booted state). Read-only, so the board is left as it was
- `Firmware/tools/lcd_render/`: renders the LCD pages on the host from the
  real `lcd_status.cpp` and writes `docs/screen-*.png`, so the README
  screenshots can be regenerated whenever the screen changes

### Changed
- README screenshots regenerated with the new tool: they now show the actual
  pixel font and the v1.3.0 screen (`STICKSAT` header, `OVR` badge)
- `platformio.ini` excludes `tools/` from the firmware sources, since
  `src_dir = .` would otherwise compile the host renderer into the firmware

## [1.3.0] - 2026-11-25

### Changed
- Renamed from FlatSat-Geek to StickSat. The repository is now
  `bellx2/StickSat` (the old URL redirects)
- LCD boot screen and status header read `STICKSAT` instead of
  `FLATSAT - Geek`
- UF2 output is named `sticksat-<env>-v<version>.uf2` (was `flatsat-...`)
- Unchanged for compatibility with JP-Bootcamp and existing host tools: the
  USB manufacturer/product strings (`Pwnsat`, `Pwnsat - Flatsat v...`), the
  `fsat-<chip id>` serial number, and the `FLATSAT_*` build flags

## [1.2.1] - 2026-11-24

### Fixed
- `AES_CONFIG` and `DEBUG_CONFIG` never updated the LCD: both return from
  `commandHandlerInternal()` before `commandApidHandler()`, so `LAST CMD`
  and the command counter stayed unchanged and the `AES CFG` milestone could
  not light. They now call `lcdStatusNoteCommand()` once accepted

## [1.2.0] - 2026-11-23

### Added
- IDS event channel from JP-Bootcamp v1.0.5 (`ids_events.h/.cpp`, TM APID
  0x20): one 15-octet cleartext event per received frame over USB, so the
  JP-Bootcamp Space IDS rules that need the board's own events can fire
- Flash window now ends with `MAINT:PROV_BLOCK=16` and the link key, so the
  flash-dump lab can recover the key from the extracted image
- `TC_GPS_OVERRIDE` with a 1-octet payload `0x00` clears the override and
  returns to the simulated orbit (or the real GPS)
- `OVR` chip on the LCD status page while the GPS override is active
- GitHub Actions release workflow: pushing a `v*` tag builds `pico2_geek_sim`
  and publishes the UF2 with the CHANGELOG section as release notes
- Per-board USB serial number `fsat-<chip id>`; product string carries the
  firmware version

### Fixed
- BOOT button ignored while waiting for the USB console (`WAIT USB CONSOLE`):
  the wait loop now polls it, so both page switch and flip work there
- Telemetry replies arriving late in bursts (USB TX now flushed per frame)
- Board stopping command handling when a host holds the console or the
  link port open without reading (bounded TX wait, guarded console writes)
- Telecommands delayed up to 1 s by a blocking `readBytes()` on USB RX

### Changed
- Ground station for GEEK builds moved from Tsukuba (100 km) to Sapporo
  (43.0618N, 141.3545E, 35 km), matching JP-Bootcamp. The Tokyo start
  position was inside the old radius, so the gate opened without GPS
  spoofing; it now starts closed. Simulated orbit and `GPS_MODE=1` track
  moved to Sapporo accordingly
- Per-frame USB hexdump on the console is off by default
  (`-DFLATSAT_USB_HEXDUMP=1` restores it)
- `SEND_FW` reports protocol level 1.0.5 (was 1.0.0)

## [1.1.3] - 2026-11-22

### Added
- GPS override command (`TC_GPS_OVERRIDE`, APID 0x14): inject a GPS position
  over USB with no authentication, enabling Attack 04 (GPS Spoofing) on the
  GEEK build. Starts active at Tokyo (35.7122N, 139.7038E, 35 m) matching
  JP-Bootcamp defaults. Clearing the override falls back to the orbit
  simulation.

## [1.1.2] - 2026-11-22

### Changed
- Ground station moved to JAXA Tsukuba (36.0604N, 140.1335E) with 100 km
  radius for GEEK builds; non-GEEK builds keep Las Vegas
- Simulated orbit now circles near Tsukuba: ~4 min in range / ~6 min out
  of range per 10-min cycle, entering range ~30 s after boot

### Fixed
- `gs_station_radius_m` uint16_t overflow when set to 100 km (changed to
  uint32_t, telemetry report capped at 65535 m)

## [1.1.1] - 2026-11-22

### Fixed
- Position line on the LCD showing only "S E alt km" without numeric values
  (printf with multiple %f was unreliable on this platform)

## [1.1] - 2026-11-21

### Added
- BOOT button long press (1 s) flips the display 180 degrees via MADCTL,
  useful when the board is mounted upside down
- Screen flip also works during the boot screen (WAIT USB CONSOLE)

## [1.0] - 2026-11-21

Initial release for the Waveshare RP2350-GEEK.

### Added
- RP2350 / Pico 2 build environments (`pico2_tinyusb`, `pico2_tinyusb_norf`,
  `pico2_geek_norf`, `pico2_geek_sim`)
- Radio-off build: SX1262 modules compiled out and held in hardware reset
  (`FLATSAT_NO_RADIO`)
- 1.14" ST7789 LCD status screen (`FLATSAT_LCD_GEEK`) with three pages:
  - **Status**: mission mode, USB/sensor status chips, last command, telemetry,
    GPS position
  - **Attack scoreboard**: eight milestones tracking student progress during
    satellite-attack exercises
  - **System**: reset reason, uptime, heap, TM/CMD counts, environment,
    firmware version
- Simulated sensors (`FLATSAT_SIM_SENSORS`): BME280, LIS2DH12, GPS with
  ISS-like circular orbit model and thermal cycling
- Simulated GPS modes: orbital (mode 0), ground-station flyover (mode 1),
  Tokyo fixed (mode 2)

### Based on
- [Pwnsat/FlatSat](https://github.com/Pwnsat/FlatSat) by astrobyte
  (GPL-2.0-or-later)
