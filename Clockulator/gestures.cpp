#include "gestures.h"
#include <Wire.h>
#include <math.h>
#include "SensorQMI8658.hpp"

#define I2C_SDA 47
#define I2C_SCL 48

// ---------------------------------------------------------------------------
// Tuning. Measured on this board, not taken from a datasheet.
//
// Thresholds are multiples of the measured resting gravity. See gestures.h for
// why absolute g values are not trustworthy on this part.
// ---------------------------------------------------------------------------

// Tilt is read about the X axis (the ax/az plane). Measured: a deliberate tilt
// swings this +-50 deg while Y moves under 9 deg. Forward reads positive.
static const int8_t TILT_SIGN = +1;

// Neutral noise measured under 0.5 deg, handling transients under 4 deg.
static const float TILT_DEADBAND_DEG = 5.0f;

// Tap. Measured, normalised by resting gravity: striking an edge on the desk
// gives 7-9, handling and pickup noise stays under 1.9. 3.5 sits between them.
static const float TAP_TRIGGER_REL = 3.5f;
static const uint32_t TAP_REFRACTORY_MS = 400;
static const uint32_t TAP_FOLLOW_MS = 60;   // track the impact to find its true peak

// Direction, from the impulse vector minus gravity:
//   left edge  -> X about -8.8    right edge -> X about +8.4
//   flat desk  -> Z about +7.4    (X near zero)
// So the dominant axis names the gesture and the sign of X names the side.

static const uint32_t NEUTRAL_SETTLE_MS = 600;
static const float ACCEL_EMA = 0.15f;   // ~100ms smoothing at ~370Hz polling

static SensorQMI8658 QMI;
static bool ready = false;

static float gx0 = 0, gy0 = 0, gz0 = 1, gMag = 1.0f;   // resting gravity
static float fax = 0, fay = 0, faz = 1;                  // smoothed
static float neutralAngle = 0.0f;
static bool haveNeutral = false;

static float curTilt = 0.0f;
static TapKind pendingTap = TAP_NONE;
static uint32_t lastTapMs = 0;
static uint32_t relearnNeutralAt = 0;
static uint32_t faceDownSince = 0;

bool gesturesReady() { return ready; }
float tiltDeg() { return curTilt; }

// Average gravity while the device sits still, and take that as level. Also
// records |g|, which every threshold is scaled against.
static void captureNeutral() {
  float sx = 0, sy = 0, sz = 0;
  int n = 0;
  uint32_t until = millis() + 400;
  while (millis() < until) {
    float ax, ay, az;
    if (QMI.getDataReady() && QMI.getAccelerometer(ax, ay, az)) { sx += ax; sy += ay; sz += az; n++; }
  }
  if (!n) return;
  gx0 = sx / n; gy0 = sy / n; gz0 = sz / n;
  float m = sqrtf(gx0*gx0 + gy0*gy0 + gz0*gz0);
  gMag = (m > 0.05f) ? m : 1.0f;
  neutralAngle = atan2f(gx0, gz0);
  fax = gx0; fay = gy0; faz = gz0;
  haveNeutral = true;
}

bool gesturesBegin() {
  Wire.begin(I2C_SDA, I2C_SCL);

  // A cold power-up can leave the QMI8658 not yet answering; one failed read is
  // not a reason to give up on it.
  // A cold power-up regularly leaves the QMI8658 not yet answering, and a short
  // retry burst is not always enough -- observed failing all of 5 x 200ms, which
  // leaves the device with no gestures at all until it is power-cycled again.
  for (int attempt = 1; attempt <= 12 && !ready; attempt++) {
    ready = QMI.begin(Wire, QMI8658_L_SLAVE_ADDRESS, I2C_SDA, I2C_SCL);
    if (!ready) {
      Serial.printf("gestures: QMI8658 attempt %d failed\n", attempt);
      delay(250);
      Wire.end();
      delay(50);
      Wire.begin(I2C_SDA, I2C_SCL);   // re-init the bus, not just the driver
    }
  }
  if (!ready) return false;

  // Widest range, for headroom on the 7-9g edge strikes. The configured range
  // is not reliably honoured, which is why nothing downstream trusts its scale.
  QMI.configAccelerometer(SensorQMI8658::ACC_RANGE_16G, SensorQMI8658::ACC_ODR_1000Hz,
                          SensorQMI8658::LPF_MODE_0, true);
  QMI.enableAccelerometer();

  captureNeutral();
  return true;
}

// Follow an impact briefly and classify it by its dominant axis. Blocks for
// TAP_FOLLOW_MS, which is imperceptible and only happens on a real strike.
static TapKind classifyImpact(float ax, float ay, float az, float amag) {
  float bx = ax, by = ay, bz = az, best = amag;
  uint32_t until = millis() + TAP_FOLLOW_MS;
  while (millis() < until) {
    float x, y, z;
    if (QMI.getDataReady() && QMI.getAccelerometer(x, y, z)) {
      float m = sqrtf(x*x + y*y + z*z);
      if (m > best) { best = m; bx = x; by = y; bz = z; }
    }
  }
  (void)by;

  float dx = (bx - gx0) / gMag;
  float dz = (bz - gz0) / gMag;

  if (fabsf(dx) > fabsf(dz)) return (dx < 0) ? TAP_LEFT : TAP_RIGHT;
  return TAP_DESK;
}

void gesturesPoll() {
  if (!ready || !QMI.getDataReady()) return;

  float ax, ay, az;
  if (!QMI.getAccelerometer(ax, ay, az)) return;

  uint32_t now = millis();
  float amag = sqrtf(ax*ax + ay*ay + az*az);

  // --- tap ---------------------------------------------------------------
  if (amag > TAP_TRIGGER_REL * gMag && now - lastTapMs > TAP_REFRACTORY_MS) {
    TapKind kind = classifyImpact(ax, ay, az, amag);
    lastTapMs = millis();
    pendingTap = kind;
    // A flat desk tap means the device is sitting where it belongs, so re-learn
    // level once the impact settles. An edge strike happens in the hand, so the
    // orientation then is meaningless -- do not re-learn from it.
    if (kind == TAP_DESK) relearnNeutralAt = lastTapMs + NEUTRAL_SETTLE_MS;
    return;
  }

  if (relearnNeutralAt && now >= relearnNeutralAt) {
    relearnNeutralAt = 0;
    captureNeutral();
    curTilt = 0.0f;
    return;
  }

  // --- tilt --------------------------------------------------------------
  fax += (ax - fax) * ACCEL_EMA;
  fay += (ay - fay) * ACCEL_EMA;
  faz += (az - faz) * ACCEL_EMA;
  if (!haveNeutral) return;

  float a = atan2f(fax, faz) - neutralAngle;
  while (a >  (float)M_PI) a -= 2.0f * (float)M_PI;
  while (a < -(float)M_PI) a += 2.0f * (float)M_PI;

  float deg = TILT_SIGN * a * 180.0f / (float)M_PI;

  // Past vertical the reading is not a control input any more -- flipping the
  // device over reads +-175 deg, which would otherwise scroll time wildly.
  if (fabsf(deg) > 90.0f) curTilt = 0.0f;
  else curTilt = (fabsf(deg) < TILT_DEADBAND_DEG) ? 0.0f : deg;

  float m = sqrtf(fax*fax + fay*fay + faz*faz);
  bool down = (m > 0.05f) && (faz / m < -0.5f);
  if (down) { if (!faceDownSince) faceDownSince = millis(); }
  else faceDownSince = 0;
}

uint32_t faceDownHeldMs() {
  return faceDownSince ? (millis() - faceDownSince) : 0;
}

float gravityZRatio() {
  float m = sqrtf(fax*fax + fay*fay + faz*faz);
  return (m > 0.05f) ? faz / m : 0.0f;
}

bool screenFacingDown() {
  if (!ready) return false;
  float sz = 0, mag = 0;
  int n = 0;
  uint32_t until = millis() + 250;
  while (millis() < until) {
    float ax, ay, az;
    if (QMI.getDataReady() && QMI.getAccelerometer(ax, ay, az)) {
      sz += az;
      mag += sqrtf(ax*ax + ay*ay + az*az);
      n++;
    }
  }
  if (!n || mag <= 0) return false;
  float ratio = (sz / n) / (mag / n);
  Serial.printf("orientation: az/|a| = %+.2f (face-down below -0.50), n=%d\n", ratio, n);
  return ratio < -0.5f;   // gravity pointing out through the screen
}

TapKind takeTap() {
  TapKind t = pendingTap;
  pendingTap = TAP_NONE;
  return t;
}
