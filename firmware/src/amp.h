// amp.h - power sequencing, the mute gate and input selection.
//
// An amplifier that is switched on or off with a relay clicks and thumps if the
// signal is live while the relay moves, so both the power switch and the input
// switch run through the same mute gate:
//     mute -> move the relay -> wait for the amp to settle -> unmute
// With no mute relay fitted the state machine still runs, it just cannot make
// the transients any quieter.
#pragma once

#include <Arduino.h>

#include "config.h"
#include "io.h"

// --------------------------------------------------------------- mute gate
class MuteGate {
 public:
  void begin(OutputPin* pin);
  void mute();                    // hard mute, cancels a pending release
  void unmute();
  void muteFor(uint32_t ms);      // engage now, release on its own
  bool muted() const { return muted_; }
  bool fitted() const { return pin_ != nullptr && pin_->bound(); }
  void loop(uint32_t now);

 private:
  OutputPin* pin_ = nullptr;
  bool muted_ = true;
  bool timed_ = false;
  uint32_t releaseAt_ = 0;
};

// -------------------------------------------------------------- amplifier
enum class AmpState : uint8_t { Off, Starting, On, Stopping };

class AmpController {
 public:
  void begin(OutputPin* power, MuteGate* mute);
  void requestOn();
  void requestOff();
  void toggle();
  bool desiredOn() const { return desired_; }
  void loop(uint32_t now);
  AmpState state() const { return state_; }
  bool isOn() const { return state_ == AmpState::On; }
  bool isBusy() const {
    return state_ == AmpState::Starting || state_ == AmpState::Stopping;
  }
  const char* stateText() const;
  OutputPin* powerPin() { return power_; }

 private:
  OutputPin* power_ = nullptr;
  MuteGate* mute_ = nullptr;
  AmpState state_ = AmpState::Off;
  bool desired_ = false;
  uint8_t stage_ = 0;
  uint32_t stamp_ = 0;
};

// ---------------------------------------------------------------- sources
class SourceController {
 public:
  void begin(MuteGate* mute);
  void select(int index);         // schedules the change, mute first
  void loop(uint32_t now);
  int current() const { return current_; }
  int target() const { return target_; }
  bool switching() const { return pending_; }
  const char* name(int index) const;
  int count() const { return CFG_SOURCE_COUNT; }

 private:
  void apply(int index);
  MuteGate* mute_ = nullptr;
  int current_ = CFG_DEFAULT_SOURCE;
  int target_ = CFG_DEFAULT_SOURCE;
  bool pending_ = false;
  uint32_t switchAt_ = 0;
};
