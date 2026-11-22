# Changelog

All notable changes to this project will be documented in this file.

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
