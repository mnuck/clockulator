/*
 * Clockulator, ported to the Waveshare ESP32-S3-LCD-1.3.
 *
 * Original: ESP8266 + two TM1637 4-digit displays + rotary encoder + button.
 * https://github.com/mnuck/clockulator
 *
 * Theory of Operation:
 *    Synchronises to pool.ntp.org and shows UTC on the top half of the panel
 *    and local time on the bottom. Twisting the device offsets both clocks to
 *    show time in the past or future; tapping it on the desk returns to the
 *    present, as does leaving it alone for ten seconds.
 *
 *    Twist clockwise for later, counter-clockwise for earlier. (Note this is
 *    the opposite of the original, where left rotation moved time forward.)
 *
 * But Why?
 *    Four common questions:
 *    1. What time is it right now?
 *    2. What time is it UTC right now?
 *    3. When it is timeX here, what time is it UTC?
 *    4. When it is timeX UTC, what time is it here?
 *
 * Hardware:
 *    ESP32-S3, 240x240 ST7789 panel, QMI8658 6-axis IMU.
 *    The rotary encoder is replaced by the gyroscope, the button by tap
 *    detection on the accelerometer. See gestures.cpp for the measured
 *    thresholds.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_sntp.h>
#include <time.h>

#include <Preferences.h>

#include "display.h"
#include "gestures.h"
#include "zones.h"

// The local zone is chosen by tapping the device's left/right edge on the desk;
// see zones.h. POSIX rules replace the original's NTPClient + Timezone
// libraries and hand-written TimeChangeRules; %Z yields the DST label on its own.
static const char *NTP_SERVER = "pool.ntp.org";

static Preferences prefs;
static int zoneIndex = 0;

static void applyZone(int idx, bool persist) {
  if (idx < 0) idx = ZONE_COUNT - 1;
  if (idx >= ZONE_COUNT) idx = 0;
  zoneIndex = idx;

  setenv("TZ", ZONES[zoneIndex].posix, 1);
  tzset();

  if (persist) prefs.putInt("zone", zoneIndex);
  Serial.printf("zone: %s (%s)\n", ZONES[zoneIndex].name, ZONES[zoneIndex].posix);
  displayForceRedraw();
}

// Tilt-to-rate ("airplane") control: tilt angle sets how fast time scrolls, and
// it keeps scrolling until the device is returned to level. Curve is quadratic
// so small tilts give fine control and full deflection covers hours:
//   10deg ~ 1.5 min/s   20deg ~ 13 min/s   30deg ~ 37 min/s   50deg ~ 2 h/s
static const float TILT_FULL_DEG   = 50.0f;   // measured comfortable deflection
static const float TILT_DEAD_DEG   = 5.0f;    // matches the deadband in gestures.cpp
static const float MAX_RATE_SEC_S  = 7200.0f; // seconds of time per real second at full tilt
static const float TILT_EXPO       = 2.0f;

static const int32_t  SNAP_SECONDS  = 600;    // display snaps to 10 min, as in the original
static const uint32_t IDLE_RESET_MS = 10000;  // original's auto_reset_delay
static const uint32_t WIFI_RETRY_MS   = 30000;

// Credentials come from WiFiManager's captive portal and live in NVS, so there
// is no secrets.h any more. The portal opens by itself when nothing is saved.
// To re-run it deliberately, boot the device face-down: this hardware has no
// button, so the accelerometer stands in for "hold a button while powering on".
static const char *AP_NAME = "Clockulator";
static const uint32_t PORTAL_TIMEOUT_S = 180;

// Holding the device face-down is the "hold the button" gesture this hardware
// does not have. Inverted reads az/|a| about -1.00 against +0.92 upright, so
// there is no chance of triggering it by accident -- and the prism has to come
// off to do it at all.
static const uint32_t FACE_DOWN_HOLD_MS = 3000;

// Anything past 2020 means SNTP has landed at least once.
static const time_t CLOCK_VALID_AFTER = 1600000000;

static float dialOffset = 0.0f;   // seconds, accumulated continuously
static uint32_t lastRateMs = 0;
static uint32_t lastTouchMs = 0;
static uint32_t lastWifiAttemptMs = 0;
static bool wifiWasConnected = false;

// SNTP calls this on every successful sync, so a silent clock can be told apart
// from one that is quietly serving a stale reading.
static void onTimeSync(struct timeval *tv) {
  struct tm utc;
  time_t t = tv->tv_sec;
  gmtime_r(&t, &utc);
  char buf[32];
  strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &utc);
  Serial.printf("ntp: synced %s UTC\n", buf);
}

// Shown while the portal is up, so the screen is not just a blank clock.
static void onPortalStart(WiFiManager *wm) {
  Serial.printf("wifi: config portal up, join '%s' then browse to %s\n",
                AP_NAME, WiFi.softAPIP().toString().c_str());
  displayMessage("SETUP", "join wifi network", AP_NAME);
}

// Modal: blocks until provisioned or timed out, then returns to being a clock.
static void enterConfigPortal() {
  Serial.println("wifi: entering config portal on request");
  WiFiManager wm;
  wm.setAPCallback(onPortalStart);
  wm.setConfigPortalTimeout(PORTAL_TIMEOUT_S);
  bool ok = wm.startConfigPortal(AP_NAME);
  Serial.printf("wifi: portal closed, %s\n", ok ? "connected" : "not connected");

  lastWifiAttemptMs = millis();
  wifiWasConnected = (WiFi.status() == WL_CONNECTED);
  displayForceRedraw();
}

static void startWifi() {
  lastWifiAttemptMs = millis();
  WiFi.mode(WIFI_STA);
  WiFi.begin();   // reuse whatever the portal saved
  Serial.printf("wifi: [%lu ms] reconnecting with saved credentials\n", (unsigned long)millis());
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nClockulator");

  displayBegin();
  displayClocks(-1, -1, -1, -1, "", 0);

  if (!gesturesBegin()) {
    // No IMU means no gestures, but it is still a clock. Do not hang.
    Serial.println("gestures: QMI8658 not found - clock only, no gestures");
  }

  // Face-down at boot forces the portal, standing in for the button this
  // hardware does not have.
  bool forcePortal = screenFacingDown();
  if (forcePortal) Serial.println("wifi: booted face-down, forcing config portal");

  WiFiManager wm;
  wm.setAPCallback(onPortalStart);
  // Bounded, so a device that is never provisioned still boots and runs as a
  // clock rather than sitting in the portal for ever.
  wm.setConfigPortalTimeout(PORTAL_TIMEOUT_S);
  wm.setConnectTimeout(20);

  bool connected = forcePortal ? wm.startConfigPortal(AP_NAME) : wm.autoConnect(AP_NAME);
  Serial.printf("wifi: [%lu ms] %s\n", (unsigned long)millis(), connected ? "connected" : "no connection, continuing anyway");

  // Seed both, or loop() sees a stale zero timer and an unset flag and tears
  // down the connection that was just established.
  lastWifiAttemptMs = millis();
  wifiWasConnected = (WiFi.status() == WL_CONNECTED);

  displayForceRedraw();

  prefs.begin("clockulator", false);
  zoneIndex = prefs.getInt("zone", 0);
  if (zoneIndex < 0 || zoneIndex >= ZONE_COUNT) zoneIndex = 0;

  sntp_set_sync_interval(60UL * 1000);   // original resynced every 60s
  sntp_set_time_sync_notification_cb(onTimeSync);
  configTzTime(ZONES[zoneIndex].posix, NTP_SERVER);
  applyZone(zoneIndex, false);
}

void loop() {
  gesturesPoll();


  uint32_t nowMs = millis();

  if (faceDownHeldMs() > FACE_DOWN_HOLD_MS) {
    enterConfigPortal();
    return;
  }

  switch (takeTap()) {
    case TAP_DESK:
      dialOffset = 0.0f;         // flat tap: back to real time
      lastTouchMs = nowMs;
      Serial.println("tap: desk - back to real time");
      break;
    case TAP_LEFT:
      applyZone(zoneIndex - 1, true);   // edge strikes change zone and
      break;                            // deliberately leave the offset alone,
    case TAP_RIGHT:                     // so a moment can be compared across zones
      applyZone(zoneIndex + 1, true);
      break;
    default:
      break;
  }

  // Integrate tilt into the offset. Rate, not position: hold a tilt and time
  // keeps moving; return to level and it stops where it landed.
  float dt = (nowMs - lastRateMs) / 1000.0f;
  lastRateMs = nowMs;
  if (dt > 0.0f && dt < 0.5f) {
    float tilt = tiltDeg();
    if (tilt != 0.0f) {
      float span = TILT_FULL_DEG - TILT_DEAD_DEG;
      float norm = (fabsf(tilt) - TILT_DEAD_DEG) / span;
      if (norm > 1.0f) norm = 1.0f;             // clamp past full deflection
      float rate = MAX_RATE_SEC_S * powf(norm, TILT_EXPO);
      dialOffset += (tilt > 0 ? rate : -rate) * dt;
      lastTouchMs = nowMs;
    }
  }

  if (dialOffset != 0.0f && nowMs - lastTouchMs > IDLE_RESET_MS) {
    dialOffset = 0.0f;
  }

  // Retry WiFi in the background rather than blocking setup() on it.
  bool isConnected = (WiFi.status() == WL_CONNECTED);
  if (isConnected != wifiWasConnected) {
    wifiWasConnected = isConnected;
    if (isConnected) Serial.printf("wifi: [%lu ms] connected, ip %s\n", (unsigned long)millis(), WiFi.localIP().toString().c_str());
    else             Serial.printf("wifi: [%lu ms] connection lost (status=%d)\n", (unsigned long)millis(), (int)WiFi.status());
  }
  if (!isConnected && nowMs - lastWifiAttemptMs > WIFI_RETRY_MS) {
    Serial.println("wifi: retrying");
    WiFi.disconnect();
    startWifi();
  }

  time_t base = time(nullptr);
  if (base < CLOCK_VALID_AFTER) {
    displayClocks(-1, -1, -1, -1, "", millis() / 1000);
    return;
  }

  int32_t shown = (int32_t)lroundf(dialOffset / SNAP_SECONDS) * SNAP_SECONDS;
  time_t moment = base + shown;
  struct tm utc, loc;
  gmtime_r(&moment, &utc);
  localtime_r(&moment, &loc);

  // Shimmer follows the real time of day, not the dialled-up time, so scrolling
  // the clock does not drag the colour around with it.
  struct tm realLoc;
  localtime_r(&base, &realLoc);
  uint32_t phaseSeconds = realLoc.tm_hour * 3600u + realLoc.tm_min * 60u + realLoc.tm_sec;

  char abbrev[8];
  strftime(abbrev, sizeof abbrev, "%Z", &loc);

  char label[32];
  snprintf(label, sizeof label, "%s %s", ZONES[zoneIndex].name, abbrev);

  displayClocks(utc.tm_hour, utc.tm_min, loc.tm_hour, loc.tm_min, label, phaseSeconds);
}
