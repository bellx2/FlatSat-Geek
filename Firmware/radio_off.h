/*  - radio_off.h
 *
 * Course build: the two SX1262 modules are held in reset and never configured.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef FIRMWARE_RADIO_OFF_H
#define FIRMWARE_RADIO_OFF_H

#if FLATSAT_NO_RADIO

/* Drive both radios into reset and both antenna switches off, before anything
 * else runs. Call it first in setup().
 *
 * Compiling the radio calls out is not enough on its own: the chips would
 * still power up in their default state, and anything that reached RadioLib
 * later could key them. Holding RESET low keeps the SX1262 in reset for as
 * long as the board is powered, so it cannot modulate a carrier whatever the
 * firmware above it does. That is a property somebody can verify with a
 * spectrum analyser, which is the point. */
void radioHoldInReset(void);

/* What the version reply tells the ground, so the state is readable from the
 * console instead of taken on trust. */
#define FLATSAT_RADIO_STATE_STR "disabled (held in reset, course build)"

#else
#define FLATSAT_RADIO_STATE_STR "enabled"
#endif /* FLATSAT_NO_RADIO */

#endif // FIRMWARE_RADIO_OFF_H
