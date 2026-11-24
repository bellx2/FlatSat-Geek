/* Host stub of the RP2350 POWMAN registers.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include <stdint.h>
struct powman_hw_t { uint32_t chip_reset; };
extern powman_hw_t *powman_hw;
