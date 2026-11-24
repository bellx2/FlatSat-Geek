/*  - version.h
 *
 * Firmware version shown on the LCD system page.
 *   FW_VERSION       edit by hand for releases
 *   FW_BUILD_NUMBER  git commit count (generated at build time)
 *   FW_BUILD_HASH    git short hash, '+' appended if the tree has local changes
 *   FW_BUILD_DATE    build date and time
 * The last three come from version_gen.h, written by tools/gen_version.py.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef FIRMWARE_VERSION_H
#define FIRMWARE_VERSION_H

#define FW_VERSION "1.3.0"

#if defined(__has_include)
#if __has_include("version_gen.h")
#include "version_gen.h"
#endif
#endif

#ifndef FW_BUILD_NUMBER
#define FW_BUILD_NUMBER 0
#endif
#ifndef FW_BUILD_HASH
#define FW_BUILD_HASH "unknown"
#endif
#ifndef FW_BUILD_DATE
#define FW_BUILD_DATE __DATE__ " " __TIME__
#endif

#endif // FIRMWARE_VERSION_H
