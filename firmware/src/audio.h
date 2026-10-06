// audio.h - the audio tap that feeds the visualiser.
//
// Two sources, one analyser:
//   * CYD_BT_SINK=0 - a task samples GPIO35 (ADC1 channel 7) at CFG_SAMPLE_RATE
//   * CYD_BT_SINK=1 - the A2DP stream reader pushes decoded PCM in
// The UI never touches the analyser directly, it reads a snapshot, so a frame
// that is being rewritten cannot tear the display.
#pragma once

#include <Arduino.h>

#include "config.h"
#include "spectrum.h"

namespace audio {

// Decimated copy of the last frame, for the oscilloscope mode.
constexpr int kScopePoints = 128;

struct Snapshot {
  float band[dsp::kBands];
  float peak[dsp::kBands];
  float level;
  float hold;
  float scope[kScopePoints];
  float sampleRate;
  float lowHz;
  float highHz;
  dsp::Stats stats;
  bool valid;
};

// Starts the ADC sampler (no-op in the A2DP build, where bt::begin() feeds it).
void begin();

// Called by the sampler task and by the A2DP stream reader.  Accepts either
// 0..1 (raw ADC) or -1..1 samples; the DC blocker in the analyser copes with the
// offset either way.
void push(float sample);

// Tells the analyser that the stream rate changed (A2DP negotiates it).
void setSampleRate(float hz);

void setGain(float gain);
float gain();

void read(Snapshot& out);

}  // namespace audio
