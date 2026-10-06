// settings.h - what survives a power cycle, kept in NVS.
#pragma once

#include <Arduino.h>

#include "config.h"

struct SettingsData {
  uint8_t brightness = CFG_BRIGHT_DEFAULT;
  bool autoDim = CFG_AUTODIM_DEFAULT;
  uint8_t visualizer = 0;             // 0 bars, 1 VU, 2 scope, 3 off
  int8_t source = CFG_DEFAULT_SOURCE;
  bool powerOnBoot = false;           // bring the amplifier up at boot
  // Touch calibration: which raw channel of the controller feeds the screen X
  // axis, the raw reading at touch::kCalX0 and at touch::kCalX1, then the same
  // pair for the screen Y axis.  Zeroes or a degenerate span mean "never
  // calibrated", which is what makes the panel run the calibration screen.
  int16_t touchCal[6] = {0, 0, 0, 0, 0, 0};
  float meterGain = 1.0f;             // calibration of the visualiser tap
  uint8_t sleepAfterMin = 0;          // 0 = disabled; auto-off after N silent minutes
  uint32_t sessionCount = 0;          // incremented on every boot
  uint32_t totalRunSeconds = 0;       // accumulated uptime in seconds
};

class Settings {
 public:
  void begin();
  void save();
  SettingsData& data() { return data_; }
  const SettingsData& data() const { return data_; }
  bool loaded() const { return loaded_; }

 private:
  SettingsData data_;
  bool loaded_ = false;
};

extern Settings gSettings;
