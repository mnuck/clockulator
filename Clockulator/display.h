#ifndef CLOCKULATOR_DISPLAY_H
#define CLOCKULATOR_DISPLAY_H

#include <Arduino.h>

void displayBegin();

// Draws both clocks. Pass hours/minutes < 0 for "--:--" (clock not yet synced).
//
// phaseSeconds drives the shimmer. Pass local seconds-since-midnight of the
// REAL time, so the tint is a property of the time of day rather than of
// uptime or of whatever offset has been dialled in. Before the clock is valid,
// pass an uptime in seconds -- it only needs to advance.
void displayClocks(int utcH, int utcM, int locH, int locM, const char *locLabel,
                   uint32_t phaseSeconds);

// Three centred lines, for setup/status screens.
void displayMessage(const char *l1, const char *l2, const char *l3);

void displayForceRedraw();

#endif
