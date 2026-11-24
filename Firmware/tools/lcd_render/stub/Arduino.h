/* Host stub of the Arduino core: just enough for lcd_status.cpp.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <stdlib.h>
#define radians(d) ((d) * M_PI / 180.0)
#ifndef ARDUINO
#define ARDUINO 10800
#endif
#define PROGMEM
#define OUTPUT 1
#define HIGH 1
#define LOW 0
#define BOOTSEL false
#include "Print.h"
class __FlashStringHelper;
class String { const char *s = ""; public: unsigned int length() const { return strlen(s); } const char *c_str() const { return s; } };
unsigned long millis(void);
static inline void delay(unsigned long) {}
static inline void pinMode(int, int) {}
static inline void digitalWrite(int, int) {}
struct FakeSerial { bool on = true; explicit operator bool() const { return on; } };
extern FakeSerial Serial;
struct FakeRp2040 { unsigned long heap = 198432; unsigned long getFreeHeap() { return heap; } };
extern FakeRp2040 rp2040;
