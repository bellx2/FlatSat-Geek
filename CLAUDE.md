# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Language

- All output (commit messages, code comments, explanations) must be in **English**.
- Commit messages use a prefix (`feat:`, `fix:`, `refactor:`, `docs:`, `chore:`, etc.).
- Do not include Claude Code attribution lines in commits.

## What this is

StickSat is a **vulnerable-by-design satellite trainer** (Pwnsat/FlatSat) adapted for the **Waveshare RP2350-GEEK** board. It is a teaching tool for space-security exercises: students attack the satellite over USB using CCSDS Space Packet Protocol (SPP) commands.

The radio hardware (two SX1262 modules) is compiled out (`FLATSAT_NO_RADIO`). All command/telemetry traffic goes over USB CDC. A 1.14" ST7789 LCD shows live status.

## Build

Requires [PlatformIO](https://platformio.org/). All source is flat under `Firmware/`.

```bash
cd Firmware

# RP2350-GEEK, no radio, LCD status screen
pio run -e pico2_geek_norf

# Same + simulated sensors (no hardware needed)
pio run -e pico2_geek_sim

# Pico 2, no radio, no LCD
pio run -e pico2_tinyusb_norf

# Original Pico (RP2040)
pio run -e pico_tinyusb_norf
```

Prebuilt UF2s are attached to GitHub releases. Flash by holding BOOTSEL and copying the `.uf2` onto the USB drive.

## Build environments

| Environment | Board | Radio | LCD | Sensors |
|---|---|---|---|---|
| `pico_tinyusb` | Pico (RP2040) | yes | no | real |
| `pico_tinyusb_norf` | Pico (RP2040) | no | no | real |
| `pico2_tinyusb` | Pico 2 (RP2350) | yes | no | real |
| `pico2_tinyusb_norf` | Pico 2 (RP2350) | no | no | real |
| `pico2_geek_norf` | RP2350-GEEK | no | yes | real |
| `pico2_geek_sim` | RP2350-GEEK | no | yes | simulated |

Key build flags: `-DFLATSAT_NO_RADIO=1`, `-DFLATSAT_LCD_GEEK=1`, `-DFLATSAT_SIM_SENSORS=1`.

## Architecture

**Dual-core RP2350** (Arduino + earlephilhower core):
- **Core 0** (`loop()`): GPS polling, radio (if enabled), telemetry workers, USB console, LCD refresh.
- **Core 1** (`loop1()`): USB CDC packet receive only.

### Key modules

- **`worker.cpp`** — central command dispatcher and mission state machine. Contains `mission_context_t` (file-static), all TC/TM handlers, ground-station authentication logic, and the attack scoreboard milestone triggers. This is the largest and most important file.
- **`spp.h/.cpp`** — CCSDS Space Packet Protocol: packet build/unpack, primary and secondary headers.
- **`mission.h`** — APID definitions (TC and TM), mission mode constants, status flag bitmasks. The source of truth for the protocol.
- **`sensors.h/.cpp`** — BME280, LIS2DH12, GPS (NMEA over UART1). When `FLATSAT_SIM_SENSORS=1`, values are synthesized: GEEK builds use a circular orbit near Sapporo (~4 min in range / ~6 min out per 10-min cycle; only used when the GPS override is inactive); non-GEEK builds use `orbit_sim.h` (ISS-like orbit). Both include thermal cycling.
- **`lcd_status.h/.cpp`** — ST7789 LCD driver and three screen pages (Status, Attack Scoreboard, System). Compiled in only with `FLATSAT_LCD_GEEK=1`. Draws to an off-screen `GFXcanvas16` and pushes via SPI1.
- **`ids_events.h/.cpp`** — IDS event channel (TM APID 0x20, ported from JP-Bootcamp v1.0.5): one cleartext 15-octet event per received frame, USB only. Emitted from `commandHandlerInternal()`.
- **`secure_link.h/.cpp`** — AES-128-ECB "encryption" layer (deliberately weak, part of the exercise).
- **`usbCDC.h/.cpp`** — dual USB CDC: `Serial` (console) + `USBRadioLink` (SPP frame transport).
- **`radio_off.h/.cpp`** — holds SX1262 chips in hardware reset when `FLATSAT_NO_RADIO=1`.
- **`orbit_sim.h`** — header-only: circular orbit model (position) + thermal model (temperature cycling). Pure C++, no Arduino dependency.
- **`version.h` + `tools/gen_version.py`** — PlatformIO pre-build script injects git commit count, hash, and build date into `version_gen.h`.

### Command flow

1. USB frame received on Core 1 → `obcUSBRecv()` → callback `commandHandlerUSB()`
2. `commandHandlerInternal()` unpacks SPP, decodes secure link if active, checks ground-station gate
3. `commandApidHandler()` dispatches by APID → handler builds TM response → `dispatchTelemetryPacket()` sends over USB (and radio if enabled)

### Ground-station gate

When GS mode is enabled, most commands require GPS fix within ground-station range *and* an active authenticated session (challenge-response with shared key `0xC0DEFACE`). GEEK builds use Sapporo (43.0618N, 141.3545E, 35 km radius), matching JP-Bootcamp; non-GEEK builds use Las Vegas (36.1699N, 115.1398W, 35 km). GEEK builds boot with the GPS override active at Tokyo (~830 km away), so the gate stays closed until the position is spoofed with `TC_GPS_OVERRIDE` (APID 0x14). A 1-octet payload `0x00` on the same APID clears the override and returns to the simulated orbit.

## Conventions

- License: GPL-2.0-or-later. All new files need the SPDX header.
- Packet byte order: SPP headers are big-endian (use `HOST_TO_BE16`/`BE_TO_HOST16`); telemetry payload fields are little-endian (use `bufferPackU16LE` etc.).
- LCD pin mapping is from the RP2350-GEEK schematic: DC=GP8 CS=GP9 CLK=GP10 MOSI=GP11 RST=GP12 BL=GP25.
- UF2 binaries are not tracked in git. Pushing a tag `vX.Y.Z` runs `.github/workflows/release.yml`, which builds `pico2_geek_sim` and creates the release (the tag must match `FW_VERSION` in `version.h`). `gen_version.py` names the output with the version (e.g. `sticksat-pico2_geek_sim-v1.3.0.uf2`).
