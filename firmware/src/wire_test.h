// wire_test.h - walk every channel and prove the line follows the command.
//
// It does not need a meter: each channel is driven high, read back, driven low
// and read back.  A pin owned by the display driver, a channel wired to the
// wrong pin, or an expander that never answered all show up as a failure - and
// the user can hear the relays clicking while it runs.
//
// The amplifier power relay is one of the channels it touches, so the test must
// only be started with the amplifier off.
#pragma once

#include <Arduino.h>

#include "config.h"
#include "io.h"

namespace wiretest {

enum class Result : uint8_t {
  Pending,     // not reached yet
  Ok,
  NoAnswer,    // the line did not follow
  Skipped,     // channel is not wired at all
};

void begin();
void cancel();
void loop(uint32_t now);

bool running();
bool finished();
int step();                      // channel under test, -1 when idle
Result result(int channel);
int tested();
int failures();

const char* resultText(int channel);

}  // namespace wiretest
