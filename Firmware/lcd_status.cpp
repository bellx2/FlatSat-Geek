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
#include <Adafruit_GFX.h>
#include <Adafruit_TinyUSB.h>
#include "hardware/gpio.h"
#include "hardware/spi.h"

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
#define LCD_Y_OFFSET 53

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

static void lcdPush(void) {
  const uint16_t x0 = LCD_X_OFFSET, x1 = LCD_X_OFFSET + LCD_W - 1;
  const uint16_t y0 = LCD_Y_OFFSET, y1 = LCD_Y_OFFSET + LCD_H - 1;
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

void lcdStatusWorker(void) {
  if (!lcd_ready) {
    return;
  }
  const unsigned long now = millis();
  if (now - last_draw_ms < 500) {
    return;
  }
  last_draw_ms = now;

  lcd_mission_t m;
  lcdMissionSnapshot(&m);
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
  const unsigned long s = now / 1000;
  canvas->setTextSize(1);
  canvas->setCursor(LCD_W - 6 * 8 - 6, 6);
  canvas->printf("%02lu:%02lu:%02lu", (s / 3600) % 100, (s / 60) % 60, s % 60);

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
  const int16_t row_low = 118;
  canvas->setTextSize(1);
  canvas->setTextColor(C_DIM);
  canvas->setCursor(6, row_low);
  canvas->printf("TM sent %lu", (unsigned long)tx_count);
  if (sflags & MISSION_FLAG_BME_OK) {
    float t, p, alt, h;
    if (bmeRead(&t, &p, &alt, &h)) {
      canvas->setCursor(110, row_low);
      canvas->printf("%.1fC %.0fhPa", t, p);
    }
  }

  lcdPush();
}

#endif /* FLATSAT_LCD_GEEK */
