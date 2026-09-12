#include "net.h"

#include <WiFi.h>
#include <esp_sntp.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <time.h>

#include "display.h"
#include "gestures.h"

// Exactly one mode, supplied by tools/flash.sh. Refusing to build otherwise
// means a build can never fall silently into the wrong network behaviour.
#if defined(CLOCKULATOR_MODE_HOME) == defined(CLOCKULATOR_MODE_COMPANY)
#error "Build with exactly one of CLOCKULATOR_MODE_HOME or CLOCKULATOR_MODE_COMPANY: use tools/flash.sh home|company"
#endif

static const char *NTP_SERVER = "pool.ntp.org";

// ---------------------------------------------------------------------------
// Shared: starting SNTP, and logging each sync
// ---------------------------------------------------------------------------

static volatile bool syncedFlag = false;   // set from the SNTP task
static int64_t lastSyncTimerUs = 0;
static int64_t lastSyncEpochUs = 0;

// Drift only rises above network jitter (milliseconds) over long spans. Across
// the home build's one-minute interval it is noise, so it is not reported.
static const int64_t DRIFT_MIN_SPAN_US = 10LL * 60 * 1000000;

static void onTimeSync(struct timeval *tv) {
  int64_t timerUs = esp_timer_get_time();
  int64_t epochUs = (int64_t)tv->tv_sec * 1000000LL + tv->tv_usec;

  struct tm utc;
  time_t t = tv->tv_sec;
  gmtime_r(&t, &utc);
  char stamp[32];
  strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &utc);

  int64_t spanUs = timerUs - lastSyncTimerUs;
  if (lastSyncTimerUs && spanUs >= DRIFT_MIN_SPAN_US) {
    // esp_timer counts the crystal and SNTP never adjusts it, so the gap between
    // how far NTP time moved and how far the crystal counted is the crystal's
    // drift since the last sync. Positive means the crystal ran slow.
    double driftS = ((epochUs - lastSyncEpochUs) - spanUs) / 1e6;
    Serial.printf("ntp: synced %s UTC, drift %+.3f s over %.2f h (%+.1f ppm)\n",
                  stamp, driftS, spanUs / 3.6e9, driftS / (spanUs / 1e6) * 1e6);
  } else {
    Serial.printf("ntp: synced %s UTC\n", stamp);
  }

  lastSyncTimerUs = timerUs;
  lastSyncEpochUs = epochUs;
  syncedFlag = true;
}

// configTzTime() resets TZ as a side effect, so it is always given the zone that
// is currently selected rather than a fixed one.
static void startSntp(const char *posixTz) {
  sntp_set_time_sync_notification_cb(onTimeSync);
  configTzTime(posixTz, NTP_SERVER);
}

#if defined(CLOCKULATOR_MODE_HOME)
// ---------------------------------------------------------------------------
// Home: WiFiManager captive portal, always connected, NTP every minute
// ---------------------------------------------------------------------------

#include <WiFiManager.h>

// Credentials come from WiFiManager's captive portal and live in NVS. The portal
// opens by itself when nothing is saved. To re-run it deliberately, hold the
// device face-down: this hardware has no button, so the accelerometer stands in
// for one. Inverted reads az/|a| about -1.00 against +0.92 upright, so it cannot
// trigger by accident, and the prism has to come off to do it at all.
//
// The portal is an open access point that accepts firmware uploads without
// authentication -- WiFiManager registers /update and /u unconditionally. That
// is acceptable at home, and it is why the company build leaves WiFiManager out.
static const char *AP_NAME = "Clockulator";
static const uint32_t PORTAL_TIMEOUT_S  = 180;
static const uint32_t FACE_DOWN_HOLD_MS = 3000;
static const uint32_t WIFI_RETRY_MS     = 30000;
static const uint32_t SYNC_INTERVAL_MS  = 60UL * 1000;   // the original resynced every 60s

static uint32_t lastWifiAttemptMs = 0;
static bool wifiWasConnected = false;

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

void netBegin(const char *posixTz) {
  // Face-down at boot forces the portal too.
  bool forcePortal = screenFacingDown();
  if (forcePortal) Serial.println("wifi: booted face-down, forcing config portal");

  WiFiManager wm;
  wm.setAPCallback(onPortalStart);
  // Bounded, so a device that is never provisioned still boots and runs as a
  // clock rather than sitting in the portal for ever.
  wm.setConfigPortalTimeout(PORTAL_TIMEOUT_S);
  wm.setConnectTimeout(20);

  bool connected = forcePortal ? wm.startConfigPortal(AP_NAME) : wm.autoConnect(AP_NAME);
  Serial.printf("wifi: [%lu ms] %s\n", (unsigned long)millis(),
                connected ? "connected" : "no connection, continuing anyway");

  // Seed both, or netPoll() sees a stale zero timer and an unset flag and tears
  // down the connection that was just established.
  lastWifiAttemptMs = millis();
  wifiWasConnected = (WiFi.status() == WL_CONNECTED);
  displayForceRedraw();

  sntp_set_sync_interval(SYNC_INTERVAL_MS);
  startSntp(posixTz);
}

void netPoll(const char *posixTz) {
  (void)posixTz;   // SNTP keeps running here, so TZ is never reset after boot
  uint32_t nowMs = millis();

  if (faceDownHeldMs() > FACE_DOWN_HOLD_MS) {
    enterConfigPortal();
    return;
  }

  bool isConnected = (WiFi.status() == WL_CONNECTED);
  if (isConnected != wifiWasConnected) {
    wifiWasConnected = isConnected;
    if (isConnected) Serial.printf("wifi: [%lu ms] connected, ip %s\n", (unsigned long)nowMs, WiFi.localIP().toString().c_str());
    else             Serial.printf("wifi: [%lu ms] connection lost (status=%d)\n", (unsigned long)nowMs, (int)WiFi.status());
  }
  if (!isConnected && nowMs - lastWifiAttemptMs > WIFI_RETRY_MS) {
    Serial.println("wifi: retrying");
    WiFi.disconnect();
    startWifi();
  }
}

#else
// ---------------------------------------------------------------------------
// Company: fixed guest-network credentials, no portal, radio on only to sync
// ---------------------------------------------------------------------------

#if __has_include("wifi_config.h")
#include "wifi_config.h"
#else
#error "The company build needs wifi_config.h: copy wifi_config.h.example to wifi_config.h and fill it in"
#endif

// The clock needs the network for a few seconds a day. Between syncs the radio
// is off, so the device is invisible on the network: nothing to spoof, ping or
// deauth. The crystal carries the time in between; each sync logs the drift.
static const uint32_t SYNC_EVERY_MS      = 24UL * 60 * 60 * 1000;
static const uint32_t CONNECT_TIMEOUT_MS = 30000;
static const uint32_t SYNC_TIMEOUT_MS    = 60000;
static const uint32_t RETRY_MIN_MS       = 60UL * 1000;
static const uint32_t RETRY_MAX_MS       = 15UL * 60 * 1000;

enum SyncPhase { PHASE_IDLE, PHASE_CONNECTING, PHASE_SYNCING };
static SyncPhase phase = PHASE_IDLE;
static uint32_t phaseStartMs = 0;
static uint32_t idleForMs = 0;
static uint32_t retryMs = RETRY_MIN_MS;

static void radioOff() {
  if (esp_sntp_enabled()) esp_sntp_stop();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

static void goIdle(uint32_t forMs) {
  phase = PHASE_IDLE;
  phaseStartMs = millis();
  idleForMs = forMs;
}

static void syncFailed(const char *why) {
  radioOff();
  Serial.printf("wifi: sync failed (%s), radio off, retrying in %lu s\n", why, (unsigned long)(retryMs / 1000));
  goIdle(retryMs);
  retryMs = (retryMs * 2 > RETRY_MAX_MS) ? RETRY_MAX_MS : retryMs * 2;
}

void netBegin(const char *posixTz) {
  (void)posixTz;   // SNTP is started per sync, in netPoll()

  // A home build keeps its network's password in NVS, via WiFiManager. This
  // build never uses stored credentials, and a device carried into the office
  // should not be holding the home password in flash, so scrub it. This must
  // happen while storage is still flash; after the switch to RAM below, an
  // erase would only clear RAM.
  //
  // Clear only the STA credentials, by writing a blank STA config. Two more
  // obvious routes both fail here, found on the hardware:
  //  - esp_wifi_restore() resets the whole WiFi stack, mode included, underneath
  //    the Arduino layer's cached state, and the first connect after it failed.
  //    That would have hit exactly the first boot of a device newly moved from
  //    home to the office.
  //  - WiFi.disconnect(false, true) refuses to erase until the station's
  //    "started" event has arrived, which is just after WiFi.mode() returns, so
  //    called here it silently erases nothing.
  // Setting the config needs WiFi initialised but not started, and leaves the
  // mode alone.
  WiFi.mode(WIFI_STA);
  wifi_config_t stored;
  if (esp_wifi_get_config(WIFI_IF_STA, &stored) == ESP_OK && stored.sta.ssid[0]) {
    Serial.printf("wifi: erasing stored credentials for '%s'\n", (char *)stored.sta.ssid);
    wifi_config_t blank;
    memset(&blank, 0, sizeof blank);
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &blank);
    if (err != ESP_OK) {
      Serial.printf("wifi: WARNING erase failed (%s), credentials may remain in flash\n", esp_err_to_name(err));
    }
  } else {
    Serial.println("wifi: no stored credentials");
  }

  // From here on, credentials live in RAM only, so the daily begin() does not
  // write the guest password to flash every time.
  WiFi.persistent(false);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  WiFi.mode(WIFI_OFF);

  goIdle(0);   // first sync straight away
}

void netPoll(const char *posixTz) {
  uint32_t now = millis();

  switch (phase) {
    case PHASE_IDLE:
      if (now - phaseStartMs >= idleForMs) {
        Serial.printf("wifi: radio on, joining '%s'\n", WIFI_SSID);
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        phase = PHASE_CONNECTING;
        phaseStartMs = now;
      }
      break;

    case PHASE_CONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("wifi: connected, ip %s\n", WiFi.localIP().toString().c_str());
        syncedFlag = false;
        startSntp(posixTz);
        phase = PHASE_SYNCING;
        phaseStartMs = now;
      } else if (now - phaseStartMs > CONNECT_TIMEOUT_MS) {
        syncFailed("could not join network");
      }
      break;

    case PHASE_SYNCING:
      if (syncedFlag) {
        radioOff();
        retryMs = RETRY_MIN_MS;
        Serial.println("wifi: radio off until the next daily sync");
        goIdle(SYNC_EVERY_MS);
      } else if (now - phaseStartMs > SYNC_TIMEOUT_MS) {
        syncFailed("no NTP response");
      }
      break;
  }
}

#endif
