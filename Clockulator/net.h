#ifndef CLOCKULATOR_NET_H
#define CLOCKULATOR_NET_H

#include <Arduino.h>

// Network and time sync. Two builds, chosen at compile time by tools/flash.sh:
//
//   CLOCKULATOR_MODE_HOME     WiFiManager captive portal, always connected,
//                             NTP every minute. Hold face-down to re-run setup.
//
//   CLOCKULATOR_MODE_COMPANY  Fixed credentials from wifi_config.h, no portal,
//                             and the radio is switched on only for a daily sync.
//
// Both take the currently selected zone, because starting SNTP resets TZ as a
// side effect and has to be handed the live zone back.

// Once, from setup(), after the display and gestures are up.
void netBegin(const char *posixTz);

// Every loop(). Never blocks -- except the home build's setup portal, which is
// modal by design.
void netPoll(const char *posixTz);

#endif
