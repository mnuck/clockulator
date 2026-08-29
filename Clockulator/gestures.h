#ifndef CLOCKULATOR_GESTURES_H
#define CLOCKULATOR_GESTURES_H

#include <Arduino.h>

// Tilt and tap sensing on the QMI8658.
//
// Tilt is measured against gravity, so it is an absolute reading -- no
// integration, no drift, and "level" always means level.
//
// All thresholds are expressed as multiples of the MEASURED resting gravity,
// never in absolute g. The chip does not reliably honour the accelerometer
// range it is configured with (asking for 8G has been observed reading half
// scale), so absolute thresholds cannot be trusted. Ratios are immune to it.

enum TapKind {
  TAP_NONE = 0,
  TAP_DESK,    // flat on the desk: reset to real time
  TAP_LEFT,    // left edge struck: previous timezone
  TAP_RIGHT,   // right edge struck: next timezone
};

bool  gesturesBegin();   // false if the IMU never answered
void  gesturesPoll();    // call from loop() as often as possible; never blocks
bool  gesturesReady();

// Signed tilt in degrees from the neutral (resting) orientation.
// Positive = tilted "forward". Zero inside the deadband, and zero while the
// device is being moved rather than held.
float tiltDeg();

// True if the panel is currently face-down. Used at boot as the "held button"
// this hardware does not have, to force the WiFi config portal.
bool screenFacingDown();

// Live az/|a|: +1 face-up, -1 face-down.
float gravityZRatio();

// How long the device has been continuously face-down, in ms (0 if it is not).
uint32_t faceDownHeldMs();

// Returns the pending tap and clears it.
TapKind takeTap();

#endif
