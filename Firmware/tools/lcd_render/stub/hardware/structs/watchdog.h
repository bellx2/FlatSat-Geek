/* Host stub of the Pico SDK watchdog registers (scratch only).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include <stdint.h>
struct watchdog_hw_t { uint32_t scratch[8]; };
extern watchdog_hw_t *watchdog_hw;
