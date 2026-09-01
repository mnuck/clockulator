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
- **Hold it face-down for three seconds** to open the WiFi config portal
- Ten seconds untouched also returns to the present
- The digits carry a slow colour shimmer keyed to the time of day

Timezone selection persists across reboots. Credentials come from WiFiManager's
captive portal and live in NVS, so there is no secrets file.

Libraries: WiFiManager, TFT_eSPI, SensorLib. NTP and DST come from the ESP32
core's own `configTzTime()` and POSIX timezone strings, which replaces the
NTPClient/Time/Timezone stack the ESP8266 build needs.

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
