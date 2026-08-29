#ifndef CLOCKULATOR_ZONES_H
#define CLOCKULATOR_ZONES_H

// Timezones cycled by tapping the device's left/right edge on the desk.
//
// Each entry carries an explicit short name as well as the POSIX rule, because
// strftime %Z alone is ambiguous here: US Central in winter and China both
// render as "CST". The name disambiguates; %Z still shows the live DST state.
//
// Adding a zone is one line. Order is the cycle order.

struct Zone {
  const char *name;
  const char *posix;
};

static const Zone ZONES[] = {
  { "PACIFIC",   "PST8PDT,M3.2.0/2,M11.1.0/2" },
  { "MOUNTAIN",  "MST7MDT,M3.2.0/2,M11.1.0/2" },
  { "CENTRAL",   "CST6CDT,M3.2.0/2,M11.1.0/2" },
  { "EASTERN",   "EST5EDT,M3.2.0/2,M11.1.0/2" },
  { "AMSTERDAM", "CET-1CEST,M3.5.0/2,M10.5.0/3" },
  { "CHINA",     "CST-8" },
};

static const int ZONE_COUNT = sizeof(ZONES) / sizeof(ZONES[0]);

#endif
