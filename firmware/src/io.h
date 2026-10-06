// io.h - switched channels and the status LED.
//
// Everything the panel switches goes through a logical channel (see the enum in
// config.h) that is either a direct GPIO or a bit of a PCF8574.  Consumers never
// see pin numbers, so the same firmware drives the two-relay build and the
// expander build.
#pragma once

#include <Arduino.h>

#include "config.h"

class OutputPin {
 public:
  void bindGpio(int8_t gpio, bool activeHigh);
  void bindExpander(uint8_t bit, bool activeHigh);
  bool bound() const { return bound_; }
  void write(bool on);
  bool state() const { return state_; }
  bool activeHigh() const { return activeHigh_; }

  // Drives the channel and reads the line back, for the wiring test.  A direct
  // GPIO is read from the pad, so a pin that some other peripheral owns shows
  // up as a failure; an expander channel is read from the PCF8574 itself, which
  // proves the chip answered and the write landed.
  bool verify(bool on);

 private:
  bool bound_ = false;
  bool expander_ = false;
  bool activeHigh_ = true;
  bool state_ = false;
  int8_t gpio_ = -1;
  uint8_t bit_ = 0;
};

// Binds every channel; all of them come up de-energised.
void ioBegin();

OutputPin& ioChannel(int channel);

// Human readable pin for the diagnostics screen, e.g. "GPIO27" or "PCF8574.2".
const char* ioDescription(int channel);

// True when the expander answered on the I2C bus (or when not in use).
bool ioBusOk();

// The CYD's RGB LED, common anode.  A colour whose pin is driven by one of the
// switched channels is skipped - GPIO 16 is the mute relay in the 5.1 build,
// so the status LED runs without its green.  (Only the direct-pin build has
// that clash; with a PCF8574 the LED keeps all three pins.)
class StatusLed {
 public:
  void begin();
  void set(uint8_t r, uint8_t g, uint8_t b);
  void off() { set(0, 0, 0); }

 private:
  void writeChannel(int i, uint8_t value);
  int8_t pin_[3] = {-1, -1, -1};
  int8_t ledc_[3] = {-1, -1, -1};
};
