#include "amp.h"

// --------------------------------------------------------------- mute gate
void MuteGate::begin(OutputPin* pin) {
  pin_ = pin;
  muted_ = true;
  timed_ = false;
  if (fitted()) pin_->write(true);      // come up muted
}

void MuteGate::mute() {
  muted_ = true;
  timed_ = false;
  if (fitted()) pin_->write(true);
}

void MuteGate::unmute() {
  muted_ = false;
  timed_ = false;
  if (fitted()) pin_->write(false);
}

void MuteGate::muteFor(uint32_t ms) {
  muted_ = true;
  if (fitted()) pin_->write(true);
  const uint32_t at = millis() + ms;
  // Never shorten a window that is already running.
  if (!timed_ || (int32_t)(at - releaseAt_) > 0) {
    timed_ = true;
    releaseAt_ = at;
  }
}

void MuteGate::loop(uint32_t now) {
  if (timed_ && (int32_t)(now - releaseAt_) >= 0) unmute();
}

// --------------------------------------------------------------- amplifier
void AmpController::begin(OutputPin* power, MuteGate* mute) {
  power_ = power;
  mute_ = mute;
  state_ = AmpState::Off;
  desired_ = false;
  stage_ = 0;
  if (power_ && power_->bound()) power_->write(false);
}

void AmpController::requestOn() {
  if (desired_) return;
  desired_ = true;
  if (state_ != AmpState::Off) return;      // still switching off, handled below
  stage_ = 0;
  stamp_ = millis();
  state_ = AmpState::Starting;
  if (mute_) mute_->mute();
}

void AmpController::requestOff() {
  if (!desired_) return;
  desired_ = false;
  if (state_ == AmpState::Off) return;
  if (state_ == AmpState::On || state_ == AmpState::Starting) {
    if (mute_) mute_->mute();
    stage_ = 0;
    stamp_ = millis();
    state_ = AmpState::Stopping;
  }
}

void AmpController::toggle() {
  if (desired_) {
    requestOff();
  } else {
    requestOn();
  }
}

void AmpController::loop(uint32_t now) {
  switch (state_) {
    case AmpState::Starting:
      // 1: mute and let any signal relay settle, 2: close the power relay,
      // 3: after the amp has settled, open the mute gate.
      if (stage_ == 0 && now - stamp_ >= CFG_AMP_MUTE_LEAD_MS) {
        if (power_ && power_->bound()) power_->write(true);
        stage_ = 1;
        stamp_ = now;
      } else if (stage_ == 1 && now - stamp_ >= CFG_AMP_WARMUP_MS) {
        if (!desired_) {          // changed our mind while warming up
          state_ = AmpState::Stopping;
          stage_ = 0;
          stamp_ = now;
          break;
        }
        if (mute_) mute_->unmute();
        state_ = AmpState::On;
        stage_ = 2;
      }
      break;

    case AmpState::Stopping:
      if (now - stamp_ >= CFG_AMP_MUTE_TAIL_MS) {
        if (power_ && power_->bound()) power_->write(false);
        stage_ = 0;
        if (desired_) {           // toggled again while shutting down
          stamp_ = now;
          state_ = AmpState::Starting;
          if (mute_) mute_->mute();
        } else {
          state_ = AmpState::Off;
        }
      }
      break;

    case AmpState::On:
      if (!desired_) requestOff();
      break;

    case AmpState::Off:
      if (desired_) requestOn();
      break;
  }
}

const char* AmpController::stateText() const {
  switch (state_) {
    case AmpState::Off: return "OFF";
    case AmpState::Starting: return "START";
    case AmpState::On: return "ON";
    case AmpState::Stopping: return "STOP";
  }
  return "?";
}

// ---------------------------------------------------------------- sources
void SourceController::begin(MuteGate* mute) {
  mute_ = mute;
  current_ = CFG_DEFAULT_SOURCE;
  target_ = current_;
  pending_ = false;
  apply(current_);
}

void SourceController::select(int index) {
  if (index < 0 || index >= CFG_SOURCE_COUNT) return;
  if (index == target_) return;
  target_ = index;
  if (mute_) mute_->muteFor(CFG_SRC_MUTE_LEAD_MS + CFG_SRC_SWITCH_MS);
  switchAt_ = millis() + CFG_SRC_MUTE_LEAD_MS;
  pending_ = true;
}

void SourceController::loop(uint32_t now) {
  if (pending_ && (int32_t)(now - switchAt_) >= 0) {
    apply(target_);
    pending_ = false;
  }
}

void SourceController::apply(int index) {
  const int8_t selected = CFG_SOURCES[index].channel;
  // Deselect everything that is not the chosen channel, then drive the chosen
  // one.  Channels shared between sources (one relay carrying two inputs) are
  // left to the selected source alone, so the two levels cannot fight.
  for (int i = 0; i < CFG_SOURCE_COUNT; ++i) {
    const SourceDef& d = CFG_SOURCES[i];
    if (d.channel == selected) continue;
    OutputPin& p = ioChannel(d.channel);
    if (p.bound()) p.write(!d.level);
  }
  OutputPin& p = ioChannel(selected);
  if (p.bound()) p.write(CFG_SOURCES[index].level);
  current_ = index;
}

const char* SourceController::name(int index) const {
  if (index < 0 || index >= CFG_SOURCE_COUNT) return "?";
  return CFG_SOURCES[index].label;
}
