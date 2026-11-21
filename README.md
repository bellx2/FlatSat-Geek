# FlatSat-Geek

Pwnsat/FlatSat firmware, adapted for the Waveshare RP2350-GEEK: no radio hardware,
and a status screen on the 1.14" LCD.

Based on `Pwnsat/FlatSat` `Firmware/` (GPL-2.0-or-later). Changes against it:

- `radio_off.h/.cpp` and `#if FLATSAT_NO_RADIO` in `firmware.ino` / `worker.cpp`:
  every call that would drive the SX1262 radios is compiled out
  (the "course no-radio" patch; one hunk was re-applied by hand).
- `lcd_status.h/.cpp`, `#if FLATSAT_LCD_GEEK`: status screen. Mission mode, USB
  console/link, sensors, GPS, last command, TM count. Drives the LCD directly
  over SPI1 (DC=GP8 CS=GP9 CLK=GP10 MOSI=GP11 RST=GP12 BL=GP25; pins taken from
  the RP2350-GEEK schematic, not yet confirmed on hardware).
- `platformio.ini`: envs `pico2_tinyusb_norf` (Pico 2, no radio) and
  `pico2_geek_norf` (RP2350-GEEK, no radio + LCD).

Build (needs PlatformIO):

    cd Firmware
    pio run -e pico2_geek_norf        # -> .pio/build/pico2_geek_norf/firmware.uf2

Prebuilt: `Firmware/build/flatsat-firmware-rp2350-geek.uf2`. Hold BOOTSEL while
plugging in USB, copy the .uf2 onto the drive. Not yet verified on hardware.

## Simulated sensors (`pico2_geek_sim`)

For a board with no sensors attached: BME280, LIS2DH12 and GPS values are
simulated (`-DFLATSAT_SIM_SENSORS=1`). A real GPS that sends NMEA always wins.
The screen shows a `SIM` chip, and the boot log prints `[INFO] SIM: ...`.

    pio run -e pico2_geek_sim
    # prebuilt: Firmware/build/flatsat-firmware-rp2350-geek-sim.uf2

Simulated GPS (`-DFLATSAT_SIM_GPS_MODE=n`):

- `0` (default): a satellite on an ISS-like circular orbit (420 km, 51.6 deg,
  period ~92.8 min), see `orbit_sim.h`. Starts over Tokyo heading north-east.
  `-DFLATSAT_SIM_ORBIT_SPEEDUP=60` runs it 60x faster. The ground-station
  gate (35 km) opens only for a few seconds when the track passes over Las
  Vegas (first time about 21 min after boot, about 3 s).
- `1`: small fixed track over the ground station, gate stays open.
- `2`: small fixed track over Tokyo, gate stays closed.

Simulated environment (space-like):

- Temperature follows the orbit's sunlight/eclipse cycle: about -28 C coming
  out of shadow, about +65 C at the end of the sunlit arc, 37% of each orbit in
  eclipse. It speeds up with `-DFLATSAT_SIM_ORBIT_SPEEDUP`.
- Pressure and humidity read 0 (vacuum); altitude from the BME280 is reported
  as 0 because pressure altitude is meaningless in vacuum.
- The accelerometer reads micro-g (free fall): well under 1 mg of noise on
  each axis. Its temperature tracks the skin temperature with a lag.
- Tunables: `FLATSAT_SIM_T_HOT_C`, `FLATSAT_SIM_T_COLD_C`,
  `FLATSAT_SIM_T_TAU_S`, `FLATSAT_SIM_ECLIPSE_FRACTION`.

## Screen pages

Press the BOOT button on the RP2350-GEEK to switch pages (the page number is
shown at the top, e.g. `2/3`):

1. **Status**: mode, links, sensors, last command, telemetry, position.
2. **Attack scoreboard**: eight milestones (recon, mode change, ground-station
   auth, flash read, AES config, debug on, payload armed, thruster). Command
   milestones tick when the command is received; GS AUTH, DEBUG ON and ARMED tick
   when the satellite is really in that state. A newly reached milestone is
   highlighted for 3 s. Counters reset on reboot.
3. **System**: firmware version (`FW_VERSION` in `version.h`, plus the git commit
   count as the build number, the git hash with `+` if the tree had local changes,
   and the build date/time, all filled in automatically by `tools/gen_version.py`),
   reset reason (power-on / watchdog or soft reset via RESETC /
   brown-out / RUN pin), uptime, free heap, TM and command counts, environment.
