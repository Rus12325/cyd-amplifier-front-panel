// encoder.h - the optional rotary encoder.
//
// The board has no free pin pair, so a real encoder means giving two pins up
// (GPIO 22, the spare key, and GPIO 35, the ADC tap) - see config.h.  It is off
// unless CFG_ENC_ENABLED=1, and everything here compiles to stubs in that case,
// so the rest of the firmware does not have to care.
//
// It reports the same thing the touch strip does: relative steps, which main()
// hands to hid::volumeUp()/volumeDown().  The knob therefore only makes sense
// in a build with the PC keys (CYD_BLE_HID=1).
#pragma once

#include <Arduino.h>

#include "config.h"

namespace encoder {

void begin();
void loop(uint32_t now);

bool fitted();

// Detents turned since the last call: positive = clockwise (unless
// CFG_ENC_REVERSE), zero when the knob did not move.  Interrupt-safe.
int takeSteps();

// True once per press of the encoder's own switch (always false when no switch
// is wired - the default).
bool takePush();

}  // namespace encoder
