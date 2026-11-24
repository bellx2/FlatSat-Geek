/* Host stub of the Pico SDK SPI API. spi_write16_blocking() is
 * implemented by render.cpp to capture the frame pushed to the panel.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef struct { int dummy; } spi_inst_t;
extern spi_inst_t *spi1;
enum { SPI_CPOL_0 = 0, SPI_CPHA_0 = 0, SPI_MSB_FIRST = 1 };
static inline void spi_init(spi_inst_t *, unsigned) {}
static inline void spi_set_format(spi_inst_t *, int, int, int, int) {}
static inline void spi_write_blocking(spi_inst_t *, const uint8_t *, size_t) {}
void spi_write16_blocking(spi_inst_t *, const uint16_t *buf, size_t n);
