/*  - lcd_status.h
 *
 * Status screen for the Waveshare RP2350-GEEK (1.14" 240x135 LCD).
 * Compiled in only with -DFLATSAT_LCD_GEEK=1; otherwise every call is a no-op.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef FIRMWARE_LCD_STATUS_H
#define FIRMWARE_LCD_STATUS_H
#include <Arduino.h>

#if FLATSAT_LCD_GEEK

typedef struct {
  uint8_t mode;
  bool payloadArmed;
  bool usbDebug;
  bool gsModeEnabled;
  bool gsSession;
} lcd_mission_t;

/* Defined in worker.cpp (mission_ctx is file-static there). */
void lcdMissionSnapshot(lcd_mission_t *snapshot);

void lcdStatusBegin(void);
void lcdStatusBoot(const char *message);
/* Call repeatedly while blocked during boot: handles the BOOT button. */
void lcdStatusBootIdle(void);
void lcdStatusWorker(void);
void lcdStatusNoteCommand(uint16_t apid, uint8_t source);
void lcdStatusNoteTx(void);

#else

static inline void lcdStatusBegin(void) {}
static inline void lcdStatusBoot(const char *) {}
static inline void lcdStatusBootIdle(void) {}
static inline void lcdStatusWorker(void) {}
static inline void lcdStatusNoteCommand(uint16_t, uint8_t) {}
static inline void lcdStatusNoteTx(void) {}

#endif
#endif // FIRMWARE_LCD_STATUS_H
