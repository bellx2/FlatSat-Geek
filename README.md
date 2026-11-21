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
