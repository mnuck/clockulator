#include "display.h"
#include <TFT_eSPI.h>

// Font 8 is 75px tall: digits 55px, colon 29px. "12:34" in one drawString is
// 249px, which overflows the 240px panel by 9px. So HH and MM are drawn as two
// strings with a gap and the colon is painted by hand -- ~236px, and it uses the
// panel far better than the 48px Font 7 would.
#define BIG_FONT     8
#define DIGIT_W      55
#define PAIR_W       (2 * DIGIT_W)
#define GAP_W        16
#define CLOCK_W      (2 * PAIR_W + GAP_W)
#define CLOCK_X      ((240 - CLOCK_W) / 2)
#define BIG_H        75

#define UTC_LABEL_Y  6
#define UTC_Y        26
#define RULE_Y       118
#define LOC_LABEL_Y  126
#define LOC_Y        146

// This unit is viewed through a prism, which mirrors the panel top-to-bottom.
// Rotation 4 is the vendor's mirrored mode (TFT_MAD_MY), the same one their
// AstronautClock_Prism example uses; rotation 0 is correct for a bare panel.
#define PRISM        1
#define ROTATION     (PRISM ? 4 : 0)

#define COL_BG       TFT_BLACK
#define COL_TIME     TFT_WHITE

// Shimmer: each digit is drawn at its own phase of a slow hue drift, so a wave
// travels across the clock rather than the whole thing changing together.
// Saturation is deliberately tiny -- these are white with a tint, not colours.
#define SHIMMER_SAT      0.13f   // 0 = plain white. Past ~0.25 it stops reading as white.
// Seconds for a full hue cycle (2 hours). Driven by time of day, so the same
// tint comes round at the same clock time each day. Prefer values that divide
// 86400 evenly (7200, 14400, 43200) or the hue jumps at midnight.
#define SHIMMER_PERIOD_S 7200.0f
#define SHIMMER_SPREAD   0.055f  // phase step per digit; larger = tighter wave
// At an hours-long period the tint moves imperceptibly per second, so there is
// no reason to redraw fast. 1fps keeps the ~23ms blocking push down to ~2% duty.
// A change to the displayed time still repaints immediately, bypassing this.
#define SHIMMER_FRAME_MS 1000
#define COL_LABEL    0x8410   // mid grey
#define COL_RULE     0x2965   // dim slate

static TFT_eSPI tft = TFT_eSPI(240, 240);
static TFT_eSprite frame = TFT_eSprite(&tft);
static bool spriteOk = false;

static char lastKey[64] = "";
static uint32_t lastFrameMs = 0;

// Low-saturation HSV -> RGB565. At small s this rides just off white.
static uint16_t shimmer(float phase) {
  float h = phase - floorf(phase);          // 0..1
  float i = h * 6.0f;
  int   seg = (int)i;
  float f = i - seg;
  float v = 255.0f;
  float p = v * (1.0f - SHIMMER_SAT);
  float q = v * (1.0f - SHIMMER_SAT * f);
  float t = v * (1.0f - SHIMMER_SAT * (1.0f - f));
  float r, g, b;
  switch (seg % 6) {
    case 0:  r = v; g = t; b = p; break;
    case 1:  r = q; g = v; b = p; break;
    case 2:  r = p; g = v; b = t; break;
    case 3:  r = p; g = q; b = v; break;
    case 4:  r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
  return ((uint16_t)r >> 3) << 11 | ((uint16_t)g >> 2) << 5 | ((uint16_t)b >> 3);
}

void displayBegin() {
  tft.begin();
  tft.setRotation(ROTATION);
  tft.fillScreen(COL_BG);

  frame.setColorDepth(16);
  spriteOk = (frame.createSprite(240, 240) != nullptr);
  if (!spriteOk) {
    // A failed 115KB allocation should degrade to a flickering clock, not a blank one.
    Serial.println("display: sprite alloc failed, drawing direct (will flicker)");
  }
}

void displayForceRedraw() { lastKey[0] = '\0'; }

void displayMessage(const char *l1, const char *l2, const char *l3) {
  TFT_eSPI &g = spriteOk ? (TFT_eSPI &)frame : tft;
  g.fillRect(0, 0, 240, 240, COL_BG);
  g.setTextDatum(TC_DATUM);
  g.setTextColor(COL_TIME, COL_BG);
  if (l1) g.drawString(l1, 120,  58, 4);
  g.setTextColor(COL_LABEL, COL_BG);
  if (l2) g.drawString(l2, 120, 106, 2);
  g.setTextColor(COL_TIME, COL_BG);
  if (l3) g.drawString(l3, 120, 136, 4);
  if (spriteOk) frame.pushSprite(0, 0);
  displayForceRedraw();   // so the clock repaints when we go back to it
}

// TFT_eSprite derives from TFT_eSPI, so this serves both the sprite and the
// direct-to-panel path unchanged.
static void drawClock(TFT_eSPI &g, int y, int h, int m, float phase) {
  char txt[6];
  if (h < 0 || m < 0) snprintf(txt, sizeof txt, "--:--");
  else                snprintf(txt, sizeof txt, "%02d:%02d", h, m);

  // Each digit carries its own phase, which is what makes it shimmer rather
  // than merely change colour. Drawn one glyph at a time for that reason.
  const int xs[4] = { CLOCK_X,
                      CLOCK_X + DIGIT_W,
                      CLOCK_X + PAIR_W + GAP_W,
                      CLOCK_X + PAIR_W + GAP_W + DIGIT_W };
  const int idx[4] = { 0, 1, 3, 4 };   // skip the ':' in txt

  g.setTextDatum(TL_DATUM);
  for (int i = 0; i < 4; i++) {
    char one[2] = { txt[idx[i]], 0 };
    g.setTextColor(shimmer(phase + i * SHIMMER_SPREAD), COL_BG);
    g.drawString(one, xs[i], y, BIG_FONT);
  }

  // Colon: two squares centred in the gap, at 1/3 and 2/3 of the digit height.
  int cx = CLOCK_X + PAIR_W + GAP_W / 2 - 4;
  uint16_t cc = shimmer(phase + 2 * SHIMMER_SPREAD);
  g.fillRect(cx, y + BIG_H / 3 - 4, 8, 8, cc);
  g.fillRect(cx, y + 2 * BIG_H / 3 - 4, 8, 8, cc);
}

static void paint(TFT_eSPI &g, int utcH, int utcM, int locH, int locM, const char *locLabel, float phase) {
  g.fillRect(0, 0, 240, 240, COL_BG);

  g.setTextDatum(TC_DATUM);
  g.setTextColor(COL_LABEL, COL_BG);
  g.drawString("UTC", 120, UTC_LABEL_Y, 2);
  g.drawString(locLabel && *locLabel ? locLabel : "LOCAL", 120, LOC_LABEL_Y, 2);

  g.drawFastHLine(20, RULE_Y, 200, COL_RULE);

  drawClock(g, UTC_Y, utcH, utcM, phase);
  drawClock(g, LOC_Y, locH, locM, phase + 4 * SHIMMER_SPREAD);
}

void displayClocks(int utcH, int utcM, int locH, int locM, const char *locLabel,
                   uint32_t phaseSeconds) {
  char key[64];
  snprintf(key, sizeof key, "%d:%d|%d:%d|%s", utcH, utcM, locH, locM, locLabel ? locLabel : "");
  uint32_t now = millis();
  bool changed = (strcmp(key, lastKey) != 0);
  // Redraw on a change, or on the shimmer's frame tick. Without shimmer this
  // would only repaint once a minute.
  if (!changed && now - lastFrameMs < SHIMMER_FRAME_MS) return;

  strncpy(lastKey, key, sizeof lastKey - 1);
  lastKey[sizeof lastKey - 1] = '\0';
  lastFrameMs = now;

  float phase = phaseSeconds / SHIMMER_PERIOD_S;

  if (spriteOk) {
    paint(frame, utcH, utcM, locH, locM, locLabel, phase);
    frame.pushSprite(0, 0);
  } else {
    paint(tft, utcH, utcM, locH, locM, locLabel, phase);
  }
}
