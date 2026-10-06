// touch.h - the CYD's resistive touch panel.
//
// TFT_eSPI cannot drive this controller: its touch handler shares the display
// SPI bus (GPIO 12/13/14 on this board), while the XPT2046 hangs on GPIO 25
// (clock), 32 (MOSI), 39 (MISO) and 33 (CS) - a bus of its own.  Talking to the
// controller over the display bus is why taps never registered, so the panel
// gets this small bit-banged driver instead.
//
// Two layers: raw readings straight from the controller (all an uncalibrated
// panel has, and enough to notice a tap and start the calibration), and
// calibrated screen coordinates on top of them.
#pragma once

#include <Arduino.h>

#include "config.h"

namespace touch {

// Where the calibration crosses are drawn.  A calibration stores the raw
// readings taken at these four points, so ui.cpp has to draw them exactly here.
constexpr int16_t kCalX0 = 24;
constexpr int16_t kCalX1 = CFG_TFT_WIDTH - 24;
constexpr int16_t kCalY0 = 24;
constexpr int16_t kCalY1 = CFG_TFT_HEIGHT - 24;

struct Raw {
  uint16_t x = 0;
  uint16_t y = 0;
  uint16_t z1 = 0;
  uint16_t z2 = 0;
  int16_t pressure = 0;      // Z1 - Z2, near zero while nothing is pressed
};

// Configures the four pins.  Call it again after anything else has driven them:
// the internal-DAC build in particular, because GPIO 25 doubles as DAC 1.
void begin();

// Samples the panel once, well under a millisecond.  Call it once per UI frame.
void update();

// The last sample, for the diagnostics screen and the boot log.
const Raw& last();

// True while a finger is on the panel.  Needs no calibration, and that is how
// an uncalibrated panel gets calibrated at all.
bool pressed();

// Calibrated screen position of the last sample.  False when nothing is pressed
// or when no usable calibration has been stored yet.
bool read(int16_t& x, int16_t& y);

// Stores a calibration: {which raw channel feeds the screen X axis (0 = raw x,
// 1 = raw y), raw reading at kCalX0, raw reading at kCalX1, then the same
// channel/low/high triple for the screen Y axis}.  Refuses a calibration whose
// spans are too small to divide by.
bool setCalibration(const int16_t cal[6]);
const int16_t* calibration();
bool calibrated();
void clearCalibration();

}  // namespace touch
