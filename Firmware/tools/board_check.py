#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial>=3.5"]
# ///
#
# board_check.py - functional check for a StickSat board.
#
# Talks to the board over USB only and checks the things the operator cannot
# see on the screen: USB identity and version, command/telemetry round trips,
# the IDS event channel, periodic telemetry, and the factory-fresh state
# (NOMINAL mode, AES/debug off, GPS override at Tokyo, GS mode off).
# It sends read-only commands only; nothing on the board is changed.
#
#   tools/board_check.py                 # auto-detect the board
#   tools/board_check.py --serial fsat-XXXXXXXXXXXXXXXX
#   tools/board_check.py --link /dev/cu.usbmodemXXXX3 --console /dev/cu.usbmodemXXXX1
#
# Exit code 0 when every check passes, 1 otherwise.
#
# SPDX-License-Identifier: GPL-2.0-or-later

import argparse
import os
import re
import struct
import sys
import threading
import time

import serial
from serial.tools import list_ports

USB_VID = 0x239A
USB_PID = 0xCAFE

# Telecommand / telemetry APIDs (mission.h)
TC_PING = 0x01
TC_SEND_FW = 0x03
TC_GET_STATUS = 0x0C
TC_GET_NAV = 0x0E
TC_GET_PAYLOAD_STATUS = 0x0F
TC_GS_STATUS = 0x13
TM_PING = 0x01
TM_SEND_FW = 0x03
TM_SEND_TM = 0x08
TM_ERROR = 0x09
TM_STATUS = 0x0C
TM_NAV = 0x0E
TM_PAYLOAD_STATUS = 0x0F
TM_GS_STATUS = 0x13
TM_IDS_EVENT = 0x20

FLAG_BME_OK = 0x01
FLAG_ACC_OK = 0x02
FLAG_SECURE_LINK = 0x10
FLAG_PAYLOAD_ARMED = 0x20
FLAG_USB_DEBUG = 0x40
GPS_FIX_VALID = 0x08
GS_MODE_ENABLED = 0x01
GS_WITHIN_RANGE = 0x04

MODE_NAMES = {0: "SAFE", 1: "NOMINAL", 2: "PAYLOAD", 3: "SCIENCE", 4: "CONTINGENCY"}

# Expected values for a freshly booted board (sensors.cpp / worker.cpp).
PROTOCOL_VERSION = (1, 0, 5)
BOOT_LAT, BOOT_LON = 35.7122, 139.7038  # GPS override at boot (Tokyo)
GS_LAT, GS_LON, GS_RADIUS_M = 43.0618, 141.3545, 35000  # Sapporo
TM_PERIOD_S = 14  # radio_tm_interval_ms

REPLY_TIMEOUT_S = 1.0
PING_LATENCY_LIMIT_S = 0.2


def firmware_version():
    """FW_VERSION from version.h next to this script's parent directory."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "version.h")
    try:
        with open(path) as f:
            m = re.search(r'#define\s+FW_VERSION\s+"([^"]+)"', f.read())
            return m.group(1) if m else None
    except OSError:
        return None


class Link:
    """Binary SPP link: TC frames are AA 55 <len BE16> <SPP>, TM frames 0xAA <SPP>."""

    def __init__(self, port):
        self.ser = serial.Serial(port, 921600, timeout=0.05)
        self.ser.dtr = True
        self.buf = bytearray()
        self.seq = 0
        self.received = []  # (time, apid, payload) of every TM frame seen

    def close(self):
        self.ser.close()

    def send(self, apid, payload=b""):
        if not payload:
            payload = b"\x00"  # SPP data field is at least one octet
        ident = (1 << 12) | (apid & 0x7FF)  # version 0, TC, no secondary header
        self.seq = (self.seq + 1) & 0x3FFF
        spp = struct.pack(">HHH", ident, (0b11 << 14) | self.seq, len(payload) - 1) + payload
        self.ser.write(b"\xAA\x55" + struct.pack(">H", len(spp)) + spp)
        self.ser.flush()

    def _parse(self):
        frames = []
        while True:
            start = self.buf.find(b"\xAA")
            if start < 0:
                self.buf.clear()
                return frames
            del self.buf[:start]
            if len(self.buf) < 7:
                return frames
            ident, _seq, length = struct.unpack(">HHH", self.buf[1:7])
            total = 7 + length + 1
            if (ident >> 13) != 0 or length > 255:
                del self.buf[:1]  # not a frame start, resync
                continue
            if len(self.buf) < total:
                return frames
            apid = ident & 0x7FF
            payload = bytes(self.buf[7:total])
            del self.buf[:total]
            frames.append((time.monotonic(), apid, payload))

    def poll(self):
        data = self.ser.read(4096)
        if data:
            self.buf += data
        frames = self._parse()
        self.received += frames
        return frames

    def wait_for(self, apids, timeout, match=None):
        """First TM frame whose APID is in apids (and passes match), or None."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for frame in self.poll():
                if frame[1] in apids and (match is None or match(frame)):
                    return frame
        return None

    def drain(self, duration=0.3):
        end = time.monotonic() + duration
        while time.monotonic() < end:
            self.poll()

    def request(self, tc, tm, payload=b""):
        """Send a TC and wait for its TM (or TM_ERROR). Returns (frame, latency)."""
        self.poll()
        t0 = time.monotonic()
        self.send(tc, payload)
        # TM APID 0x01 also carries the periodic sync/beacon frames; only ACK answers a PING.
        match = (lambda f: f[1] != TM_PING or f[2][1:4] == b"ACK") if tm == TM_PING else None
        frame = self.wait_for({tm, TM_ERROR}, REPLY_TIMEOUT_S, match)
        return frame, (frame[0] - t0) if frame else None


class ConsoleDrain(threading.Thread):
    """Keeps the console port open (the board waits for it at boot) and reads it."""

    def __init__(self, port):
        super().__init__(daemon=True)
        self.ser = serial.Serial(port, 921600, timeout=0.1)
        self.ser.dtr = True
        self.text = bytearray()
        self.stop = False

    def run(self):
        while not self.stop:
            try:
                self.text += self.ser.read(1024)
            except serial.SerialException:
                return

    def close(self):
        self.stop = True
        self.join(1)
        self.ser.close()


class Report:
    def __init__(self):
        self.failed = 0

    def check(self, name, ok, detail=""):
        print("  [%s] %s%s" % ("PASS" if ok else "FAIL", name, ("  - " + detail) if detail else ""))
        if not ok:
            self.failed += 1
        return ok


def find_board(args):
    """Returns (console_port, link_port, usb_info) for the board to test."""
    ports = [p for p in list_ports.comports() if p.vid == USB_VID and p.pid == USB_PID]
    if args.serial:
        ports = [p for p in ports if p.serial_number == args.serial]
    by_serial = {}
    for p in ports:
        by_serial.setdefault(p.serial_number, []).append(p)
    if args.link:
        info = next((p for p in ports if p.device == args.link), None)
        return args.console, args.link, info
    if not by_serial:
        sys.exit("No StickSat board found (USB %04X:%04X)." % (USB_VID, USB_PID))
    if len(by_serial) > 1:
        print("Several boards found; pick one with --serial:")
        for sn in sorted(by_serial):
            print("  " + sn)
        sys.exit(1)
    pair = sorted(next(iter(by_serial.values())), key=lambda p: (p.location or "", p.device))
    if len(pair) < 2:
        sys.exit("Only one CDC port found for the board; give --console and --link.")
    # Interface 0 is the console, interface 2 the binary link.
    return pair[0].device, pair[1].device, pair[1]


def le_i32(b, o):
    return struct.unpack_from("<i", b, o)[0]


def le_u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def le_u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def main():
    ap = argparse.ArgumentParser(description="Functional check for a StickSat board.")
    ap.add_argument("--serial", help="USB serial number of the board (fsat-<chip id>)")
    ap.add_argument("--link", help="binary link port (skips auto-detection)")
    ap.add_argument("--console", help="console port (used with --link)")
    ap.add_argument("--version", dest="expect_version", default=firmware_version(),
                    help="expected firmware version (default: FW_VERSION in version.h)")
    ap.add_argument("--pings", type=int, default=20, help="number of PINGs for the round-trip test")
    args = ap.parse_args()

    console_port, link_port, usb = find_board(args)
    r = Report()

    print("== USB")
    if usb is not None:
        print("  serial  %s" % usb.serial_number)
        print("  ports   console=%s link=%s" % (console_port, link_port))
        r.check("manufacturer is Pwnsat", usb.manufacturer == "Pwnsat", str(usb.manufacturer))
        product = usb.product or ""
        if args.expect_version:
            r.check("firmware version %s" % args.expect_version,
                    product.endswith("v" + args.expect_version), product)
        r.check("serial number fsat-<chip id>", (usb.serial_number or "").startswith("fsat-"),
                str(usb.serial_number))
    else:
        print("  (USB descriptors not available for %s; identity checks skipped)" % link_port)

    console = ConsoleDrain(console_port) if console_port else None
    if console:
        console.start()
    link = Link(link_port)
    try:
        time.sleep(0.5)  # let the board finish booting once the console is open
        link.drain()

        print("== Commands")
        frame, lat = link.request(TC_PING, TM_PING)
        if r.check("PING answered", frame is not None and frame[1] == TM_PING,
                   "%.0f ms" % (lat * 1000) if lat else "no reply"):
            r.check("PING reply is ACK", frame[2][1:4] == b"ACK", frame[2].hex())

        ev = link.wait_for({TM_IDS_EVENT}, REPLY_TIMEOUT_S)
        if ev is None:
            ev = next((f for f in reversed(link.received) if f[1] == TM_IDS_EVENT), None)
        if r.check("IDS event for the PING", ev is not None and len(ev[2]) == 15,
                   ev[2].hex() if ev else "none"):
            p = ev[2]
            r.check("IDS event fields (v1, USB, APID 0x01, OK, executed)",
                    p[0] == 1 and p[1] == 1 and (p[2] << 8 | p[3]) == TC_PING
                    and p[12] == 0 and (p[14] & 0x01) == 1, p.hex())

        frame, _ = link.request(TC_SEND_FW, TM_SEND_FW)
        if r.check("SEND_FW answered", frame is not None and frame[1] == TM_SEND_FW):
            ver = (frame[2][3], frame[2][2], frame[2][1])
            r.check("protocol version %d.%d.%d" % PROTOCOL_VERSION, ver == PROTOCOL_VERSION,
                    "%d.%d.%d" % ver)

        frame, _ = link.request(TC_GET_STATUS, TM_STATUS)
        if r.check("GET_STATUS answered", frame is not None and frame[1] == TM_STATUS
                   and len(frame[2]) >= 26):
            p = frame[2]
            mode, flags = p[1], p[2]
            r.check("mode NOMINAL", mode == 1, MODE_NAMES.get(mode, str(mode)))
            r.check("BME280 OK", bool(flags & FLAG_BME_OK))
            r.check("accelerometer OK", bool(flags & FLAG_ACC_OK))
            r.check("AES secure link off", not flags & FLAG_SECURE_LINK)
            r.check("payload not armed", not flags & FLAG_PAYLOAD_ARMED)
            r.check("USB debug off", not flags & FLAG_USB_DEBUG)
            print("  uptime  %d s" % le_u32(p, 12))

        frame, _ = link.request(TC_GET_NAV, TM_NAV)
        if r.check("GET_NAV answered", frame is not None and frame[1] == TM_NAV
                   and len(frame[2]) >= 15):
            p = frame[2]
            lat_deg, lon_deg = le_i32(p, 3) / 1e7, le_i32(p, 7) / 1e7
            r.check("GPS fix", bool(p[1] & GPS_FIX_VALID), "status 0x%02X" % p[1])
            r.check("boot position (Tokyo override)",
                    abs(lat_deg - BOOT_LAT) < 1e-3 and abs(lon_deg - BOOT_LON) < 1e-3,
                    "%.4f, %.4f" % (lat_deg, lon_deg))

        frame, _ = link.request(TC_GET_PAYLOAD_STATUS, TM_PAYLOAD_STATUS)
        r.check("GET_PAYLOAD_STATUS answered", frame is not None and frame[1] == TM_PAYLOAD_STATUS)

        frame, _ = link.request(TC_GS_STATUS, TM_GS_STATUS)
        if r.check("GS_STATUS answered", frame is not None and frame[1] == TM_GS_STATUS
                   and len(frame[2]) >= 23):
            p = frame[2]
            r.check("GS mode off", not p[1] & GS_MODE_ENABLED, "flags 0x%02X" % p[1])
            r.check("outside ground-station range", not p[1] & GS_WITHIN_RANGE,
                    "distance %s" % ("> 65 km" if le_u16(p, 3) == 0xFFFF else "%d m" % le_u16(p, 3)))
            st_lat, st_lon, radius = le_i32(p, 13) / 1e7, le_i32(p, 17) / 1e7, le_u16(p, 21)
            r.check("ground station Sapporo, 35 km",
                    abs(st_lat - GS_LAT) < 1e-3 and abs(st_lon - GS_LON) < 1e-3
                    and radius == GS_RADIUS_M,
                    "%.4f, %.4f, %d m" % (st_lat, st_lon, radius))

        print("== Round trip")
        latencies = []
        for _ in range(args.pings):
            frame, lat = link.request(TC_PING, TM_PING)
            if frame is not None and frame[1] == TM_PING:
                latencies.append(lat)
        r.check("%d/%d PINGs answered" % (len(latencies), args.pings), len(latencies) == args.pings)
        if latencies:
            worst = max(latencies)
            r.check("worst PING latency under %d ms" % (PING_LATENCY_LIMIT_S * 1000),
                    worst < PING_LATENCY_LIMIT_S,
                    "avg %.0f ms, max %.0f ms" % (sum(latencies) / len(latencies) * 1000, worst * 1000))

        print("== Periodic telemetry")
        seen = any(f[1] == TM_SEND_TM for f in link.received)
        if not seen:
            print("  waiting up to %d s for a telemetry frame..." % (TM_PERIOD_S + 2))
            seen = link.wait_for({TM_SEND_TM}, TM_PERIOD_S + 2) is not None
        r.check("telemetry frame (APID 0x08) received", seen)
    finally:
        link.close()
        if console:
            console.close()

    commands = 6 + args.pings  # PING, SEND_FW, GET_STATUS, GET_NAV, PL_STAT, GS_STAT + PINGs
    print()
    print("Screen (check by eye, first run since power-on): Status page LAST CMD = PING, n=%d;" % commands)
    print("  scoreboard RECON lit, SCORE 1/8.")
    print("Unplug and replug the board to clear the counters.")
    print()
    if r.failed:
        print("RESULT: FAIL (%d check%s failed)" % (r.failed, "" if r.failed == 1 else "s"))
        return 1
    print("RESULT: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
