#include "ui.h"

#include <TFT_eSPI.h>

#include "hid.h"
#include "io.h"
#include "touch.h"
#include "wire_test.h"

TFT_eSPI tft;

namespace {

// ------------------------------------------------------------------ layout
// One grid runs the whole panel: 8 px outer margin, 6 px gaps, 6 px corner
// radius, a 24 px header, 30 px source chips and a 56 px control row.  Every
// rectangle below is derived from those numbers, so columns always line up.
constexpr int16_t W = CFG_TFT_WIDTH;
constexpr int16_t H = CFG_TFT_HEIGHT;
constexpr int16_t MARGIN = 8;
constexpr int16_t GAP = 6;
constexpr int16_t RAD = 6;

constexpr int16_t STATUS_Y = 0;
constexpr int16_t STATUS_H = 24;
constexpr int16_t SRC_Y = 30;
constexpr int16_t SRC_H = 30;
// Track-info band: 20 px between source chips and visualiser.
// Only drawn in BT builds, but the constants are always defined so the
// visualiser position is stable and check_spectrum.py stays happy.
constexpr int16_t TRK_Y = 62;   // SRC_Y + SRC_H + GAP
constexpr int16_t TRK_H = 20;
constexpr int16_t VIZ_Y = 88;   // TRK_Y + TRK_H + GAP
constexpr int16_t VIZ_H = 84;   // trimmed by 22 to keep ROW at 178
constexpr int16_t ROW_Y = 178;
constexpr int16_t ROW_H = 56;

// PC volume row (CYD_BLE_HID builds only): the single source chip gives the
// right two thirds of the 30 px row to three volume chips
// (8 + 94 + 6 + 64 + 6 + 64 + 6 + 64 = 312).  With more than two sources the
// row is too crowded for them and the strip below is the whole control.
constexpr int16_t VOL_SRC_W = 94;
constexpr int16_t VOL_CHIP_W = 64;
constexpr bool kVolChips = CYD_BLE_HID && CFG_SOURCE_COUNT <= 2;

// ------------------------------------------------------------------ colours
// Semantic palette: white/grey text, cyan = interactive, green = on,
// amber = busy, red = fault, blue = Bluetooth.  COL_LINE is the quiet grey
// for dividers, meter tracks and grids.
constexpr uint16_t COL_BG = 0x0000;
constexpr uint16_t COL_PANEL = 0x18E3;
constexpr uint16_t COL_PANEL_ON = 0x2D6B;
constexpr uint16_t COL_LINE = 0x1082;
constexpr uint16_t COL_TEXT = 0xFFFF;
constexpr uint16_t COL_DIM = 0x7BEF;
constexpr uint16_t COL_ACCENT = 0x07FF;
constexpr uint16_t COL_ON = 0x07E0;
constexpr uint16_t COL_WARN = 0xFD20;
constexpr uint16_t COL_HOT = 0xF800;
constexpr uint16_t COL_BT = 0x33FF;

struct Rect {
  int16_t x, y, w, h;
  bool has(int16_t px, int16_t py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
};

enum class View : uint8_t { Main, Settings, Diagnostics };
// Visualiser modes
enum : uint8_t { VIZ_BARS, VIZ_VU, VIZ_SCOPE, VIZ_OFF, VIZ_COUNT };
const char* kVizName[VIZ_COUNT] = {"BARS", "VU", "SCOPE", "OFF"};

Settings* sSettings = nullptr;
View sView = View::Main;

int sBrightness = CFG_BRIGHT_DEFAULT;      // 0..255, never a signed byte
uint32_t sLastTouch = 0;
bool sDimmed = false;

// Touch gesture state
bool sDown = false;
int16_t sDownX = 0;
int16_t sDownY = 0;
uint32_t sDownAt = 0;
bool sLongFired = false;

// Cached state, so a redraw only touches what changed
int sShownSource = -1;
int sShownAmp = -1;
int sShownBt = -1;
int sShownTrack = -2;   // hash of title+artist; -2 forces first draw
int8_t sLastDrawnViz = -1;
uint32_t sNextDiag = 0;
int16_t sShownVol = -1;   // hash of the volume row: estimate, mute, link, hold
int16_t sShownHid = -1;   // link state drawn in the status pill
#if CYD_BLE_HID
int16_t sVolDragX = 0;    // strip: where the finger was at the last step
uint32_t sVolNext = 0;    // held chip: when the next step is due
#endif

// Marquee scroll state
char sMarqueeText[160] = "";   // full "Title – Artist" string
int16_t sMarqueeOffset = 0;    // pixel offset scrolled so far
uint32_t sMarqueeNext = 0;     // when to advance the scroll
constexpr int16_t kMarqueeStep = 2;     // px per tick
constexpr uint32_t kMarqueeMs = 35;     // ms per tick  (~57 px/s)
constexpr uint32_t kMarqueePause = 1500; // pause at start before scrolling

// Button press feedback
Rect sFeedbackRect = {0,0,0,0};
uint32_t sFeedbackUntil = 0;

// Backlight fade state
int sFadeTarget = -1;
uint32_t sFadeNext = 0;
constexpr int kFadeStep = 8;
constexpr uint32_t kFadeMs = 16;   // ~60 Hz

// AVRCP button rects (BT build only, inside TRK band)
#if CYD_BT_SINK
Rect avrcPrevRect() { return Rect{(int16_t)(W-8-4*22-3*4), TRK_Y, 22, TRK_H}; }
Rect avrcPlayRect() { return Rect{(int16_t)(W-8-3*22-2*4), TRK_Y, 22, TRK_H}; }
Rect avrcPauseRect(){ return Rect{(int16_t)(W-8-2*22-1*4), TRK_Y, 22, TRK_H}; }
Rect avrcNextRect() { return Rect{(int16_t)(W-8-1*22-0*4), TRK_Y, 22, TRK_H}; }
#endif

// No usable calibration yet: the panel cannot map a tap to a button until the
// user has tapped the crosses, and any touch at all is a good enough hint that
// they are there.  touch::calibrated() is the authority; what is stored in NVS
// only feeds the driver when the panel comes up.
bool uncalibrated() { return !touch::calibrated(); }
int8_t sShownBrightness = -1;
int8_t sShownViz = -1;
bool sShownDim = false;
float sLastBar[dsp::kBands];
float sLastPeak[dsp::kBands];
int16_t sLastVu = -1;
int16_t sLastVuPeak = -1;
// Scope trace: the screen Y drawn for each point last frame, so the old line
// can be erased exactly where it lies instead of blanking the whole area.
int16_t sScopeY[audio::kScopePoints] = {};

constexpr int kVizBars = dsp::kBands;

// --------------------------------------------------------------- backlight
void applyBrightness() {
#if CFG_BL_FITTED && !CYD_BL_STATIC
  ledcWrite(CFG_BL_CHANNEL, (uint32_t)sBrightness);
#endif
}

void setBrightness(int value) {
  if (value < CFG_BRIGHT_MIN) value = CFG_BRIGHT_MIN;
  if (value > 255) value = 255;
  sBrightness = value;
  applyBrightness();
  if (sSettings) sSettings->data().brightness = (uint8_t)sBrightness;
}

// Smooth backlight fade: call every frame, drives sBrightness toward target.
void fadeTick() {
#if CFG_BL_FITTED && !CYD_BL_STATIC
  if (sFadeTarget < 0) return;
  const uint32_t now = millis();
  if (now < sFadeNext) return;
  sFadeNext = now + kFadeMs;
  if (sBrightness < sFadeTarget) {
    sBrightness = (int)min((int)sBrightness + kFadeStep, sFadeTarget);
  } else if (sBrightness > sFadeTarget) {
    sBrightness = (int)max((int)sBrightness - kFadeStep, sFadeTarget);
  } else {
    sFadeTarget = -1;  // reached
  }
  ledcWrite(CFG_BL_CHANNEL, (uint32_t)sBrightness);
#endif
}

void startFade(int target) {
  if (target < CFG_BRIGHT_MIN) target = CFG_BRIGHT_MIN;
  if (target > 255) target = 255;
  sFadeTarget = target;
}

void wake() {
  sLastTouch = millis();
  if (sDimmed) {
    sDimmed = false;
    startFade(sBrightness);  // fade back up to current brightness
  }
}

// ------------------------------------------------------------------ drawing
void label(int16_t x, int16_t y, const char* text, uint16_t fg, uint16_t bg,
           uint8_t font, uint8_t datum) {
  tft.setTextDatum(datum);
  tft.setTextColor(fg, bg);
  tft.drawString(text, x, y, font);
}

// Vector font (GFXFF) label: paint the container first, then draw over it
// with a transparent background - free fonts then need no bg handling.
void labelF(int16_t x, int16_t y, const char* text, uint16_t fg,
            const GFXfont* font, uint8_t datum) {
  tft.setFreeFont(font);
  tft.setTextDatum(datum);
  tft.setTextColor(fg);
  tft.drawString(text, x, y);
  tft.setFreeFont(nullptr);
}

// Rounded chip with centred text - the one shape every button shares.
void chip(const Rect& r, uint16_t fill, uint16_t fg, const char* text) {
  tft.fillRoundRect(r.x, r.y, r.w, r.h, RAD, fill);
  if (text && *text) {
    label(r.x + r.w / 2, r.y + r.h / 2, text, fg, fill, 2, MC_DATUM);
  }
}

void drawVizFrame();   // defined with the visualiser below

Rect sourceRect(int index, int count) {
  // In a PC-volume build the row is split: the sources keep VOL_SRC_W and the
  // three volume chips take the rest.
  const int16_t area = kVolChips ? VOL_SRC_W : (int16_t)(W - 2 * MARGIN);
  const int16_t bw = (area - (count - 1) * GAP) / count;
  return Rect{(int16_t)(MARGIN + index * (bw + GAP)), SRC_Y, bw, SRC_H};
}

// Bottom row: one wide power button and two equal 71 px buttons, all on the
// 8/6 grid (8 + 150 + 6 + 71 + 6 + 71 + 8 = 320).
Rect powerRect() { return Rect{MARGIN, ROW_Y, 150, ROW_H}; }
Rect vizRect() { return Rect{(int16_t)(MARGIN + 150 + GAP), ROW_Y, 71, ROW_H}; }
Rect menuRect() {
  return Rect{(int16_t)(MARGIN + 150 + GAP + 71 + GAP), ROW_Y, 71, ROW_H};
}

#if CYD_BLE_HID
// The PC-volume chips: -, MUTE, + across the right of the source row, and the
// 20 px strip under them - the touch "encoder" (see uiPoll).
Rect volDownRect() {
  return Rect{(int16_t)(MARGIN + VOL_SRC_W + GAP), SRC_Y, VOL_CHIP_W, SRC_H};
}
Rect volMuteRect() {
  return Rect{(int16_t)(MARGIN + VOL_SRC_W + 2 * GAP + VOL_CHIP_W), SRC_Y,
              VOL_CHIP_W, SRC_H};
}
Rect volUpRect() {
  return Rect{(int16_t)(MARGIN + VOL_SRC_W + 3 * GAP + 2 * VOL_CHIP_W), SRC_Y,
              VOL_CHIP_W, SRC_H};
}
Rect pcVolumeRect() {
  return Rect{MARGIN, TRK_Y, (int16_t)(W - 2 * MARGIN), TRK_H};
}
#endif

Rect overlayRow(int index) {
  return Rect{MARGIN, (int16_t)(30 + index * 24), (int16_t)(W - 2 * MARGIN),
              24};
}
#if CYD_BT_SINK
// Two buttons on one row: the audio output only exists in the A2DP build.
Rect diagTestRect() { return Rect{8, 192, 156, 22}; }
Rect diagToneRect() { return Rect{172, 192, 140, 22}; }
#else
Rect diagTestRect() { return Rect{8, 192, 168, 22}; }
Rect diagToneRect() { return Rect{0, 0, 0, 0}; }
#endif
Rect overlayArrow(int index, bool right) {
  Rect r = overlayRow(index);
  const int16_t chipX = (int16_t)(r.x + r.w - 24);      // right chip is ">"
  return right ? Rect{chipX, r.y, 24, r.h}
               : Rect{(int16_t)(chipX - 4 - 24), r.y, 24, r.h};
}

void drawStatusBar(const UiStatus& st) {
  tft.fillRect(0, STATUS_Y, W, STATUS_H, COL_PANEL);

  // Brand on the left margin; the amp state lives on the power button below.
  labelF(MARGIN + 4, STATUS_Y + STATUS_H / 2 + 1, "AMP PANEL", COL_TEXT,
         &FreeSansBold9pt7b, ML_DATUM);

  // State pill on the right: the dot's colour says what Bluetooth is doing.
  if (st.btPresent) {
    const int16_t tw = tft.textWidth(st.btText, 2);
    const int16_t pw = (int16_t)(tw + 26);
    const Rect p{(int16_t)(W - MARGIN - pw), 3, pw, 18};
    const uint16_t bc = strcmp(st.btText, "PLAYING") == 0   ? COL_ON
                        : strcmp(st.btText, "LINKING") == 0 ? COL_WARN
                        : st.btConnected                     ? COL_BT
                                                             : COL_DIM;
    tft.fillRoundRect(p.x, p.y, p.w, p.h, 4, COL_PANEL_ON);
    tft.fillCircle(p.x + 9, p.y + p.h / 2, 3, bc);
    label(p.x + 17, p.y + p.h / 2, st.btText, COL_TEXT, COL_PANEL_ON, 2,
          ML_DATUM);
  }
#if CYD_BLE_HID
  // Without A2DP the right-hand pill belongs to the PC link: the dot is lit
  // while a PC is listening, the text says what the radio is doing.
  if (!st.btPresent && st.busOk) {
    const char* text = hid::stateText();
    const int16_t tw = tft.textWidth(text, 2);
    const int16_t pw = (int16_t)(tw + 26);
    const Rect p{(int16_t)(W - MARGIN - pw), 3, pw, 18};
    tft.fillRoundRect(p.x, p.y, p.w, p.h, 4, COL_PANEL_ON);
    tft.fillCircle(p.x + 9, p.y + p.h / 2, 3,
                   hid::connected() ? COL_ON : COL_DIM);
    label(p.x + 17, p.y + p.h / 2, text, COL_TEXT, COL_PANEL_ON, 2, ML_DATUM);
  }
#endif
  if (!st.busOk) {
    const Rect e{(int16_t)(W - MARGIN - 56), 3, 56, 18};
    chip(e, COL_HOT, COL_TEXT, "IO ERR");
  }
}

void drawSourceButtons(const UiStatus& st) {
  for (int i = 0; i < CFG_SOURCE_COUNT; ++i) {
    const Rect r = sourceRect(i, CFG_SOURCE_COUNT);
    const bool on = (i == st.source);
    // Selected = solid accent with black text, off = panel grey: one glance,
    // one answer.
    chip(r, on ? COL_ACCENT : COL_PANEL, on ? COL_BG : COL_DIM,
         CFG_SOURCES[i].label);
  }
}

#if CYD_BLE_HID
// Which PC-volume chip the finger is on: -1 none, 0 "-", 1 "MUTE", 2 "+".
int volPressedIndex() {
  if (!kVolChips || !sDown || sView != View::Main) return -1;
  if (volDownRect().has(sDownX, sDownY)) return 0;
  if (volMuteRect().has(sDownX, sDownY)) return 1;
  if (volUpRect().has(sDownX, sDownY)) return 2;
  return -1;
}

// Same, for any position: 1 = up, -1 = down, 0 = neither.
int volDirection(int16_t px, int16_t py) {
  if (!kVolChips) return 0;
  if (volUpRect().has(px, py)) return 1;
  if (volDownRect().has(px, py)) return -1;
  return 0;
}

// The three chips.  A chip under the finger is drawn lit: one tap is one key,
// and holding it repeats (see uiPoll).
void drawVolumeChips() {
  const bool linked = hid::connected();
  const bool muted = hid::muteEstimate();
  const int held = volPressedIndex();
  const Rect rects[3] = {volDownRect(), volMuteRect(), volUpRect()};
  const char* labels[3] = {"-", "MUTE", "+"};
  for (int i = 0; i < 3; ++i) {
    const Rect& r = rects[i];
    const uint16_t fill = (held == i) ? COL_PANEL_ON : COL_PANEL;
    tft.fillRoundRect(r.x, r.y, r.w, r.h, RAD, fill);
    const uint16_t fg =
        !linked ? COL_DIM : (muted && i == 1 ? COL_WARN : COL_TEXT);
    if (i == 1) {
      label(r.x + r.w / 2, r.y + r.h / 2, labels[i], fg, fill, 2, MC_DATUM);
    } else {
      labelF(r.x + r.w / 2, r.y + r.h / 2 + 1, labels[i], fg,
             &FreeSansBold12pt7b, MC_DATUM);
    }
  }
}

// The strip: what the panel believes the PC is set to, and the drag target.
// Nothing reads the PC's level (HID only sends keys), so the number is an
// estimate - it starts at 50 % and moves with every key that goes out.
void drawPcVolumeStrip() {
  const Rect r = pcVolumeRect();
  const int vol = hid::volumeEstimate();
  const bool muted = hid::muteEstimate();
  const bool linked = hid::connected();

  tft.fillRoundRect(r.x, r.y, r.w, r.h, 5, COL_PANEL);
  label(r.x + 6, r.y + r.h / 2, "PC VOL", muted ? COL_WARN : COL_DIM,
        COL_PANEL, 2, ML_DATUM);

  const int16_t barX = (int16_t)(r.x + 52);
  const int16_t barW = (int16_t)(r.w - 52 - 44);
  const int16_t barY = (int16_t)(r.y + 7);
  const int16_t barH = (int16_t)(r.h - 14);
  tft.fillRoundRect(barX, barY, barW, barH, 3, COL_LINE);
  const int16_t fill = (int16_t)((int32_t)barW * vol / 100);
  if (fill > 0) {
    tft.fillRoundRect(barX, barY, fill, barH, 3,
                      !linked ? COL_DIM : (muted ? COL_WARN : COL_ACCENT));
  }

  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", vol);
  label(r.x + r.w - 6, r.y + r.h / 2, buf, linked ? COL_TEXT : COL_DIM,
        COL_PANEL, 2, MR_DATUM);
}
#endif

void drawPowerButton(const UiStatus& st) {
  const Rect r = powerRect();
  const uint16_t bg = st.ampBusy ? COL_WARN : (st.ampOn ? COL_ON : COL_PANEL);
  const uint16_t fg = (st.ampBusy || st.ampOn) ? COL_BG : COL_TEXT;
  tft.fillRoundRect(r.x, r.y, r.w, r.h, RAD, bg);
  labelF(r.x + r.w / 2, r.y + 18,
         st.ampBusy ? "..." : (st.ampOn ? "ON" : "OFF"), fg,
         &FreeSansBold18pt7b, MC_DATUM);
  label(r.x + r.w / 2, r.y + r.h - 11, "POWER",
        (st.ampBusy || st.ampOn) ? COL_BG : COL_DIM, bg, 2, MC_DATUM);
}

// The two right-hand buttons share one strict pattern: dim caption above,
// value below, both centred.
void drawModeButton(const char* name) {
  const Rect r = vizRect();
  tft.fillRoundRect(r.x, r.y, r.w, r.h, RAD, COL_PANEL);
  label(r.x + r.w / 2, r.y + 13, "DISPLAY", COL_DIM, COL_PANEL, 2, MC_DATUM);
  label(r.x + r.w / 2, r.y + 35, name, COL_ACCENT, COL_PANEL, 2, MC_DATUM);
}

void drawMenuButton() {
  const Rect r = menuRect();
  tft.fillRoundRect(r.x, r.y, r.w, r.h, RAD, COL_PANEL);
  label(r.x + r.w / 2, r.y + 13, "PANEL", COL_DIM, COL_PANEL, 2, MC_DATUM);
  label(r.x + r.w / 2, r.y + 35, "MENU", COL_TEXT, COL_PANEL, 2, MC_DATUM);
}

void clearVizCache() {
  for (int i = 0; i < kVizBars; ++i) {
    sLastBar[i] = -1.0f;
    sLastPeak[i] = -1.0f;
  }
  sLastVu = -1;
  sLastVuPeak = -1;
  for (int i = 0; i < audio::kScopePoints; ++i) sScopeY[i] = -1;
}

// After a full screen overlay has covered everything, nothing may be assumed
// to still be on the display.
void forceFullRedraw() {
  tft.fillScreen(COL_BG);
  clearVizCache();
  drawVizFrame();
  sShownSource = -1;
  sShownAmp = -1;
  sShownBt = -1;
  sShownTrack = -2;
  sShownVol = -1;
  sShownHid = -1;
  sShownViz = -1;
  sLastDrawnViz = -1;
  sMarqueeText[0] = '\0';   // force marquee rebuild
  sMarqueeOffset = 0;
}

// Track-info band: marquee text on the left, four AVRCP buttons on the right.
// In non-BT builds: hairline divider.

// Build the combined track string into sMarqueeText; reset scroll if changed.
static void updateMarqueeText(const UiStatus& st) {
  char fresh[160];
#if CYD_BT_SINK
  if (st.btPresent && (st.btConnected || *st.trackTitle || *st.trackArtist)) {
    if (*st.trackTitle && *st.trackArtist) {
      snprintf(fresh, sizeof(fresh), "%s  \xC3\xB7  %s", st.trackTitle, st.trackArtist);
    } else if (*st.trackTitle) {
      strncpy(fresh, st.trackTitle, sizeof(fresh) - 1);
      fresh[sizeof(fresh)-1] = '\0';
    } else if (*st.trackArtist) {
      strncpy(fresh, st.trackArtist, sizeof(fresh) - 1);
      fresh[sizeof(fresh)-1] = '\0';
    } else {
      strncpy(fresh, st.btText, sizeof(fresh) - 1);
      fresh[sizeof(fresh)-1] = '\0';
    }
  } else {
    fresh[0] = '\0';
  }
#else
  fresh[0] = '\0';
  (void)st;
#endif
  if (strcmp(fresh, sMarqueeText) != 0) {
    strncpy(sMarqueeText, fresh, sizeof(sMarqueeText) - 1);
    sMarqueeText[sizeof(sMarqueeText)-1] = '\0';
    sMarqueeOffset = 0;
    sMarqueeNext = millis() + kMarqueePause;
  }
}

void drawTrackBand(const UiStatus& st) {
  tft.fillRect(0, TRK_Y, W, TRK_H, COL_BG);

#if CYD_BLE_HID
  // No A2DP, so there is no track metadata: this band carries the PC volume
  // strip instead - the touch "encoder", dragged like a knob.
  drawPcVolumeStrip();
  (void)st;
  return;
#endif

#if CYD_BT_SINK
  if (st.btPresent) {
    // Four AVRCP control buttons packed at the right edge.
    const Rect rPrev = avrcPrevRect();
    const Rect rPlay = avrcPlayRect();
    const Rect rPaus = avrcPauseRect();
    const Rect rNext = avrcNextRect();
    const bool conn = st.btConnected;
    const uint16_t bc = conn ? COL_PANEL_ON : COL_PANEL;
    const uint16_t fc = conn ? COL_TEXT     : COL_DIM;
    chip(rPrev, bc, fc, "|<");
    chip(rPlay, bc, fc, ">");
    chip(rPaus, bc, fc, "||");
    chip(rNext, bc, fc, ">|");
  }

  // Marquee text area: everything to the left of the buttons.
  const int16_t textAreaW = (st.btPresent)
      ? avrcPrevRect().x - MARGIN - 2
      : W - 2 * MARGIN;
  if (*sMarqueeText) {
    const uint16_t col = strcmp(st.btText, "PLAYING") == 0 ? COL_TEXT : COL_DIM;
    tft.setTextFont(1);
    const int16_t fullW = (int16_t)tft.textWidth(sMarqueeText, 1);
    tft.setTextColor(col, COL_BG);
    tft.setTextDatum(ML_DATUM);
    if (fullW <= textAreaW) {
      // Fits — draw in place.
      tft.drawString(sMarqueeText, MARGIN + 2, TRK_Y + TRK_H / 2, 1);
    } else {
      // Scrolling: use a TFT_eSprite as an off-screen buffer so we can clip
      // without setClipRect (which TFT_eSPI doesn't expose at this version).
      TFT_eSprite spr(&tft);
      spr.setColorDepth(16);
      if (spr.createSprite(textAreaW, TRK_H)) {
        spr.fillSprite(COL_BG);
        spr.setTextFont(1);
        spr.setTextColor(col, COL_BG);
        spr.setTextDatum(ML_DATUM);
        spr.drawString(sMarqueeText, (int16_t)(2 - sMarqueeOffset),
                       TRK_H / 2, 1);
        spr.pushSprite(MARGIN + 2, TRK_Y);
        spr.deleteSprite();
      }

      // Advance the scroll on the next tick.
      const uint32_t now = millis();
      if (now >= sMarqueeNext) {
        sMarqueeNext = now + kMarqueeMs;
        sMarqueeOffset = (int16_t)(sMarqueeOffset + kMarqueeStep);
        // Wrap: pause, then scroll from the start again.
        if (sMarqueeOffset > fullW + textAreaW / 2) {
          sMarqueeOffset = 0;
          sMarqueeNext = now + kMarqueePause;
        }
      }
    }
    return;
  }
#endif
  // No BT or no metadata: hairline divider.
  tft.drawFastHLine(MARGIN, TRK_Y + TRK_H / 2, W - 2 * MARGIN, COL_LINE);
  (void)st;
}

// The meter's frame: one rounded border.  Redrawn wherever the whole
// visualiser area is refilled.
void drawVizFrame() {
  tft.drawRoundRect(MARGIN, VIZ_Y, W - 2 * MARGIN, VIZ_H, RAD, COL_PANEL);
}

void drawBars(const audio::Snapshot& snap) {
  const int16_t gap = 3;
  const int16_t inset = MARGIN + 5;
  const int16_t areaW = W - 2 * inset;
  const int16_t bw = (areaW - (kVizBars - 1) * gap) / kVizBars;
  const int16_t bottom = VIZ_Y + VIZ_H - 9;      // inside the frame
  const int16_t trackTop = VIZ_Y + 25;           // below the captions
  const int16_t usable = bottom - trackTop;

  if (sLastBar[0] < 0.0f) {
    // Meter furniture, once per layout: frequency captions and the full
    // height track every bar grows and shrinks against.
    label(inset, VIZ_Y + 9, "40 Hz", COL_DIM, COL_BG, 2, ML_DATUM);
    label(W - inset, VIZ_Y + 9, "16 kHz", COL_DIM, COL_BG, 2, MR_DATUM);
    for (int i = 0; i < kVizBars; ++i) {
      tft.fillRect((int16_t)(inset + i * (bw + gap)), trackTop, bw, usable,
                   COL_LINE);
    }
  }

  for (int i = 0; i < kVizBars; ++i) {
    const int16_t x = (int16_t)(inset + i * (bw + gap));
    const int h = (int)(snap.band[i] * usable);
    const int lastH = sLastBar[i] < 0 ? 0 : (int)(sLastBar[i] * usable);
    const int16_t top = (int16_t)(bottom - h);
    const int16_t lastTop = (int16_t)(bottom - lastH);

    // Four-tier meter colour: green, cyan, amber, red.
    uint16_t col = COL_ON;
    if (snap.band[i] > 0.86f) {
      col = COL_HOT;
    } else if (snap.band[i] > 0.62f) {
      col = COL_WARN;
    } else if (snap.band[i] > 0.40f) {
      col = COL_ACCENT;
    }

    // Retire last frame's peak cap first, so a grown bar can never be
    // punched through by the erase.
    const int16_t lastPTop = (int16_t)(bottom - (int)(sLastPeak[i] * usable));
    if (sLastPeak[i] >= 0 && lastPTop - 2 >= trackTop)
      tft.fillRect(x, (int16_t)(lastPTop - 2), bw, 2, COL_LINE);

    if (h > lastH) {
      tft.fillRect(x, top, bw, h - lastH, col);
    } else if (h < lastH) {
      tft.fillRect(x, lastTop, bw, lastH - h, COL_LINE);  // back to track
    }
    sLastBar[i] = snap.band[i];

    const int16_t pTop = (int16_t)(bottom - (int)(snap.peak[i] * usable));
    if (snap.peak[i] > 0.01f) tft.fillRect(x, (int16_t)(pTop - 2), bw, 2,
                                           COL_TEXT);
    sLastPeak[i] = snap.peak[i];
  }
}

void drawVu(const audio::Snapshot& snap) {
  // Strict three-part layout inside the frame: caption left, big value right,
  // track below, scale labels on thirds.
  const int16_t inset = MARGIN + 5;
  label(inset, VIZ_Y + 18, "LEVEL", COL_DIM, COL_BG, 2, ML_DATUM);

  char buf[16];
  const float db = CFG_METER_FLOOR_DB * (1.0f - snap.level);
  snprintf(buf, sizeof(buf), "%+.0f", db);
  labelF(W - MARGIN - 34, VIZ_Y + 16, buf, COL_TEXT, &FreeSansBold18pt7b,
         MR_DATUM);
  label(W - MARGIN - 6, VIZ_Y + 16, "dB", COL_DIM, COL_BG, 2, MR_DATUM);

  const Rect r{inset, VIZ_Y + 36, (int16_t)(W - 2 * inset), 40};
  if (sLastVu < 0) {
    tft.fillRoundRect(r.x, r.y, r.w, r.h, RAD, COL_LINE);
  }

  const int16_t fill = (int16_t)(snap.level * r.w);
  if (fill > sLastVu) {
    tft.fillRect((int16_t)(r.x + (sLastVu < 0 ? 0 : sLastVu)), r.y,
                 (int16_t)(fill - (sLastVu < 0 ? 0 : sLastVu)), r.h,
                 snap.level > 0.86f ? COL_HOT
                                    : snap.level > 0.62f ? COL_WARN
                                    : snap.level > 0.40f ? COL_ACCENT
                                                         : COL_ON);
  } else if (fill < sLastVu) {
    tft.fillRect((int16_t)(r.x + fill), r.y, (int16_t)(sLastVu - fill), r.h,
                 COL_LINE);
  }
  sLastVu = fill;

  // Scale ticks drawn over whatever is underneath, so they always show.
  tft.fillRect((int16_t)(r.x + r.w / 4), r.y, 1, r.h, COL_BG);
  tft.fillRect((int16_t)(r.x + r.w / 2), r.y, 1, r.h, COL_BG);
  tft.fillRect((int16_t)(r.x + 3 * r.w / 4), r.y, 1, r.h, COL_BG);

  const int16_t pu = (int16_t)(snap.hold * r.w);
  if (sLastVuPeak >= 0) tft.fillRect((int16_t)(r.x + sLastVuPeak),
                                     (int16_t)(r.y - 5), 3, 4, COL_BG);
  if (pu > 0) tft.fillRect((int16_t)(r.x + pu - 1), (int16_t)(r.y - 5), 3, 4,
                           COL_TEXT);
  sLastVuPeak = pu;

  label(inset, VIZ_Y + 90, "-60", COL_DIM, COL_BG, 2, ML_DATUM);
  label(W / 2, VIZ_Y + 90, "-30", COL_DIM, COL_BG, 2, MC_DATUM);
  label(W - inset, VIZ_Y + 90, "0", COL_DIM, COL_BG, 2, MR_DATUM);
}

void drawScope(const audio::Snapshot& snap) {
  const int16_t inset = MARGIN + 5;
  const int16_t mid = VIZ_Y + VIZ_H / 2;
  const float scale = (VIZ_H / 2 - 10) * 4.0f;   // x4: the tap sits low
  const int16_t step =
      (int16_t)((W - 2 * inset) / (audio::kScopePoints - 1));

  // Erase last frame's trace where it was (3 px wide covers the 1 px line).
  if (sScopeY[0] >= 0) {
    for (int i = 0; i < audio::kScopePoints - 1; ++i) {
      const int16_t x0 = (int16_t)(inset + i * step);
      const int16_t x1 = (int16_t)(x0 + step);
      for (int16_t d = -1; d <= 1; ++d) {
        tft.drawLine(x0, (int16_t)(sScopeY[i] + d), x1,
                     (int16_t)(sScopeY[i + 1] + d), COL_BG);
      }
    }
  }

  // Grid over the erased area, then the centre axis a shade brighter.
  for (int16_t gx = (int16_t)(inset + 24); gx < W - inset; gx += 40) {
    tft.drawFastVLine(gx, (int16_t)(VIZ_Y + 3), (int16_t)(VIZ_H - 6),
                      COL_LINE);
  }
  tft.drawFastHLine(inset, mid, (int16_t)(W - 2 * inset), COL_PANEL);

  int16_t px = inset;
  int16_t py = (int16_t)(mid - snap.scope[0] * scale);
  for (int i = 0; i < audio::kScopePoints; ++i) {
    const int16_t x = (int16_t)(inset + i * step);
    int16_t y = (int16_t)(mid - snap.scope[i] * scale);
    y = constrain(y, (int16_t)(VIZ_Y + 4), (int16_t)(VIZ_Y + VIZ_H - 5));
    if (i > 0) tft.drawLine(px, py, x, y, COL_ACCENT);
    sScopeY[i] = y;          // clamped: the erase has to match the line
    px = x;
    py = y;
  }
}void drawSettingsOverlay() {
  tft.fillScreen(COL_BG);
  // Header strip with an accent rule, the same on every full screen.
  tft.fillRect(0, 0, W, 24, COL_PANEL);
  labelF(MARGIN + 4, 13, "SETTINGS", COL_TEXT, &FreeSansBold12pt7b, ML_DATUM);
  label(W - MARGIN - 4, 13, "PANEL CONFIG", COL_DIM, COL_PANEL, 2, MR_DATUM);
  tft.drawFastHLine(MARGIN, 25, (int16_t)(W - 2 * MARGIN), COL_ACCENT);

  char buf[32];
  const SettingsData& d = sSettings->data();

  // Rows 0..4 are flat value rows: label on the left margin, value in a
  // column that ends where the arrow chips begin, hairline divider below.
  for (int i = 0; i <= 4; ++i) {
    const Rect r = overlayRow(i);
    tft.drawFastHLine((int16_t)(MARGIN + 6), (int16_t)(r.y + r.h),
                      (int16_t)(W - 2 * MARGIN - 12), COL_LINE);
  }

  const Rect r0 = overlayRow(0);
  label(MARGIN + 10, r0.y + r0.h / 2,
        CFG_BL_FITTED ? "BRIGHTNESS" : "BACKLIGHT", COL_DIM, COL_BG, 2,
        ML_DATUM);
#if CFG_BL_FITTED
  snprintf(buf, sizeof(buf), "%d", sBrightness);
  label(r0.x + r0.w - 58, r0.y + r0.h / 2, buf, COL_TEXT, COL_BG, 2, MR_DATUM);
  for (int side = 0; side < 2; ++side) {
    const Rect c = overlayArrow(0, side == 1);
    tft.fillRoundRect(c.x, (int16_t)(c.y + 4), c.w, (int16_t)(c.h - 8), 4,
                      COL_PANEL_ON);
    label(c.x + c.w / 2, c.y + c.h / 2, side == 1 ? ">" : "<", COL_TEXT,
          COL_PANEL_ON, 2, MC_DATUM);
  }
#else
  label(r0.x + r0.w - 10, r0.y + r0.h / 2, "no PWM (I2S)", COL_DIM, COL_BG, 2,
        MR_DATUM);
#endif

  const Rect r1 = overlayRow(1);
  label(MARGIN + 10, r1.y + r1.h / 2, "AUTO DIM", COL_DIM, COL_BG, 2,
        ML_DATUM);
  label(r1.x + r1.w - 10, r1.y + r1.h / 2, d.autoDim ? "ON" : "OFF",
        d.autoDim ? COL_ON : COL_DIM, COL_BG, 2, MR_DATUM);

  const Rect r2 = overlayRow(2);
  label(MARGIN + 10, r2.y + r2.h / 2, "VISUALISER", COL_DIM, COL_BG, 2,
        ML_DATUM);
  label(r2.x + r2.w - 10, r2.y + r2.h / 2,
        kVizName[d.visualizer % VIZ_COUNT], COL_ACCENT, COL_BG, 2, MR_DATUM);

  const Rect r3 = overlayRow(3);
  snprintf(buf, sizeof(buf), "x%.1f", d.meterGain);
  label(MARGIN + 10, r3.y + r3.h / 2, "METER GAIN", COL_DIM, COL_BG, 2,
        ML_DATUM);
  label(r3.x + r3.w - 58, r3.y + r3.h / 2, buf, COL_TEXT, COL_BG, 2, MR_DATUM);
  for (int side = 0; side < 2; ++side) {
    const Rect c = overlayArrow(3, side == 1);
    tft.fillRoundRect(c.x, (int16_t)(c.y + 4), c.w, (int16_t)(c.h - 8), 4,
                      COL_PANEL_ON);
    label(c.x + c.w / 2, c.y + c.h / 2, side == 1 ? ">" : "<", COL_TEXT,
          COL_PANEL_ON, 2, MC_DATUM);
  }

  const Rect r4 = overlayRow(4);
  label(MARGIN + 10, r4.y + r4.h / 2, "POWER ON BOOT", COL_DIM, COL_BG, 2,
        ML_DATUM);
  label(r4.x + r4.w - 10, r4.y + r4.h / 2, d.powerOnBoot ? "ON" : "OFF",
        d.powerOnBoot ? COL_ON : COL_DIM, COL_BG, 2, MR_DATUM);

  // Rows 5..7 are actions: solid chips instead of flat rows.
  const Rect r5 = overlayRow(5);
  tft.fillRoundRect(r5.x, r5.y, r5.w, r5.h, RAD, COL_PANEL);
  label(MARGIN + 10, r5.y + r5.h / 2, "CALIBRATE TOUCH", COL_WARN,
        COL_PANEL, 2, ML_DATUM);
  label(r5.x + r5.w - 12, r5.y + r5.h / 2, ">", COL_DIM, COL_PANEL, 2,
        MR_DATUM);

  const Rect r6 = overlayRow(6);
  tft.fillRoundRect(r6.x, r6.y, r6.w, r6.h, RAD, COL_PANEL);
  label(MARGIN + 10, r6.y + r6.h / 2, "DIAGNOSTICS", COL_TEXT, COL_PANEL, 2,
        ML_DATUM);
  label(r6.x + r6.w - 12, r6.y + r6.h / 2, ">", COL_DIM, COL_PANEL, 2,
        MR_DATUM);

  chip(overlayRow(7), COL_ACCENT, COL_BG, "CLOSE");
}

// The diagnostics screen used to blank the whole display on every pass to get
// clean text under the values that move (heap, raw touch, uptime).  At four
// passes a second that is a black flash several times a second - plainly
// visible strobing - so each row now repaints only when its own text or colour
// actually changed, over the fill done when the screen was entered.
constexpr int kDiagMaxRows = 16;
struct DiagCache {
  char text[48];
  uint16_t col;
};
DiagCache sDiagCache[kDiagMaxRows];
int sDiagIdx = 0;
bool sDiagFullRedraw = true;

void diagRow(int16_t y, const char* text, uint16_t col) {
  if (sDiagIdx >= kDiagMaxRows) return;
  DiagCache& c = sDiagCache[sDiagIdx++];
  if (!sDiagFullRedraw && c.col == col && strcmp(c.text, text) == 0) return;
  strncpy(c.text, text, sizeof(c.text) - 1);
  c.text[sizeof(c.text) - 1] = '\0';
  c.col = col;
  tft.fillRect(0, y - 8, W, 16, COL_BG);
  label(8, y, text, col, COL_BG, 2, ML_DATUM);
}

void drawDiagnostics(const UiStatus& st, const audio::Snapshot& snap) {
  sDiagIdx = 0;
  tft.fillRect(0, 0, W, 24, COL_PANEL);
  labelF(MARGIN + 4, 13, "DIAGNOSTICS", COL_TEXT, &FreeSansBold12pt7b,
         ML_DATUM);
  label(W - MARGIN - 4, 13, "SELF TEST", COL_DIM, COL_PANEL, 2, MR_DATUM);
  tft.drawFastHLine(MARGIN, 25, (int16_t)(W - 2 * MARGIN), COL_ACCENT);

  char line[48];
  int y = 38;
  const int dy = 14;

  snprintf(line, sizeof(line), "amp %-6s mute %-3s src %s", st.ampText,
           ioChannel(CH_MUTE).bound()
               ? (ioChannel(CH_MUTE).state() ? "on" : "off")
               : "n/a",
           st.source < CFG_SOURCE_COUNT ? CFG_SOURCES[st.source].label : "?");
  diagRow(y, line, COL_TEXT);
  y += dy;

  const int step = wiretest::step();
  for (int i = 0; i < CFG_SOURCE_COUNT; ++i) {
    const SourceDef& d = CFG_SOURCES[i];
    OutputPin& p = ioChannel(d.channel);
    // A source with no relay of its own is wired straight to the boards: say so
    // instead of printing the pin as "unused".
    snprintf(line, sizeof(line), "  %-10s %-9s %-3s %s", d.label,
             p.bound() ? ioDescription(d.channel) : "direct",
             p.bound() ? (p.state() ? "ON" : "off") : "--",
             wiretest::resultText(d.channel));
    const bool testing = step >= 0 && step == d.channel;
    diagRow(y, line,
            testing ? COL_WARN : (i == st.source ? COL_ACCENT : COL_DIM));
    y += dy;
  }

  {
    // The three switching channels of this build on one row: 24 V bus, mute
    // relay and the spare key.  The pin behind each is in the serial log.
    static const int8_t kCh[3] = {CH_AMP_POWER, CH_MUTE, CH_SPARE_0};
    static const char* kTag[3] = {"bus", "mute", "key"};
    char cells[3][16];
    for (int i = 0; i < 3; ++i) {
      OutputPin& p = ioChannel(kCh[i]);
      snprintf(cells[i], sizeof(cells[i]), "%s %-3s %-3s", kTag[i],
               p.bound() ? (p.state() ? "ON" : "off") : "--",
               wiretest::resultText(kCh[i]));
    }
    const bool testing =
        step >= 0 && (step == kCh[0] || step == kCh[1] || step == kCh[2]);
    snprintf(line, sizeof(line), "%s %s %s", cells[0], cells[1], cells[2]);
    diagRow(y, line, testing ? COL_WARN : COL_DIM);
    y += dy;
  }

  if (wiretest::tested() > 0) {
    snprintf(line, sizeof(line), "wiring %d ch, %d FAIL", wiretest::tested(),
             wiretest::failures());    diagRow(y, line, wiretest::failures() ? COL_HOT : COL_ON);
  } else {
    diagRow(y, "wiring test not run", COL_DIM);
  }
  y += dy;

  {
    // Raw controller readings: with an uncalibrated panel these are the only
    // numbers that say whether anything is coming back at all.
    const touch::Raw& t = touch::last();
    snprintf(line, sizeof(line), "touch %-3s raw %u,%u z %d",
             touch::calibrated() ? "cal" : "RAW", t.x, t.y, t.pressure);    diagRow(y, line, touch::calibrated() ? COL_DIM : COL_WARN);
    y += dy;
  }

  snprintf(line, sizeof(line), "tap %.0f Hz  %u bands  %.0f fps",
           snap.sampleRate, dsp::kBands, snap.stats.frameHz);
  diagRow(y, line, COL_DIM);
  y += dy;

  if (snap.lowHz > 0) {
    snprintf(line, sizeof(line), "band %0.f-%0.f Hz  floor %.0f dB", snap.lowHz,
             snap.highHz, CFG_METER_FLOOR_DB);
    diagRow(y, line, COL_DIM);
    y += dy;
  }

#if CYD_BT_SINK
  // No ADC in this build: the meter runs on the decoded stream.
  snprintf(line, sizeof(line), "bt %-9s %s", st.btText,
           st.btConnected ? "linked" : "no link");    diagRow(y, line, st.btConnected ? COL_BT : COL_DIM);
#elif CFG_ENC_SHADOWS_TAP
  // The encoder sits on the tap pin, so there is nothing to sample.
  snprintf(line, sizeof(line), "adc tap off - GPIO%d is the encoder",
           CFG_PIN_AUDIO_ADC);
  diagRow(y, line, COL_WARN);
#else
  snprintf(line, sizeof(line), "adc raw %.0f..%.0f  dc %.2f%s",
           snap.stats.minRaw, snap.stats.maxRaw, snap.stats.dcLevel,
           snap.stats.clipping ? " CLIP" : "");    diagRow(y, line, snap.stats.clipping ? COL_HOT : COL_DIM);
#endif
  y += dy;

  snprintf(line, sizeof(line), "heap %u  lvl %.2f  peak %.2f", ESP.getFreeHeap(),
           snap.level, snap.hold);
  diagRow(y, line, COL_DIM);
  y += dy;

  snprintf(line, sizeof(line), "uptime %lu s", millis() / 1000UL);
  diagRow(y, line, COL_DIM);
  y += dy;

#if CYD_BLE_HID
  // The PC link: what the radio is doing, and what the panel thinks the PC's
  // volume is (HID only sends keys, it never asks).
  snprintf(line, sizeof(line), "pc keys %-8s vol ~%d%%%s", hid::stateText(),
           hid::volumeEstimate(), hid::muteEstimate() ? " MUTED" : "");
  diagRow(y, line, hid::connected() ? COL_ON : COL_DIM);
#endif

  // Wiring test: it drives the amplifier relay too, so it is refused while the
  // amplifier is on.
  const Rect btn = diagTestRect();
  const bool busy = wiretest::running();
  const char* text = busy ? "TESTING..."
                          : (st.ampOn ? "AMP IS ON" : "RUN WIRING TEST");
  const uint16_t bg = busy ? COL_PANEL_ON : COL_PANEL;
  tft.fillRoundRect(btn.x, btn.y, btn.w, btn.h, 5, bg);
  label(btn.x + btn.w / 2, btn.y + btn.h / 2, text, st.ampOn && !busy ? COL_HOT
                                                                      : COL_TEXT,
        bg, 2, MC_DATUM);

  // 440 Hz through the same output the music uses, so the amplifier can be
  // checked without pairing a phone.
  if (st.toneAvailable) {
    const Rect tb = diagToneRect();
    const uint16_t tbg = st.toneOn ? COL_PANEL_ON : COL_PANEL;
    tft.fillRoundRect(tb.x, tb.y, tb.w, tb.h, 5, tbg);
    label(tb.x + tb.w / 2, tb.y + tb.h / 2, st.toneOn ? "TONE ON" : "TEST TONE",
          st.toneOn ? COL_ACCENT : COL_TEXT, tbg, 2, MC_DATUM);
  }

  label(W - 16, H - 16, "TAP ELSEWHERE TO CLOSE", COL_WARN, COL_BG, 2,
        MR_DATUM);
  sDiagFullRedraw = false;
}

void drawVisualizer(const audio::Snapshot& snap) {
  const uint8_t mode = sSettings->data().visualizer % VIZ_COUNT;
  switch (mode) {
    case VIZ_BARS: drawBars(snap); break;
    case VIZ_VU: drawVu(snap); break;
    case VIZ_SCOPE: drawScope(snap); break;
    default:
      // Nothing to animate: the caller has already cleared the area, draw the
      // label once.
      if (sLastDrawnViz != VIZ_OFF) {
        label(W / 2, VIZ_Y + VIZ_H / 2, "VISUALISER OFF", COL_DIM, COL_BG, 2,
              MC_DATUM);
      }
      break;
  }
  sLastDrawnViz = (int8_t)mode;
}

// Waits for a finger to arrive, stay for a few samples and leave again.  That
// way the reading is not taken from the first (unsettled) conversion, and a
// finger left on the panel from the previous cross cannot count twice.  False
// on timeout.
bool waitForTap(uint16_t& x, uint16_t& y, uint32_t timeoutMs) {
  const uint32_t deadline = millis() + timeoutMs;
  while (touch::pressed() && millis() < deadline) {
    touch::update();
    delay(10);
  }

  uint32_t sumX = 0;
  uint32_t sumY = 0;
  int taken = 0;
  while (millis() < deadline) {
    touch::update();
    if (touch::pressed()) {
      sumX += touch::last().x;
      sumY += touch::last().y;
      if (++taken >= 4) {
        x = (uint16_t)(sumX / (uint32_t)taken);
        y = (uint16_t)(sumY / (uint32_t)taken);
        return true;
      }
    } else if (taken > 0) {
      taken = 0;                 // a flicker, start the average over
    }
    delay(15);
  }
  return false;
}

void drawCross(int16_t x, int16_t y, uint16_t colour) {
  tft.drawFastHLine(x - 12, y, 25, colour);
  tft.drawFastVLine(x, y - 12, 25, colour);
  tft.drawCircle(x, y, 16, colour);
}

// Blocking by nature: the user is standing at the panel and taps four crosses.
// Every wait here has a timeout, because a panel whose touch controller is dead
// or unplugged must not hang the whole firmware - the boot log has to reach the
// serial port either way, and one tap on the screen runs the calibration again.
void calibrate() {
  const int16_t xs[2] = {touch::kCalX0, touch::kCalX1};
  const int16_t ys[2] = {touch::kCalY0, touch::kCalY1};
  const int16_t cornerX[4] = {0, 1, 1, 0};        // top left, top right,
  const int16_t cornerY[4] = {0, 0, 1, 1};        // bottom right, bottom left
  static const char* kWay[4] = {"top left", "top right", "bottom right",
                                "bottom left"};
  uint16_t raw[4][2];                             // raw x, raw y per corner

  tft.fillScreen(COL_BG);
  touch::update();
  const touch::Raw idle = touch::last();
  Serial.println("touch        : tap the four crosses, one at a time");
  Serial.printf("touch        : idle raw %u,%u z1 %u z2 %u z %d - nothing "
                "pressed\n",
                idle.x, idle.y, idle.z1, idle.z2, idle.pressure);

  for (int done = 0; done < 4; ++done) {
    tft.fillRect(0, 0, W, 66, COL_BG);
    labelF(W / 2, 13, "TOUCH CALIBRATION", COL_TEXT, &FreeSansBold12pt7b,
           MC_DATUM);
    tft.drawFastHLine(84, 26, (int16_t)(W - 168), COL_ACCENT);
    char hint[44];
    snprintf(hint, sizeof(hint), "TAP THE CROSS: %s  (%d/4)", kWay[done],
             done + 1);
    label(W / 2, 42, hint, COL_ACCENT, COL_BG, 2, MC_DATUM);
    // Progress dots: four slots, filled as each corner is accepted.
    for (int i = 0; i < 4; ++i) {
      tft.fillCircle((int16_t)(W / 2 - 18 + i * 12), 58, 3,
                     i < done ? COL_ON : COL_LINE);
    }
    for (int i = 0; i < 4; ++i) {
      drawCross(xs[cornerX[i]], ys[cornerY[i]], i < done ? COL_ON : COL_DIM);
    }
    drawCross(xs[cornerX[done]], ys[cornerY[done]], COL_WARN);

    uint16_t rx = 0;
    uint16_t ry = 0;
    if (!waitForTap(rx, ry, 20000)) {
      Serial.printf("touch        : no response at the %s cross, booting "
                    "uncalibrated (tap the screen to try again)\n", kWay[done]);
      drawCross(xs[cornerX[done]], ys[cornerY[done]], COL_HOT);
      tft.fillScreen(COL_BG);
      forceFullRedraw();
      return;
    }
    raw[done][0] = rx;
    raw[done][1] = ry;
    Serial.printf("touch        : %-12s raw %u,%u\n", kWay[done], rx, ry);
  }

  // Which raw channel runs along which screen axis, and in which direction.  A
  // channel that changes a lot from the left crosses to the right ones is the
  // screen X axis; the two axes have to use different channels, so picking one
  // decides the other.  Averaging opposite corners keeps a sloppy tap from
  // dragging the mapping with it.
  const int32_t leftX = ((int32_t)raw[0][0] + raw[3][0]) / 2;
  const int32_t rightX = ((int32_t)raw[1][0] + raw[2][0]) / 2;
  const int32_t topX = ((int32_t)raw[0][0] + raw[1][0]) / 2;
  const int32_t bottomX = ((int32_t)raw[3][0] + raw[2][0]) / 2;
  const int32_t leftY = ((int32_t)raw[0][1] + raw[3][1]) / 2;
  const int32_t rightY = ((int32_t)raw[1][1] + raw[2][1]) / 2;
  const int32_t topY = ((int32_t)raw[0][1] + raw[1][1]) / 2;
  const int32_t bottomY = ((int32_t)raw[3][1] + raw[2][1]) / 2;

  const int32_t spanXrawX = rightX - leftX;
  const int32_t spanXrawY = rightY - leftY;

  int16_t cal[6] = {0, 0, 0, 0, 0, 0};
  const bool xFromRawX = labs(spanXrawX) >= labs(spanXrawY);
  cal[0] = xFromRawX ? 0 : 1;
  cal[3] = xFromRawX ? 1 : 0;
  cal[1] = (int16_t)(xFromRawX ? leftX : leftY);
  cal[2] = (int16_t)(xFromRawX ? rightX : rightY);
  cal[4] = (int16_t)(cal[3] == 0 ? topX : topY);
  cal[5] = (int16_t)(cal[3] == 0 ? bottomX : bottomY);

  if (!touch::setCalibration(cal)) {
    Serial.printf("touch        : calibration rejected, spans %d/%d are too "
                  "small to map\n", (int)(cal[2] - cal[1]),
                  (int)(cal[5] - cal[4]));
    tft.fillScreen(COL_BG);
    forceFullRedraw();
    return;
  }
  for (int i = 0; i < 6; ++i) sSettings->data().touchCal[i] = cal[i];
  sSettings->save();
  Serial.printf("touch        : calibrated, screen x = raw[%d] %d..%d, "
                "screen y = raw[%d] %d..%d\n",
                cal[0], cal[1], cal[2], cal[3], cal[4], cal[5]);
  tft.fillScreen(COL_BG);
  forceFullRedraw();
}

UiEvent hitOverlay(int16_t x, int16_t y, bool longPress) {
  UiEvent ev;
  SettingsData& d = sSettings->data();
  for (int i = 0; i < 8; ++i) {
    if (!overlayRow(i).has(x, y)) continue;
    switch (i) {
      case 0:
#if CFG_BL_FITTED
        if (overlayArrow(0, true).has(x, y)) {
          setBrightness(sBrightness + 24);
        } else if (overlayArrow(0, false).has(x, y)) {
          setBrightness(sBrightness - 24);
        }
        ev.action = UiAction::SettingsChanged;
#endif
        break;
      case 1:
        d.autoDim = !d.autoDim;
        sSettings->save();
        ev.action = UiAction::SettingsChanged;
        break;
      case 2:
        d.visualizer = (uint8_t)((d.visualizer + 1) % VIZ_COUNT);
        sSettings->save();
        clearVizCache();
        tft.fillRect(0, VIZ_Y, W, VIZ_H, COL_BG);
        ev.action = UiAction::SettingsChanged;
        break;
      case 3:
        if (overlayArrow(3, true).has(x, y)) {
          d.meterGain = constrain(d.meterGain * 1.3f, 0.2f, 20.0f);
        } else if (overlayArrow(3, false).has(x, y)) {
          d.meterGain = constrain(d.meterGain / 1.3f, 0.2f, 20.0f);
        } else {
          break;
        }
        audio::setGain(d.meterGain);
        sSettings->save();
        ev.action = UiAction::SettingsChanged;
        break;
      case 4:
        d.powerOnBoot = !d.powerOnBoot;
        sSettings->save();
        ev.action = UiAction::SettingsChanged;
        break;
      case 5:
        calibrate();
        break;
      case 6:
        sView = View::Diagnostics;
        sNextDiag = 0;
        tft.fillScreen(COL_BG);
        sDiagFullRedraw = true;      // over the fresh fill, paint every row
        break;
      default:
        sView = View::Main;
        forceFullRedraw();
        break;
    }
    // The overlay shows the values it edits, so it has to be redrawn.
    if (sView == View::Settings) drawSettingsOverlay();
    return ev;
  }
  return ev;
}

}  // namespace

// ------------------------------------------------------------------- public
void uiBegin(Settings* settings) {
  sSettings = settings;
  sBrightness = settings->data().brightness;
  if (sBrightness < CFG_BRIGHT_MIN) sBrightness = CFG_BRIGHT_MIN;

  tft.init();
  tft.setRotation(CFG_TFT_ROTATION);
  tft.fillScreen(COL_BG);
  tft.setTextWrap(false);

  touch::begin();
  touch::setCalibration(settings->data().touchCal);

#if CFG_BL_FITTED && !CYD_BL_STATIC
  ledcSetup(CFG_BL_CHANNEL, CFG_BL_FREQ, 8);
  ledcAttachPin(CFG_PIN_TFT_BL, CFG_BL_CHANNEL);
  applyBrightness();
  Serial.printf("backlight     : pwm %d/255, auto dim %s after %lu s\n",
                sBrightness, settings->data().autoDim ? "on" : "off",
                CFG_DIM_AFTER_MS / 1000UL);
#elif CFG_BL_FITTED
  // Static test build: tft.init() already drove the pin to TFT_BACKLIGHT_ON
  // and no timer is ever attached, so the level physically cannot change.
  Serial.println("backlight     : STATIC - PWM disabled (test build)");
#endif

  clearVizCache();
  drawVizFrame();
  sLastTouch = millis();

  // A panel that has never been calibrated cannot be used at all, so do it now.
  if (!touch::calibrated()) calibrate();
}

UiEvent uiPoll(const UiStatus& st) {
  UiEvent ev;
  touch::update();
  int16_t x = 0;
  int16_t y = 0;
  const uint32_t now = millis();

  // A panel that booted into the timeout is still uncalibrated, and without
  // calibration no button can be hit.  Any touch at all is the signal to run the
  // calibration again, wherever it lands.
  if (touch::pressed() && uncalibrated() && sView == View::Main) {
    calibrate();
    return ev;
  }

  const bool down = touch::read(x, y);

  if (down) {
    if (!sDown) {
      sDown = true;
      sDownX = (int16_t)x;
      sDownY = (int16_t)y;
      sDownAt = now;
      sLongFired = false;
#if CYD_BLE_HID
      sVolDragX = (int16_t)x;    // the strip steps from where the finger landed
      sVolNext = 0;
#endif
      wake();
    }
    if (!sLongFired && now - sDownAt >= CFG_BTN_LONG_MS) {
      sLongFired = true;
      if (sView == View::Main && powerRect().has(sDownX, sDownY)) {
        ev.action = UiAction::PowerOff;
      }
    }
#if CYD_BLE_HID
    if (sView == View::Main) {
      const int dir = volDirection(sDownX, sDownY);
      if (dir != 0) {
        // A volume chip held down repeats: the first step after
        // CFG_HID_REPEAT_DELAY_MS, then one every CFG_HID_REPEAT_MS.  Marking
        // the press as long-fired stops the release adding one more step.
        if (!sLongFired && now - sDownAt >= CFG_HID_REPEAT_DELAY_MS) {
          sLongFired = true;
          sVolNext = now;
        }
        if (sLongFired && now >= sVolNext) {
          sVolNext = now + CFG_HID_REPEAT_MS;
          ev.action = dir > 0 ? UiAction::VolumeUp : UiAction::VolumeDown;
          ev.arg = 1;
        }
        return ev;
      }
      if (pcVolumeRect().has(sDownX, sDownY)) {
        // The strip is the touch encoder: CFG_HID_DRAG_PX of travel per step,
        // and it follows the finger back and forth without lifting.
        const int steps = (int)(x - sVolDragX) / CFG_HID_DRAG_PX;
        if (steps != 0) {
          sVolDragX = (int16_t)(sVolDragX + steps * CFG_HID_DRAG_PX);
          int n = steps > 0 ? steps : -steps;
          if (n > 4) n = 4;      // the radio can only send so fast
          ev.action = steps > 0 ? UiAction::VolumeUp : UiAction::VolumeDown;
          ev.arg = n;
        }
        return ev;
      }
    }
#endif
    return ev;
  }

  if (!sDown) return ev;
  sDown = false;
  if (sLongFired) return ev;     // a chip that repeated has already done its work

#if CYD_BLE_HID
  if (sView == View::Main) {
    const int dir = volDirection(sDownX, sDownY);
    if (dir != 0) {
      ev.action = dir > 0 ? UiAction::VolumeUp : UiAction::VolumeDown;
      ev.arg = 1;
      return ev;
    }
    if (kVolChips && volMuteRect().has(sDownX, sDownY)) {
      ev.action = UiAction::VolumeMute;
      return ev;
    }
  }
#endif

  switch (sView) {
    case View::Settings:
      return hitOverlay(sDownX, sDownY, false);
    case View::Diagnostics:
      if (diagTestRect().has(sDownX, sDownY)) {
        if (!st.ampOn && !wiretest::running()) ev.action = UiAction::RunWiringTest;
        return ev;               // stay on the screen either way
      }
      if (st.toneAvailable && diagToneRect().has(sDownX, sDownY)) {
        ev.action = UiAction::ToggleTestTone;
        return ev;
      }
      sView = View::Main;
      forceFullRedraw();
      return ev;
    case View::Main:
      break;
  }

  // AVRCP buttons in the track band (BT build only).
#if CYD_BT_SINK
  if (st.btPresent) {
    auto doAvrcp = [&](const Rect& r, UiAction act) -> bool {
      if (!r.has(sDownX, sDownY)) return false;
      sFeedbackRect = r;
      sFeedbackUntil = millis() + 80;
      tft.fillRoundRect(r.x, r.y, r.w, r.h, RAD, COL_ACCENT);
      ev.action = act;
      return true;
    };
    if (doAvrcp(avrcPrevRect(), UiAction::AvrcPrev))  return ev;
    if (doAvrcp(avrcPlayRect(), UiAction::AvrcPlay))  return ev;
    if (doAvrcp(avrcPauseRect(),UiAction::AvrcPause)) return ev;
    if (doAvrcp(avrcNextRect(), UiAction::AvrcNext))  return ev;
  }
#endif

  for (int i = 0; i < CFG_SOURCE_COUNT; ++i) {
    if (sourceRect(i, CFG_SOURCE_COUNT).has(sDownX, sDownY)) {
      sFeedbackRect = sourceRect(i, CFG_SOURCE_COUNT);
      sFeedbackUntil = millis() + 80;
      ev.action = UiAction::SelectSource;
      ev.arg = i;
      return ev;
    }
  }
  if (powerRect().has(sDownX, sDownY)) {
    sFeedbackRect = powerRect();
    sFeedbackUntil = millis() + 80;
    ev.action = UiAction::PowerToggle;
    return ev;
  }
  if (vizRect().has(sDownX, sDownY)) {
    SettingsData& d = sSettings->data();
    d.visualizer = (uint8_t)((d.visualizer + 1) % VIZ_COUNT);
    sSettings->save();
    tft.fillRect(0, VIZ_Y, W, VIZ_H, COL_BG);
    clearVizCache();
    drawVizFrame();
    ev.action = UiAction::SettingsChanged;
    return ev;
  }
  if (menuRect().has(sDownX, sDownY)) {
    sView = View::Settings;
    drawSettingsOverlay();
    return ev;
  }
  // Anywhere else on the visualiser: cycle the mode as well.
  if (sDownY >= VIZ_Y && sDownY < VIZ_Y + VIZ_H) {
    SettingsData& d = sSettings->data();
    d.visualizer = (uint8_t)((d.visualizer + 1) % VIZ_COUNT);
    sSettings->save();
    tft.fillRect(0, VIZ_Y, W, VIZ_H, COL_BG);
    clearVizCache();
    drawVizFrame();
    ev.action = UiAction::SettingsChanged;
  }
  (void)st;
  return ev;
}

void uiRender(const UiStatus& st, const audio::Snapshot& snap) {
  const uint32_t now = millis();

  // Smooth backlight fade tick (every frame).
  fadeTick();

  // Button feedback: clear the highlight after 80 ms.
  if (sFeedbackUntil && now >= sFeedbackUntil) {
    sFeedbackUntil = 0;
    // Force a full redraw of the region that was highlighted.
    sShownSource = -1;
    sShownAmp = -1;
    sShownBt = -1;
    sShownTrack = -2;
  }

  switch (sView) {
    case View::Diagnostics:
      if (now >= sNextDiag) {
        sNextDiag = now + 250;
        drawDiagnostics(st, snap);
      }
      return;
    case View::Settings:
      return;
    case View::Main:
      break;
  }

  // Auto dim: smooth fade instead of a jump.
  const bool wantDim = sSettings->data().autoDim &&
                       (now - sLastTouch > CFG_DIM_AFTER_MS);
  if (wantDim != sDimmed) {
    sDimmed = wantDim;
    Serial.printf("[bl] auto dim %s\n", wantDim ? "on" : "off");
    startFade(wantDim ? CFG_DIM_BRIGHT : sBrightness);
  }

  // Update marquee text (resets scroll offset if track changed).
  updateMarqueeText(st);

#if CYD_BLE_HID
  // The status pill shows the PC link, and the volume row moves with every key
  // that goes out: both belong in the hashes that decide what is repainted.
  const int hidHash = !hid::ready() ? 0 : (hid::connected() ? 1 : 2);
  const int volHash = hid::volumeEstimate() * 4 +
                      (hid::muteEstimate() ? 2 : 0) +
                      (hid::connected() ? 1 : 0) + 100 * volPressedIndex();
#else
  const int hidHash = 0;
  const int volHash = sShownVol;   // no PC volume row in this build
#endif

  const int ampHash = (int)st.ampOn * 2 + (int)st.ampBusy + (st.busOk ? 0 : 4);
  if (sShownAmp != ampHash || sShownBt != (int)st.btState ||
      sShownHid != hidHash) {
    drawStatusBar(st);
    drawPowerButton(st);
    sShownAmp = ampHash;
    sShownBt = (int)st.btState;
    sShownHid = (int16_t)hidHash;
  }
  if (sShownSource != st.source || sShownVol != volHash) {
    drawSourceButtons(st);
#if CYD_BLE_HID
    if (kVolChips) drawVolumeChips();
#endif
    sShownSource = st.source;
    sShownVol = (int16_t)volHash;
  }
  // Track band: redraw every frame (marquee scrolls continuously).
  {
    int trackHash = (int)st.btState * 1000
        + (int)st.trackTitle[0] * 7 + (int)st.trackArtist[0] * 13;
#if CYD_BLE_HID
    // The band carries the PC volume strip, so its estimate is part of it.
    trackHash += hid::volumeEstimate() * 31 + (hid::muteEstimate() ? 7 : 0) +
                 (hid::connected() ? 13 : 0);
#endif
    for (int i = 1; i < 4 && st.trackTitle[i]; ++i)
      trackHash += (int)st.trackTitle[i] * (i + 1);
    for (int i = 1; i < 4 && st.trackArtist[i]; ++i)
      trackHash += (int)st.trackArtist[i] * (i + 5);
    // Always redraw if scrolling (marquee), otherwise only on change.
    const bool scrolling = *sMarqueeText != '\0';
    if (sShownTrack != trackHash || scrolling) {
      drawTrackBand(st);
      sShownTrack = trackHash;
    }
  }
  if (sShownViz != (int8_t)sSettings->data().visualizer) {
    tft.fillRect(0, VIZ_Y, W, VIZ_H, COL_BG);
    clearVizCache();
    drawVizFrame();
    sLastDrawnViz = -1;
    sShownViz = (int8_t)sSettings->data().visualizer;
  }
  drawModeButton(kVizName[sSettings->data().visualizer % VIZ_COUNT]);
  drawMenuButton();

  drawVisualizer(snap);
}

void uiSetBrightness(uint8_t value) { setBrightness(value); }
uint8_t uiBrightness() { return (uint8_t)sBrightness; }
bool uiOverlayOpen() { return sView != View::Main; }
