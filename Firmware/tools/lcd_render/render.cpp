/*  - render.cpp
 *
 * Renders the three LCD pages of the RP2350-GEEK build on the host by compiling
 * the real lcd_status.cpp against small stubs (stub/) and capturing the frame
 * that lcdPush() sends to the panel. The mission/sensor state below is a fixed
 * example used for the README screenshots.
 *
 * Build and run with render_screens.sh.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "lcd_status.cpp"
#include <string>
#include <vector>
#include <zlib.h>
// Same values as the COURSE_GPS_* defaults in sensors.cpp (file-local there).
#define COURSE_GPS_LAT_E7 357122000L
#define COURSE_GPS_LON_E7 1397038000L
#define COURSE_GPS_ALT_CM 3500L

FakeSerial Serial;
FakeRp2040 rp2040;
Adafruit_USBD_CDC USBRadioLink;
static spi_inst_t spi1_inst;
spi_inst_t *spi1 = &spi1_inst;
static powman_hw_t powman_inst = {POWMAN_CHIP_RESET_HAD_POR_BITS};
powman_hw_t *powman_hw = &powman_inst;
static watchdog_hw_t watchdog_inst = {};
watchdog_hw_t *watchdog_hw = &watchdog_inst;

static unsigned long now_ms = 0;
unsigned long millis(void) { return now_ms; }

static lcd_mission_t mstate = {MISSION_MODE_NOMINAL, false, false, true, false};
void lcdMissionSnapshot(lcd_mission_t *s) { *s = mstate; }
uint8_t sensorsStatusFlags(void) { return MISSION_FLAG_BME_OK | MISSION_FLAG_ACC_OK; }
bool sensorsSimulated(void) { return true; }
bool gpsOverrideActive(void) { return true; }
bool bmeRead(float *t, float *p, float *alt, float *h) { *t = -12.3f; *p = 0.0f; *alt = 0; *h = 0; return true; }
bool gpsRead(gps_nav_t *n) {
  memset(n, 0, sizeof *n);
  n->uartOk = n->nmeaActive = n->hasFix = true;
  // Boot-time GPS override position (COURSE_GPS_* in sensors.cpp)
  n->latitudeE7 = COURSE_GPS_LAT_E7; n->longitudeE7 = COURSE_GPS_LON_E7; n->altitudeCm = COURSE_GPS_ALT_CM;
  return true;
}

static std::string out_path;
static const int SCALE = 3;

static void putBE32(std::vector<uint8_t> &v, uint32_t x) {
  for (int i = 3; i >= 0; i--) v.push_back((uint8_t)(x >> (8 * i)));
}

static void pngChunk(FILE *f, const char *type, const std::vector<uint8_t> &data) {
  std::vector<uint8_t> c;
  putBE32(c, (uint32_t)data.size());
  c.insert(c.end(), type, type + 4);
  c.insert(c.end(), data.begin(), data.end());
  putBE32(c, (uint32_t)crc32(0, c.data() + 4, (uInt)(c.size() - 4)));
  fwrite(c.data(), 1, c.size(), f);
}

// Captures the frame lcdPush() sends and writes it as a PNG, scaled up SCALE x.
void spi_write16_blocking(spi_inst_t *, const uint16_t *buf, size_t n) {
  if (out_path.empty() || n != (size_t)LCD_W * LCD_H) return;
  const int W = LCD_W * SCALE, H = LCD_H * SCALE;
  std::vector<uint8_t> raw;
  raw.reserve((size_t)(W * 3 + 1) * H);
  for (int y = 0; y < H; y++) {
    raw.push_back(0); // filter: none
    for (int x = 0; x < W; x++) {
      const uint16_t c = buf[(y / SCALE) * LCD_W + x / SCALE];
      raw.push_back((uint8_t)(((c >> 11) & 0x1F) * 255 / 31));
      raw.push_back((uint8_t)(((c >> 5) & 0x3F) * 255 / 63));
      raw.push_back((uint8_t)((c & 0x1F) * 255 / 31));
    }
  }
  uLongf zlen = compressBound((uLong)raw.size());
  std::vector<uint8_t> z(zlen);
  compress2(z.data(), &zlen, raw.data(), (uLong)raw.size(), Z_BEST_COMPRESSION);
  z.resize(zlen);

  std::vector<uint8_t> ihdr;
  putBE32(ihdr, W);
  putBE32(ihdr, H);
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0}); // 8-bit RGB
  FILE *f = fopen(out_path.c_str(), "wb");
  if (f == NULL) {
    perror(out_path.c_str());
    exit(1);
  }
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  fwrite(sig, 1, 8, f);
  pngChunk(f, "IHDR", ihdr);
  pngChunk(f, "IDAT", z);
  pngChunk(f, "IEND", {});
  fclose(f);
  printf("wrote %s\n", out_path.c_str());
  out_path.clear();
}

static void shot(uint8_t p, const char *path) {
  page = p;
  last_draw_ms = 0;
  out_path = path;
  lcdStatusWorker();
}

int main(int argc, char **argv) {
  const std::string dir = argc > 1 ? argv[1] : ".";
  lcdStatusBegin();
  // An attack that has got through recon, a mode change and a flash read
  now_ms = 120000;
  for (int i = 0; i < 9; i++) lcdStatusNoteCommand(SPP_APID_TC_PING, 1);
  lcdStatusNoteCommand(SPP_APID_TC_SET_MISSION_MODE, 1);
  lcdStatusNoteCommand(SPP_APID_TC_FLASH_READ, 1);
  now_ms = 320000;
  lcdStatusNoteCommand(SPP_APID_TC_GET_STATUS, 1);
  for (int i = 0; i < 42; i++) lcdStatusNoteTx();
  now_ms = 323000;
  shot(0, (dir + "/screen-status.png").c_str());
  shot(1, (dir + "/screen-scoreboard.png").c_str());
  shot(2, (dir + "/screen-system.png").c_str());
  return 0;
}
