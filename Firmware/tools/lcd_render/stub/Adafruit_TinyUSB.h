/* Host stub of the TinyUSB CDC class.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
struct Adafruit_USBD_CDC { bool on = true; explicit operator bool() const { return on; } };
