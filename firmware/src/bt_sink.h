// bt_sink.h - CYD as the Bluetooth receiver (CYD_BT_SINK=1 only).
//
// The A2DP sink decodes the phone's stream and hands it to an I2S DAC, which
// feeds the amplifier; the same decoded PCM is tapped for the visualiser, so
// the meter works on real audio instead of an analog guess.
#pragma once

#include <Arduino.h>

#include "config.h"

#if CYD_BT_SINK
namespace bt {

enum class State : uint8_t { Idle, Connecting, Connected, Streaming };

void begin();
void end();

// A 440 Hz tone on the same output the music uses, so the amplifier wiring can
// be checked without pairing a phone.  It switches itself off if a stream
// arrives, and it touches the audio only while nothing is playing.
void setTestTone(bool on);
bool testTone();

State state();
bool connected();
bool streaming();
const char* stateText();
const char* peerName();

// AVRCP track metadata (empty strings when nothing is playing).
const char* trackTitle();
const char* trackArtist();

// AVRCP playback controls — no-op if not connected.
void avrcPlay();
void avrcPause();
void avrcNext();
void avrcPrev();

}  // namespace bt
#endif  // CYD_BT_SINK
