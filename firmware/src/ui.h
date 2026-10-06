// ui.h - the panel: status bar, source buttons, power button, visualiser,
// settings overlay and a diagnostics screen.
//
// The UI never talks to the hardware itself; it renders a status snapshot and
// returns the user's intent as an event, which main() then applies.
#pragma once

#include <Arduino.h>

#include "audio.h"
#include "config.h"
#include "settings.h"

enum class UiAction : uint8_t {
  None,
  PowerToggle,
  PowerOff,         // long press: force off
  SelectSource,
  RunWiringTest,    // from the diagnostics screen, main() gates on amp state
  ToggleTestTone,   // diagnostics: 440 Hz through the audio output, if there is one
  SettingsChanged,  // brightness / mode / gain changed, main() may persist
  // The PC's volume, over Bluetooth: the panel is a BLE HID keyboard for the
  // PC, and these are the volume keys of a multimedia one.  'arg' is how many
  // presses, and anything above 1 comes from dragging the strip.  No-ops in a
  // build without CYD_BLE_HID (the UI does not draw the volume row there).
  VolumeUp,
  VolumeDown,
  VolumeMute,       // toggles the PC's own mute
  AvrcPlay,
  AvrcPause,
  AvrcNext,
  AvrcPrev,
};

struct UiStatus {
  const char* ampText = "";
  bool ampOn = false;
  bool ampBusy = false;
  int source = 0;
  bool muteFitted = false;
  bool busOk = true;
  bool btPresent = false;
  const char* btText = "";
  bool btConnected = false;
  uint8_t btState = 0;      // opaque to the UI, only used to spot changes
  bool toneAvailable = false;   // this build has an audio output to test
  bool toneOn = false;
  const char* trackTitle = "";  // AVRCP: empty when nothing is known
  const char* trackArtist = "";
};

struct UiEvent {
  UiAction action = UiAction::None;
  int arg = 0;
};

void uiBegin(Settings* settings);
UiEvent uiPoll(const UiStatus& st);
void uiRender(const UiStatus& st, const audio::Snapshot& snap);

void uiSetBrightness(uint8_t value);
uint8_t uiBrightness();
bool uiOverlayOpen();
