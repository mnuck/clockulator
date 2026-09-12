# Clockulator

Two clocks, side by side: local time and UTC. Turn a dial and both move together,
so you can read a moment in both zones at once.

It answers four questions:

1. What time is it right now?
2. What time is it UTC right now?
3. When it is timeX here, what time is it UTC?
4. When it is timeX UTC, what time is it here?

Two builds, for different hardware. They share the idea, not the code.

## ClockSpin — ESP8266

The original. Two TM1637 4-digit displays, a rotary encoder, and a button.

- Local time on one display, UTC on the other
- The knob offsets both, ten minutes per detent
- Press the knob, or leave it ten seconds, to return to the present
- Hold the knob while powering on to re-run the WiFi config portal

Libraries: WiFiManager, NTPClient, Time, Timezone, ESPRotary, Button2, Grove
4-Digit Display.

## Clockulator — ESP32-S3

A port to a Waveshare ESP32-S3-LCD-1.3: one 240x240 ST7789 panel viewed through
a prism, and a QMI8658 6-axis IMU. No knob, no buttons, so every control is a
gesture.

- UTC on the top half of the panel, local time below
- **Tilt to set time**, the way a kitchen timer has an H button and an M button:
  a shallow tilt (~30 degrees) steps minutes, a deep tilt (~60 degrees) steps
  hours, and which way you tilt decides forward or back. A quick tilt-and-return
  moves exactly one unit; holding keeps stepping and accelerates the longer it
  is held
- **Tap it flat on the desk** to return to the present
- **Tap its left or right edge on the desk** to change timezone (see `zones.h`)
- Ten seconds untouched also returns to the present
- The digits carry a slow colour shimmer keyed to the time of day

Timezone selection persists across reboots.

Libraries: TFT_eSPI, SensorLib, and WiFiManager for the home build only. NTP and
DST come from the ESP32 core's own `configTzTime()` and POSIX timezone strings,
which replaces the NTPClient/Time/Timezone stack the ESP8266 build needs.

### Home and company builds

The two builds differ only in networking (`net.cpp`). The build mode is a
compile-time choice, and the code refuses to build without one.

**Home** uses WiFiManager. On first boot it opens a setup access point named
`Clockulator`; join it and pick your network. It stays connected and syncs NTP
every minute. **Hold it face-down for three seconds** to re-open the portal.

**Company** takes fixed credentials from `Clockulator/wifi_config.h` (copy
`wifi_config.h.example`; the real file is gitignored) and contains no portal at
all. The radio is off except for a daily time sync, so between syncs the device
is not on the network. On first boot it erases any credentials a home build
left in flash.

Company devices are built to run as a fleet. Each one syncs once a day at its
own UTC time, derived from a hash of its MAC address and anchored to the wall
clock, so a thousand devices spread their syncs evenly across the day and a
building-wide power cut cannot line them up. After power-up a device waits a
per-device delay of up to two minutes before its first sync, so a mass reboot
reaches the access point and NTP gradually. Failed syncs retry with jittered
exponential backoff.

Why the company build leaves WiFiManager out entirely: its setup portal is an
open access point that accepts firmware uploads and erases WiFi settings with no
authentication (`/u`, `/update`, and `/erase` are registered unconditionally),
and `autoConnect()` falls back to that portal whenever the saved network is
unreachable at boot. Anyone within radio range of a rebooting device could
reflash it. The company firmware contains none of that code.

Bandwidth is negligible either way. One NTP exchange is 152 bytes at the IP
layer: about 220 KB/day in the home build, and a few hundred bytes a day in the
company build. The company build logs the crystal's measured drift at each sync.

### Building

Board settings are recorded in `Clockulator/sketch.yaml`, which both
`arduino-cli` and Arduino IDE 2.x read, so nothing needs setting in the Tools
menu. `PartitionScheme=huge_app` is the one to keep an eye on: the firmware is
about 1.1 MB, and the default scheme's app slot is only about 1.3 MB.

    tools/flash.sh home       # build and upload the home build
    tools/flash.sh company    # ...or the company build (needs wifi_config.h)
    tools/cap.py 20           # reset the board and capture 20s of output from boot

Both find `arduino-cli` (including the copy bundled inside Arduino IDE 2.x) and
the board's serial port by themselves; pass a port or set `CLOCKULATOR_PORT` to
override. Close the IDE's Serial Monitor before running `cap.py`.

A plain Arduino IDE build fails on purpose, since it has no way to pass the
mode. Use `flash.sh`, or add `-DCLOCKULATOR_MODE_HOME` (or `_COMPANY`) to the
build flags yourself.

### Notes for this hardware

The sensor and display constants in `gestures.cpp` were measured on the device
rather than taken from datasheets, and two of them are worth knowing about:

- **Thresholds are multiples of measured gravity, never absolute g.** The
  QMI8658 does not reliably honour the accelerometer range it is configured
  with -- asking for +-8g has been observed reading half scale, varying with
  what state the chip was left in. Ratios are immune to this; absolute
  thresholds are not.
- **Tilt is read about the X axis, taps are classified by impulse direction.**
  Striking an edge on the desk registers 7-9g on X (sign gives the side) while a
  flat tap registers on Z. Tapping the side *in situ*, without lifting the
  device, is too weak to separate from handling noise.

TFT_eSPI needs two fixes for the ESP32-S3 that live in the library, not here:
`USE_FSPI_PORT` in `User_Setup.h`, and dropping the S3 from the MISO-to-MOSI
aliasing block in `Processors/TFT_eSPI_ESP32_S3.h`. Without the first,
`tft.begin()` crashes with StoreProhibited; without the second, it hangs for
ever on the SPI busy flag.
