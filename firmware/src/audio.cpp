#include "audio.h"

#include "config.h"

namespace audio {
namespace {

dsp::Analyzer gAnalyzer;
portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;
volatile float gGain = 1.0f;

// Frame timing, in microseconds.
uint32_t gLastFrameUs = 0;

// Raw extremes of the ADC tap, they never go through the filter chain.
float gMinRaw = 4095.0f;
float gMaxRaw = 0.0f;

void snapshotFromAnalyzer(Snapshot& out) {
  out.sampleRate = gAnalyzer.sampleRate();
  out.lowHz = gAnalyzer.bandHz(0);
  out.highHz = gAnalyzer.bandHz(gAnalyzer.bandCount() - 1);
  for (int i = 0; i < dsp::kBands; ++i) {
    out.band[i] = gAnalyzer.band(i);
    out.peak[i] = gAnalyzer.bandPeak(i);
  }
  // Decimate the scope trace: the display has 128 columns for 512 samples.
  const int step = dsp::kFftN / kScopePoints;
  for (int i = 0; i < kScopePoints; ++i)
    out.scope[i] = gAnalyzer.sample(i * step);
  out.level = gAnalyzer.level();
  out.hold = gAnalyzer.peak();
  out.stats = gAnalyzer.stats();
  out.stats.minRaw = gMinRaw;
  out.stats.maxRaw = gMaxRaw;
  out.valid = true;
}

// Feeds one sample and, if that completed a frame, analyses it.  Caller holds
// the spinlock.
void pushLocked(float sample) {
  if (gAnalyzer.push(sample)) {
    const uint32_t now = micros();
    const float dt = (now - gLastFrameUs) * 1e-6f;
    gLastFrameUs = now;
    gAnalyzer.analyse(dt);
  }
}

#if !CYD_BT_SINK && !CFG_ENC_SHADOWS_TAP
// Paced sampler.  analogRead() costs well under a tenth of the 125 us period at
// 8 kHz, and pacing off micros() keeps the sample rate honest even though the
// loop wakes up late now and then.
void samplerTask(void*) {
  const uint32_t periodUs = 1000000UL / CFG_SAMPLE_RATE;
  uint32_t next = micros();
  gLastFrameUs = micros();

  for (;;) {
    const int raw = analogRead(CFG_PIN_AUDIO_ADC);
    if ((float)raw < gMinRaw) gMinRaw = (float)raw;
    if ((float)raw > gMaxRaw) gMaxRaw = (float)raw;

    portENTER_CRITICAL(&gMux);
    pushLocked((float)raw * (1.0f / 4095.0f));
    portEXIT_CRITICAL(&gMux);

    next += periodUs;
    const int32_t wait = (int32_t)(next - micros());
    if (wait > 0) {
      delayMicroseconds((uint32_t)wait);
    } else {
      next = micros();               // fell behind, resynchronise
    }
  }
}
#endif

}  // namespace

void begin() {
  gAnalyzer.begin(CFG_SAMPLE_RATE, CFG_BAND_LOW_HZ, CFG_BAND_HIGH_HZ,
                  CFG_METER_FLOOR_DB);
  gLastFrameUs = micros();

#if !CYD_BT_SINK && !CFG_ENC_SHADOWS_TAP
  // No sampler when the encoder owns the tap pin (CFG_ENC_SHADOWS_TAP): the
  // panel in that build is a remote control, and the ADC would only fight the
  // encoder's edges for the same pin.
  analogSetPinAttenuation(CFG_PIN_AUDIO_ADC, CFG_ADC_ATTEN);
  pinMode(CFG_PIN_AUDIO_ADC, INPUT);
  xTaskCreatePinnedToCore(samplerTask, "audiotap", 3072, nullptr,
                          CFG_ANALYZER_TASK_PRIO, nullptr,
                          CFG_ANALYZER_TASK_CORE);
#endif
}

void push(float sample) {
  const float g = gGain;
  portENTER_CRITICAL(&gMux);
  pushLocked(sample * g);
  portEXIT_CRITICAL(&gMux);
}

void setSampleRate(float hz) {
  if (hz < 1000.0f) return;
  portENTER_CRITICAL(&gMux);
  gAnalyzer.begin(hz, CFG_BAND_LOW_HZ, CFG_BAND_HIGH_HZ, CFG_METER_FLOOR_DB);
  portEXIT_CRITICAL(&gMux);
}

void setGain(float gain) { gGain = constrain(gain, 0.1f, 32.0f); }
float gain() { return gGain; }

void read(Snapshot& out) {
  portENTER_CRITICAL(&gMux);
  snapshotFromAnalyzer(out);
  portEXIT_CRITICAL(&gMux);
}

}  // namespace audio
