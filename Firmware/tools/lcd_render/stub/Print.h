/* Host stub of Arduino Print (print/printf for Adafruit_GFX).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
class Print {
public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t *b, size_t n) { size_t k = 0; while (n--) k += write(*b++); return k; }
  size_t write(const char *s) { return write((const uint8_t *)s, strlen(s)); }
  size_t print(const char *s) { return write(s); }
  size_t print(char c) { return write((uint8_t)c); }
  size_t print(int v) { char b[16]; snprintf(b, sizeof b, "%d", v); return write(b); }
  size_t print(unsigned v) { char b[16]; snprintf(b, sizeof b, "%u", v); return write(b); }
  size_t print(long v) { char b[24]; snprintf(b, sizeof b, "%ld", v); return write(b); }
  size_t print(unsigned long v) { char b[24]; snprintf(b, sizeof b, "%lu", v); return write(b); }
  size_t print(double v, int d = 2) { char b[32]; snprintf(b, sizeof b, "%.*f", d, v); return write(b); }
  size_t printf(const char *f, ...) { char b[256]; va_list a; va_start(a, f); vsnprintf(b, sizeof b, f, a); va_end(a); return write(b); }
};
