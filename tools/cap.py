#!/usr/bin/env python3
"""Reset the Clockulator board and capture its serial output from boot.

Why this exists: on macOS, opening /dev/cu.* does not reset an ESP32, so a
serial monitor attached after power-up misses everything printed during boot,
which is exactly where most failures show up. This opens the port, pulses the
reset line the way esptool's auto-reset does, throws away stale buffered bytes
so output from an earlier boot cannot interleave, and records what the board
prints.

Close the Arduino IDE's Serial Monitor first. It holds the port exclusively.

No pyserial needed: termios and ioctl are enough.

Usage: tools/cap.py [seconds] [port]      (or set CLOCKULATOR_PORT)
"""
import fcntl
import glob
import os
import select
import struct
import sys
import termios
import time

# Same preference as flash.sh: the CH340 bridge carries Serial, native USB does not.
PORT_PATTERNS = ["/dev/cu.wchusbserial*", "/dev/cu.usbserial*", "/dev/ttyUSB*",
                 "/dev/cu.usbmodem*", "/dev/ttyACM*"]


def find_port():
    for pattern in PORT_PATTERNS:
        hits = sorted(glob.glob(pattern))
        if hits:
            return hits[0]
    return None


def set_lines(fd, set_bits, clear_bits):
    cur = struct.unpack("I", fcntl.ioctl(fd, termios.TIOCMGET, struct.pack("I", 0)))[0]
    fcntl.ioctl(fd, termios.TIOCMSET, struct.pack("I", (cur | set_bits) & ~clear_bits))


def main():
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 10.0
    port = sys.argv[2] if len(sys.argv) > 2 else os.environ.get("CLOCKULATOR_PORT") or find_port()
    if not port:
        sys.exit("no serial port found: pass one, or set CLOCKULATOR_PORT")

    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        attrs = termios.tcgetattr(fd)
        cflag = (attrs[2] | termios.CREAD | termios.CLOCAL | termios.CS8) \
            & ~(termios.PARENB | termios.CSTOPB | termios.CRTSCTS)
        attrs[6][termios.VMIN] = 0
        attrs[6][termios.VTIME] = 0
        termios.tcsetattr(fd, termios.TCSANOW,
                          [0, 0, cflag, 0, termios.B115200, termios.B115200, attrs[6]])

        # Auto-reset: hold DTR low so the chip boots normally rather than into
        # the bootloader, then pulse RTS, which drives EN.
        dtr, rts = termios.TIOCM_DTR, termios.TIOCM_RTS
        set_lines(fd, 0, dtr | rts)
        time.sleep(0.10)
        set_lines(fd, rts, 0)
        time.sleep(0.15)
        set_lines(fd, 0, rts)

        time.sleep(0.05)
        termios.tcflush(fd, termios.TCIFLUSH)

        out = b""
        end = time.time() + seconds
        while time.time() < end:
            ready, _, _ = select.select([fd], [], [], 0.2)
            if ready:
                try:
                    out += os.read(fd, 4096)
                except BlockingIOError:
                    pass
    finally:
        os.close(fd)

    sys.stdout.write(out.decode("utf-8", "replace"))


if __name__ == "__main__":
    main()
