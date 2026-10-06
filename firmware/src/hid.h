// hid.h - the panel as a Bluetooth LE keyboard for the PC (volume keys only).
//
// The PC feeds the amplifier through three analog jacks, so the only volume
// there is lives in Windows.  This module makes the panel a BLE HID "consumer
// control" device: the PC pairs with it once, and after that the volume up /
// down / mute usages the touch controls (and the optional encoder) send move
// the PC's own volume slider - the same way a multimedia keyboard does.
//
// The whole module compiles away when CYD_BLE_HID is 0 (the CYD_BT_SINK builds,
// where the A2DP stack owns the Bluetooth controller - see config.h).
#pragma once

#include <Arduino.h>

#include "config.h"

#if CYD_BLE_HID

namespace hid {

// Brings up the BLE stack and advertises as "CFG_HID_NAME".  Call once, after
// the display is up: this is the last thing that needs to work.
void begin();

// Pumps the key presses waiting for the radio.  Call it from loop().
void loop(uint32_t now);

bool ready();                  // the stack started and advertising is on
bool connected();              // something is linked and receiving reports
const char* stateText();       // "LINKED", "PAIRING" or "OFF"

// One step is one multimedia key press, which Windows turns into
// CFG_HID_STEP_PCT percent of volume.  Steps are queued, not lost, when the
// panel is spun faster than the radio can send them.
void volumeUp(uint8_t steps = 1);
void volumeDown(uint8_t steps = 1);
void muteToggle();

// What the panel believes the PC is set to.  Nothing reads the PC's real level
// (HID only sends keys, it does not ask), so this is an estimate: it starts at
// 50 %, moves one step per press that really went out, and anything changed on
// the PC itself is invisible here.  It exists so the screen can show what the
// keys are doing.
int volumeEstimate();          // 0..100
bool muteEstimate();

uint32_t sentSteps();          // steps handed to the radio
uint32_t droppedSteps();       // steps that had nowhere to go (no link, or full)

}  // namespace hid
#endif  // CYD_BLE_HID
