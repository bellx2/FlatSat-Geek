/*  - lcd_status.cpp
 *
 * Status screen for the Waveshare RP2350-GEEK.
 *
 * Pins (from the RP2350-GEEK schematic, not yet confirmed on hardware):
 *   DC=GP8  CS=GP9  CLK=GP10 (SPI1 SCK)  MOSI=GP11 (SPI1 TX)  RST=GP12  BL=GP25
 * Only CLK/MOSI are handed to the SPI peripheral, so GP12 stays a plain GPIO
 * (it is also SPI1's default MISO pin, which is why the Arduino SPI wrapper is
 * not used here).
 *
 * Drawing is done into an off-screen 240x135 canvas (Adafruit_GFX) which is
 * then pushed to the panel in one transfer. Nothing here blocks the main loop
 * for more than a few milliseconds, and it redraws at most twice a second.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "lcd_status.h"

#if FLATSAT_LCD_GEEK

#include "mission.h"
#include "sensors.h"
#include "version.h"
#include <Adafruit_GFX.h>
#include <Fonts/TomThumb.h>
#include <math.h>
#include <Adafruit_TinyUSB.h>
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/watchdog.h"
#include "hardware/structs/powman.h"
#include "hardware/regs/powman.h"

#define LCD_DC 8
#define LCD_CS 9
#define LCD_CLK 10
#define LCD_MOSI 11
#define LCD_RST 12
#define LCD_BL 25

#define LCD_W 240
#define LCD_H 135
/* 135x240 ST7789 panel inside a 240x320 controller RAM; landscape offsets. */
#define LCD_X_OFFSET 40
#define LCD_Y_OFFSET_NORMAL 53
static bool lcd_flipped = false;

#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
static const uint16_t C_BG = RGB(8, 10, 20);
static const uint16_t C_BAR = RGB(20, 40, 110);
static const uint16_t C_TEXT = RGB(235, 235, 235);
static const uint16_t C_DIM = RGB(130, 135, 150);
static const uint16_t C_OK = RGB(40, 200, 90);
static const uint16_t C_WARN = RGB(240, 180, 30);
static const uint16_t C_BAD = RGB(220, 60, 60);
static const uint16_t C_HIT = RGB(250, 220, 60);

extern Adafruit_USBD_CDC USBRadioLink;

static GFXcanvas16 *canvas = NULL;
static bool lcd_ready = false;

static uint32_t cmd_count = 0;
static uint32_t tx_count = 0;
static uint16_t last_apid = 0;
static uint8_t last_source = 0;
static bool have_cmd = false;
static unsigned long last_cmd_ms = 0;
static unsigned long last_draw_ms = 0;
static uint32_t usb_cmds = 0;
static uint32_t rf_cmds = 0;

/* Pages, switched with the BOOT button. */
#define LCD_PAGES 3
static uint8_t page = 0;
static bool boot_was_down = false;
static unsigned long boot_poll_ms = 0;

/* Reset reason, captured once at start-up. */
static const char *reset_reason = "unknown";

/* Attack scoreboard: milestones a student reaches while attacking the
 * satellite. Command milestones mean "the command was received"; state
 * milestones mean the satellite is actually in that state. */
enum {
  MS_RECON,
  MS_MODE,
  MS_GS_AUTH,
  MS_FLASH_RD,
  MS_AES,
  MS_DEBUG,
  MS_ARMED,
  MS_THRUSTER,
  MS_COUNT
};
static const char *ms_names[MS_COUNT] = {"RECON",   "MODE CHG", "GS AUTH", "FLASH RD",
                                         "AES CFG", "DEBUG ON", "ARMED",   "THRUSTER"};
static bool ms_hit[MS_COUNT];
static unsigned long ms_hit_ms[MS_COUNT];

static void milestone(int id) {
  if (!ms_hit[id]) {
    ms_hit[id] = true;
    ms_hit_ms[id] = millis();
  }
}

static void captureResetReason(void) {
  if (watchdog_caused_reboot()) {
    reset_reason = "watchdog / soft reset";
  } else {
    const uint32_t r = powman_hw->chip_reset;
    if (r & POWMAN_CHIP_RESET_HAD_POR_BITS) {
      reset_reason = "power-on";
    } else if (r & POWMAN_CHIP_RESET_HAD_BOR_BITS) {
      reset_reason = "brown-out";
    } else if (r & POWMAN_CHIP_RESET_HAD_RUN_LOW_BITS) {
      reset_reason = "RUN pin";
    } else {
      reset_reason = "other";
    }
  }
}

static void lcdSelect(bool on) { gpio_put(LCD_CS, on ? 0 : 1); }

static void lcdCommand(uint8_t cmd, const uint8_t *data, size_t len) {
  spi_set_format(spi1, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
  lcdSelect(true);
  gpio_put(LCD_DC, 0);
  spi_write_blocking(spi1, &cmd, 1);
  if (len) {
    gpio_put(LCD_DC, 1);
    spi_write_blocking(spi1, data, len);
  }
  lcdSelect(false);
}

static void lcdCommand1(uint8_t cmd, uint8_t value) { lcdCommand(cmd, &value, 1); }

static void lcdFlipBuffer(void) {
  uint16_t *buf = canvas->getBuffer();
  const int n = LCD_W * LCD_H;
  for (int i = 0, j = n - 1; i < j; i++, j--) {
    uint16_t tmp = buf[i];
    buf[i] = buf[j];
    buf[j] = tmp;
  }
}

static void lcdPush(void) {
  if (lcd_flipped) {
    lcdFlipBuffer();
  }
  const uint16_t x0 = LCD_X_OFFSET, x1 = LCD_X_OFFSET + LCD_W - 1;
  const uint16_t y0 = LCD_Y_OFFSET_NORMAL, y1 = LCD_Y_OFFSET_NORMAL + LCD_H - 1;
  const uint8_t caset[4] = {(uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1};
  const uint8_t raset[4] = {(uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1};
  lcdCommand(0x2A, caset, 4);
  lcdCommand(0x2B, raset, 4);

  const uint8_t ramwr = 0x2C;
  spi_set_format(spi1, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
  lcdSelect(true);
  gpio_put(LCD_DC, 0);
  spi_write_blocking(spi1, &ramwr, 1);
  gpio_put(LCD_DC, 1);
  /* 16-bit MSB-first words: high byte of each RGB565 pixel goes out first. */
  spi_set_format(spi1, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
  spi_write16_blocking(spi1, canvas->getBuffer(), (size_t)LCD_W * LCD_H);
  lcdSelect(false);
}

static void lcdPanelInit(void) {
  pinMode(LCD_DC, OUTPUT);
  pinMode(LCD_CS, OUTPUT);
  pinMode(LCD_RST, OUTPUT);
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_CS, HIGH);
  digitalWrite(LCD_BL, LOW);

  spi_init(spi1, 24 * 1000 * 1000);
  gpio_set_function(LCD_CLK, GPIO_FUNC_SPI);
  gpio_set_function(LCD_MOSI, GPIO_FUNC_SPI);

  digitalWrite(LCD_RST, HIGH);
  delay(10);
  digitalWrite(LCD_RST, LOW);
  delay(20);
  digitalWrite(LCD_RST, HIGH);
  delay(120);

  lcdCommand(0x01, NULL, 0); /* SWRESET */
  delay(150);
  lcdCommand(0x11, NULL, 0); /* SLPOUT */
  delay(120);
  lcdCommand1(0x3A, 0x05);   /* COLMOD: 16 bit/pixel */
  lcdCommand1(0x36, 0x70);   /* MADCTL: landscape */

  const uint8_t porch[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
  lcdCommand(0xB2, porch, sizeof(porch));
  lcdCommand1(0xB7, 0x35);
  lcdCommand1(0xBB, 0x19);
  lcdCommand1(0xC0, 0x2C);
  lcdCommand1(0xC2, 0x01);
  lcdCommand1(0xC3, 0x12);
  lcdCommand1(0xC4, 0x20);
  lcdCommand1(0xC6, 0x0F);
  const uint8_t pwr[] = {0xA4, 0xA1};
  lcdCommand(0xD0, pwr, sizeof(pwr));
  const uint8_t gp[] = {0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F,
                        0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23};
  const uint8_t gn[] = {0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F,
                        0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23};
  lcdCommand(0xE0, gp, sizeof(gp));
  lcdCommand(0xE1, gn, sizeof(gn));
  lcdCommand(0x21, NULL, 0); /* INVON: this panel needs inversion */
  lcdCommand(0x13, NULL, 0); /* NORON */
  lcdCommand(0x29, NULL, 0); /* DISPON */
  delay(20);
}

void lcdStatusBegin(void) {
  captureResetReason();
  canvas = new GFXcanvas16(LCD_W, LCD_H);
  if (canvas == NULL || canvas->getBuffer() == NULL) {
    return;
  }
  lcdPanelInit();
  canvas->fillScreen(C_BG);
  lcdPush();
  digitalWrite(LCD_BL, HIGH);
  lcd_ready = true;
}

void lcdStatusBoot(const char *message) {
  if (!lcd_ready) {
    return;
  }
  canvas->fillScreen(C_BG);
  canvas->fillRect(0, 0, LCD_W, 22, C_BAR);
  canvas->setTextColor(C_TEXT);
  canvas->setTextSize(2);
  canvas->setCursor(6, 4);
  canvas->print("FLATSAT");
  canvas->setTextSize(1);
  canvas->setCursor(6 + 7 * 12, 11);
  canvas->print(" - Geek");
  canvas->setTextColor(C_DIM);
  canvas->setCursor(6, 32);
  canvas->print("vulnerable-by-design trainer");
  canvas->setCursor(6, 44);
  canvas->printf("build %s", __DATE__);
#if FLATSAT_NO_RADIO
  canvas->setTextColor(C_WARN);
  canvas->setCursor(6, 56);
  canvas->print("RADIO: disabled in this build");
#endif
  canvas->setTextColor(C_HIT);
  canvas->setTextSize(2);
  canvas->setCursor(6, 90);
  canvas->print(message);
  lcdPush();
}

void lcdStatusNoteCommand(uint16_t apid, uint8_t source) {
  cmd_count++;
  last_apid = apid;
  last_source = source;
  have_cmd = true;
  last_cmd_ms = millis();
  if (source == 1) {
    usb_cmds++;
  } else {
    rf_cmds++;
  }
  switch (apid) {
  case SPP_APID_TC_PING:
  case SPP_APID_TC_GET_STATUS:
  case SPP_APID_TC_GET_NAV:
  case SPP_APID_TC_GET_PAYLOAD_STATUS:
  case SPP_APID_TC_GS_STATUS:
    milestone(MS_RECON);
    break;
  case SPP_APID_TC_SET_MISSION_MODE: milestone(MS_MODE); break;
  case SPP_APID_TC_FLASH_READ: milestone(MS_FLASH_RD); break;
  case SPP_APID_TC_AES_CONFIG: milestone(MS_AES); break;
  case SPP_APID_TC_SET_THRUSTER: milestone(MS_THRUSTER); break;
  default: break;
  }
}

void lcdStatusNoteTx(void) { tx_count++; }

static const char *tcName(uint16_t apid) {
  switch (apid) {
  case SPP_APID_TC_PING: return "PING";
  case SPP_APID_TC_RESETC: return "RESETC";
  case SPP_APID_TC_SEND_FW: return "SEND_FW";
  case SPP_APID_TC_SET_THRUSTER: return "THRUSTER";
  case SPP_APID_TC_SET_BEACON_RATE: return "BEACON";
  case SPP_APID_TC_BROADCAST_MSG: return "BROADCAST";
  case SPP_APID_TC_FLASH: return "FLASH";
  case SPP_APID_TC_AES_CONFIG: return "AES_CFG";
  case SPP_APID_TC_FLASH_READ: return "FLASH_RD";
  case SPP_APID_TC_GET_STATUS: return "GET_STAT";
  case SPP_APID_TC_SET_MISSION_MODE: return "MODE";
  case SPP_APID_TC_GET_NAV: return "GET_NAV";
  case SPP_APID_TC_GET_PAYLOAD_STATUS: return "PL_STAT";
  case SPP_APID_TC_DEBUG_CONFIG: return "DEBUG";
  case SPP_APID_TC_GS_MODE: return "GS_MODE";
  case SPP_APID_TC_GS_ACCESS: return "GS_ACC";
  case SPP_APID_TC_GS_STATUS: return "GS_STAT";
  default: return "UNKNOWN";
  }
}

static const char *modeName(uint8_t mode) {
  switch (mode) {
  case MISSION_MODE_SAFE: return "SAFE";
  case MISSION_MODE_NOMINAL: return "NOMINAL";
  case MISSION_MODE_PAYLOAD: return "PAYLOAD";
  case MISSION_MODE_SCIENCE: return "SCIENCE";
  case MISSION_MODE_CONTINGENCY: return "CONTINGENCY";
  default: return "?";
  }
}

static uint16_t modeColor(uint8_t mode) {
  switch (mode) {
  case MISSION_MODE_SAFE: return C_WARN;
  case MISSION_MODE_NOMINAL: return C_OK;
  case MISSION_MODE_CONTINGENCY: return C_BAD;
  default: return C_TEXT;
  }
}

/* Small status chip: label on a coloured background. */
static void chip(int16_t x, int16_t y, const char *label, uint16_t colour) {
  const int16_t w = (int16_t)(strlen(label) * 6 + 8);
  canvas->fillRoundRect(x, y, w, 14, 3, colour);
  canvas->setTextSize(1);
  canvas->setTextColor(C_BG);
  canvas->setCursor(x + 4, y + 3);
  canvas->print(label);
}

static int16_t chipWidth(const char *label) { return (int16_t)(strlen(label) * 6 + 8 + 4); }

static void lcdFlip(void) {
  lcd_flipped = !lcd_flipped;
}

/* BOOT button: short press = next page, long press (>=1 s) = flip screen. */
#define BOOT_LONG_PRESS_MS 1000
static unsigned long boot_press_start = 0;
static bool boot_long_handled = false;

enum { BTN_NONE = 0, BTN_SHORT = 1, BTN_LONG = 2 };

static uint8_t bootButtonAction(unsigned long now) {
  if (now - boot_poll_ms < 80) {
    return BTN_NONE;
  }
  boot_poll_ms = now;
  const bool down = BOOTSEL;

  if (down && !boot_was_down) {
    boot_press_start = now;
    boot_long_handled = false;
  } else if (down && boot_was_down && !boot_long_handled) {
    if (now - boot_press_start >= BOOT_LONG_PRESS_MS) {
      boot_long_handled = true;
      boot_was_down = down;
      return BTN_LONG;
    }
  } else if (!down && boot_was_down) {
    boot_was_down = false;
    return boot_long_handled ? BTN_NONE : BTN_SHORT;
  }

  boot_was_down = down;
  return BTN_NONE;
}

static void pageScoreboard(unsigned long now) {
  int score = 0;
  for (int i = 0; i < MS_COUNT; i++) {
    score += ms_hit[i] ? 1 : 0;
  }
  canvas->setTextSize(1);
  canvas->setTextColor(C_DIM);
  canvas->setCursor(6, 26);
  canvas->print("ATTACK SCOREBOARD");
  canvas->setTextSize(1);
  canvas->setTextColor(score ? C_HIT : C_DIM);
  canvas->setCursor(174, 26);
  canvas->printf("SCORE %d/%d", score, MS_COUNT);

  for (int i = 0; i < MS_COUNT; i++) {
    const int col = i / 4;
    const int row = i % 4;
    const int16_t x = (int16_t)(6 + col * 120);
    const int16_t y = (int16_t)(44 + row * 17);
    const bool fresh = ms_hit[i] && (now - ms_hit_ms[i]) < 3000;
    if (fresh) {
      canvas->fillRect(x - 3, y - 3, 116, 15, C_HIT);
    }
    canvas->drawRect(x, y, 9, 9, ms_hit[i] ? (fresh ? C_BG : C_OK) : C_DIM);
    if (ms_hit[i]) {
      canvas->fillRect(x + 2, y + 2, 5, 5, fresh ? C_BG : C_OK);
    }
    canvas->setTextColor(fresh ? C_BG : (ms_hit[i] ? C_TEXT : C_DIM));
    canvas->setCursor(x + 14, y + 1);
    canvas->print(ms_names[i]);
  }

  /* Footer in a tiny font (TomThumb, 4 px advance; cursor y is the baseline). */
  canvas->setFont(&TomThumb);
  canvas->setTextColor(C_DIM);
  canvas->setCursor(6, 122);
  canvas->printf("CMDS %lu  USB %lu  RF %lu", (unsigned long)cmd_count,
                 (unsigned long)usb_cmds, (unsigned long)rf_cmds);
  canvas->setCursor(6, 131);
  canvas->print("GS AUTH / DEBUG ON / ARMED = real state");
  canvas->setFont();
}

static void pageSystem(unsigned long now, uint8_t sflags) {
  canvas->setTextSize(1);
  canvas->setTextColor(C_DIM);
  canvas->setCursor(6, 26);
  canvas->print("SYSTEM");

  int16_t y = 36;
  canvas->setTextColor(C_DIM);
  canvas->setCursor(6, y);
  canvas->print("RESET");
  canvas->setTextColor(C_TEXT);
  canvas->setCursor(60, y);
  canvas->print(reset_reason);
  y += 13;

  canvas->setTextColor(C_DIM);
  canvas->setCursor(6, y);
  canvas->print("UPTIME");
  canvas->setTextColor(C_TEXT);
  canvas->setCursor(60, y);
  const unsigned long s = now / 1000;
  canvas->printf("%lud %02lu:%02lu:%02lu", s / 86400, (s / 3600) % 24, (s / 60) % 60, s % 60);
  y += 13;

  canvas->setTextColor(C_DIM);
  canvas->setCursor(6, y);
  canvas->print("HEAP");
  canvas->setTextColor(C_TEXT);
  canvas->setCursor(60, y);
  canvas->printf("%lu B free", (unsigned long)rp2040.getFreeHeap());
  y += 13;

  canvas->setTextColor(C_DIM);
  canvas->setCursor(6, y);
  canvas->print("TM / CMD");
  canvas->setTextColor(C_TEXT);
  canvas->setCursor(60, y);
  canvas->printf("%lu sent / %lu rcvd", (unsigned long)tx_count, (unsigned long)cmd_count);
  y += 13;

  canvas->setTextColor(C_DIM);
  canvas->setCursor(6, y);
  canvas->print("ENV");
  canvas->setTextColor(C_TEXT);
  canvas->setCursor(60, y);
  float t, p, alt, h;
  if ((sflags & MISSION_FLAG_BME_OK) && bmeRead(&t, &p, &alt, &h)) {
    if (p < 1.0f) {
      canvas->printf("%.1fC vacuum", t);
    } else {
      canvas->printf("%.1fC %.0fhPa %.0f%%", t, p, h);
    }
  } else {
    canvas->print("no BME280");
  }
  y += 13;

  canvas->setTextColor(C_DIM);
  canvas->setCursor(6, y);
  canvas->print("VERSION");
  canvas->setTextColor(C_TEXT);
  canvas->setCursor(60, y);
  canvas->printf("V%s  build %d  %s", FW_VERSION, (int)FW_BUILD_NUMBER, FW_BUILD_HASH);
  y += 13;

  canvas->setTextColor(C_DIM);
  canvas->setCursor(6, y);
  canvas->print("BUILT");
  canvas->setTextColor(C_TEXT);
  canvas->setCursor(60, y);
  canvas->print(FW_BUILD_DATE);
  y += 13;

  canvas->setTextColor(C_DIM);
  canvas->setCursor(6, 126);
  canvas->print("BOOT button: next page");
}

void lcdStatusWorker(void) {
  if (!lcd_ready) {
    return;
  }
  const unsigned long now = millis();

  /* State milestones are checked on every call so a brief state is not missed. */
  lcd_mission_t m;
  lcdMissionSnapshot(&m);
  if (m.gsSession) milestone(MS_GS_AUTH);
  if (m.usbDebug) milestone(MS_DEBUG);
  if (m.payloadArmed) milestone(MS_ARMED);

  bool force = false;
  const uint8_t btn = bootButtonAction(now);
  if (btn == BTN_SHORT) {
    page = (uint8_t)((page + 1) % LCD_PAGES);
    force = true;
  } else if (btn == BTN_LONG) {
    lcdFlip();
    force = true;
  }
  if (!force && now - last_draw_ms < 500) {
    return;
  }
  last_draw_ms = now;

  const uint8_t sflags = sensorsStatusFlags();
  gps_nav_t nav;
  const bool gps_ok = gpsRead(&nav);

  canvas->fillScreen(C_BG);

  /* Header: name + uptime */
  canvas->fillRect(0, 0, LCD_W, 20, C_BAR);
  canvas->setTextColor(C_TEXT);
  canvas->setTextSize(2);
  canvas->setCursor(6, 3);
  canvas->print("FLATSAT");
  canvas->setTextSize(1);
  canvas->setCursor(6 + 7 * 12, 10); /* smaller suffix, same baseline */
  canvas->print(" - Geek");
  const unsigned long s = now / 1000;
  canvas->setTextSize(1);
  canvas->setCursor(LCD_W - 6 * 8 - 6, 6);
  canvas->printf("%02lu:%02lu:%02lu", (s / 3600) % 100, (s / 60) % 60, s % 60);
  canvas->setCursor(150, 6);
  canvas->setTextColor(C_TEXT);
  canvas->printf("%u/%u", (unsigned)page + 1, (unsigned)LCD_PAGES);

  if (page == 1) {
    pageScoreboard(now);
    lcdPush();
    return;
  }
  if (page == 2) {
    pageSystem(now, sflags);
    lcdPush();
    return;
  }

  /* Mission mode */
  canvas->setTextSize(1);
  canvas->setTextColor(C_DIM);
  canvas->setCursor(6, 26);
  canvas->print("MODE");
  canvas->setTextSize(2);
  canvas->setTextColor(modeColor(m.mode));
  canvas->setCursor(46, 23);
  canvas->print(modeName(m.mode));
  int16_t fx = 196;
  if (m.payloadArmed) {
    chip(fx, 24, "ARM", C_WARN);
    fx += chipWidth("ARM");
  }

  /* Links */
  int16_t x = 6;
  const int16_t row_link = 46;
  canvas->setTextSize(1);
  canvas->setTextColor(C_DIM);
  canvas->setCursor(x, row_link + 3);
  canvas->print("USB");
  x += 26;
  chip(x, row_link, "CON", Serial ? C_OK : C_BAD);
  x += chipWidth("CON");
  chip(x, row_link, "LINK", USBRadioLink ? C_OK : C_BAD);
  x += chipWidth("LINK");
  chip(x, row_link, "RADIO OFF", C_DIM);
  x += chipWidth("RADIO OFF");
  if (m.usbDebug) {
    chip(x, row_link, "DBG", C_WARN);
  }

  /* Sensors */
  x = 6;
  const int16_t row_sens = 64;
  canvas->setTextColor(C_DIM);
  canvas->setCursor(x, row_sens + 3);
  canvas->print("SNS");
  x += 26;
  chip(x, row_sens, "BME", (sflags & MISSION_FLAG_BME_OK) ? C_OK : C_BAD);
  x += chipWidth("BME");
  chip(x, row_sens, "ACC", (sflags & MISSION_FLAG_ACC_OK) ? C_OK : C_BAD);
  x += chipWidth("ACC");
  uint16_t gps_col = C_BAD;
  const char *gps_txt = "GPS none";
  if (gps_ok && nav.uartOk) {
    if (nav.hasFix) {
      gps_col = C_OK;
      gps_txt = "GPS fix";
    } else if (nav.nmeaActive) {
      gps_col = C_WARN;
      gps_txt = "GPS nofix";
    } else {
      gps_col = C_WARN;
      gps_txt = "GPS quiet";
    }
  }
  chip(x, row_sens, gps_txt, gps_col);
  x += chipWidth(gps_txt);
  if (sensorsSimulated()) {
    chip(x, row_sens, "SIM", C_WARN);
    x += chipWidth("SIM");
  }
  if (m.gsSession) {
    chip(x, row_sens, "GS AUTH", C_OK);
  } else if (m.gsModeEnabled) {
    chip(x, row_sens, "GS ON", C_WARN);
  }

  /* Last command */
  const int16_t row_cmd = 88;
  const bool fresh = have_cmd && (now - last_cmd_ms) < 1500;
  if (fresh) {
    canvas->fillRect(0, row_cmd - 3, LCD_W, 28, C_HIT);
  }
  canvas->setTextSize(1);
  canvas->setTextColor(fresh ? C_BG : C_DIM);
  canvas->setCursor(6, row_cmd);
  canvas->printf("LAST CMD   n=%lu", (unsigned long)cmd_count);
  canvas->setTextSize(2);
  canvas->setTextColor(fresh ? C_BG : C_TEXT);
  canvas->setCursor(6, row_cmd + 8);
  if (have_cmd) {
    canvas->printf("%-9s", tcName(last_apid));
    canvas->setTextSize(1);
    canvas->setCursor(130, row_cmd + 12);
    canvas->printf("%s %lus ago", last_source == 1 ? "USB" : "RF",
                   (now - last_cmd_ms) / 1000);
  } else {
    canvas->print("waiting...");
  }

  /* Telemetry + environment */
  const int16_t row_low = 116;
  canvas->setTextSize(1);
  canvas->setTextColor(C_DIM);
  canvas->setCursor(6, row_low);
  canvas->printf("TM %lu", (unsigned long)tx_count);
  if (sflags & MISSION_FLAG_BME_OK) {
    float t, p, alt, h;
    if (bmeRead(&t, &p, &alt, &h)) {
      canvas->setCursor(96, row_low);
      if (p < 1.0f) {
        canvas->printf("%.1fC  vacuum", t);
      } else {
        canvas->printf("%.1fC %.0fhPa", t, p);
      }
    }
  }

  /* Position */
  canvas->setCursor(6, 126);
  if (gps_ok && nav.hasFix) {
    const double lat = nav.latitudeE7 / 1e7;
    const double lon = nav.longitudeE7 / 1e7;
    const double alt_m = nav.altitudeCm / 100.0;
    canvas->setTextColor(C_TEXT);
    if (alt_m >= 10000.0) {
      canvas->printf("%.3f%c %.3f%c  alt %.0fkm", fabs(lat), lat >= 0 ? 'N' : 'S',
                     fabs(lon), lon >= 0 ? 'E' : 'W', alt_m / 1000.0);
    } else {
      canvas->printf("%.4f%c %.4f%c  %.0fm", fabs(lat), lat >= 0 ? 'N' : 'S',
                     fabs(lon), lon >= 0 ? 'E' : 'W', alt_m);
    }
  } else {
    canvas->print("position: no fix");
  }

  lcdPush();
}

#endif /* FLATSAT_LCD_GEEK */
