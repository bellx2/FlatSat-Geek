/*  - radio_off.cpp
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "radio_off.h"

#if FLATSAT_NO_RADIO
#include "pins.h"
#include <Arduino.h>

void radioHoldInReset(void) {
#if FLATSAT_LCD_GEEK
  /* The GEEK boards have no radio hardware, and the LCD uses GP8-12 and GP25,\n   * which overlap this firmware's radio pin map. Drive nothing. */
  return;
#endif
  /* RESET low, and keep it low. The SX1262 stays in reset while the board is
   * powered, so neither module can transmit or receive. */
  pinMode(PIN_RADIO0_RST, OUTPUT);
  digitalWrite(PIN_RADIO0_RST, LOW);
  pinMode(PIN_RADIO1_RST, OUTPUT);
  digitalWrite(PIN_RADIO1_RST, LOW);

  /* Antenna switches off as well. Belt and braces: even a module that somehow
   * came out of reset would not reach the antenna. */
  pinMode(PIN_RADIO0_ANT_SW, OUTPUT);
  digitalWrite(PIN_RADIO0_ANT_SW, LOW);
  pinMode(PIN_RADIO1_ANT_SW, OUTPUT);
  digitalWrite(PIN_RADIO1_ANT_SW, LOW);

  /* Chip selects high (deselected), so nothing on the shared SPI bus talks to
   * a radio by accident. */
  pinMode(PIN_RADIO0_NSS, OUTPUT);
  digitalWrite(PIN_RADIO0_NSS, HIGH);
  pinMode(PIN_RADIO1_NSS, OUTPUT);
  digitalWrite(PIN_RADIO1_NSS, HIGH);
}
#endif /* FLATSAT_NO_RADIO */
