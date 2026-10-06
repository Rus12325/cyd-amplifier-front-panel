#include "spectrum.h"

#include <string.h>

namespace dsp {

namespace {

inline float clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// Amplitude (0..1 = full scale) to a 0..1 meter value on a dB scale.
inline float toUnit(float amp, float floorDb) {
  const float db = 20.0f * log10f(amp + 1e-9f);
  return clampf((db - floorDb) / (0.0f - floorDb), 0.0f, 1.0f);
}

}  // namespace

// ------------------------------------------------------------------------ FFT
Fft::Fft() {
  for (int i = 0; i < kFftN / 2; ++i) {
    const double a = -2.0 * kPi * i / kFftN;
    cos_[i] = (float)cos(a);
    sin_[i] = (float)sin(a);
  }
  int bits = 0;
  while ((1 << bits) < kFftN) ++bits;
  for (int i = 0; i < kFftN; ++i) {
    uint16_t r = 0;
    for (int b = 0; b < bits; ++b)
      if (i & (1 << b)) r = (uint16_t)(r | (1 << (bits - 1 - b)));
    rev_[i] = r;
  }
}

void Fft::forward(float* re, float* im) const {
  for (int i = 0; i < kFftN; ++i) {
    const int j = rev_[i];
    if (j > i) {
      const float tr = re[i];
      re[i] = re[j];
      re[j] = tr;
      const float ti = im[i];
      im[i] = im[j];
      im[j] = ti;
    }
  }
  for (int size = 2; size <= kFftN; size <<= 1) {
    const int half = size >> 1;
    const int step = kFftN / size;
    for (int base = 0; base < kFftN; base += size) {
      for (int j = 0; j < half; ++j) {
        const int k = j * step;
        const float wr = cos_[k];
        const float wi = sin_[k];
        const float xr = re[base + j + half];
        const float xi = im[base + j + half];
        const float tr = xr * wr - xi * wi;
        const float ti = xr * wi + xi * wr;
        re[base + j + half] = re[base + j] - tr;
        im[base + j + half] = im[base + j] - ti;
        re[base + j] += tr;
        im[base + j] += ti;
      }
    }
  }
}

// ------------------------------------------------------------------ Analyzer
void Analyzer::begin(float sampleRateHz, float lowHz, float highHz,
                     float floorDb) {
  reset();
  rate_ = sampleRateHz;
  floorDb_ = floorDb;

  const float nyquist = rate_ * 0.45f;
  if (highHz > nyquist) highHz = nyquist;
  const float binHz = rate_ / kFftN;
  if (lowHz < binHz) lowHz = binHz;         // stay above the DC bin
  if (highHz <= lowHz * 2.0f) highHz = lowHz * 4.0f;

  const float step = logf(highHz / lowHz) / kBands;
  for (int i = 0; i <= kBands; ++i) edges_[i] = lowHz * expf(step * i);
  octaves_ = logf(highHz / lowHz) / logf(2.0f) / kBands;

  // Hann window: only half of it is stored, the transform mirrors it.
  for (int i = 0; i < kFftN / 2; ++i)
    window_[i] = 0.5f - 0.5f * cosf((float)(2.0 * kPi * i / (kFftN - 1)));

  frameInterval_ = kFftN / rate_;
}

void Analyzer::reset() {
  for (int i = 0; i < kBands; ++i) {
    band_[i] = 0.0f;
    peak_[i] = 0.0f;
    peakAge_[i] = 0.0f;
  }
  memset(buffer_, 0, sizeof(buffer_));
  memset(scope_, 0, sizeof(scope_));
  fill_ = 0;
  frames_ = 0;
  level_ = 0.0f;
  hold_ = 0.0f;
  hpX1_ = hpY1_ = 0.0f;
  dcAvg_ = 0.5f;
  clipping_ = false;
  pushCount_ = 0;
  pushWindowSecs_ = 0.0f;
  stats_ = Stats();
}

bool Analyzer::push(float sample) {
  // Slow average before the high pass: what the diagnostics screen shows as
  // the resting level of the tap.
  dcAvg_ += 0.0005f * (sample - dcAvg_);
  if (fabsf(sample) > 0.98f) clipping_ = true;

  // Single-pole DC blocker: removes the 1.65 V bias of the ADC tap and any
  // offset of the decoded PCM without eating audible bass at these rates.
  const float y = sample - hpX1_ + 0.995f * hpY1_;
  hpX1_ = sample;
  hpY1_ = y;

  buffer_[fill_++] = y;
  ++pushCount_;
  if (fill_ >= kFftN) {
    fill_ = 0;
    return true;
  }
  return false;
}

void Analyzer::analyse(float dtSeconds) {
  if (!(dtSeconds > 0.0001f)) dtSeconds = frameInterval_;

  memcpy(scope_, buffer_, sizeof(scope_));

  for (int i = 0; i < kFftN; ++i) {
    const float w = (i < kFftN / 2) ? window_[i] : window_[kFftN - 1 - i];
    re_[i] = buffer_[i] * w;
    im_[i] = 0.0f;
  }
  fft_.forward(re_, im_);

  // Scale so that a full-scale sine reads 1.0: Hann coherent gain is 0.5, the
  // two halves of the spectrum each carry A*N/4.
  const float norm = 4.0f / kFftN;
  for (int i = 0; i < kFftN / 2; ++i)
    amp_[i] = sqrtf(re_[i] * re_[i] + im_[i] * im_[i]) * norm;

  const float attack = 1.0f - expf(-dtSeconds / DSP_TAU_ATTACK);
  const float release = 1.0f - expf(-dtSeconds / DSP_TAU_RELEASE);

  for (int b = 0; b < kBands; ++b) {
    int lo = (int)(edges_[b] / rate_ * kFftN + 0.5f);
    int hi = (int)(edges_[b + 1] / rate_ * kFftN + 0.5f);
    if (lo < 1) lo = 1;
    if (hi <= lo) hi = lo + 1;
    if (hi > kFftN / 2) hi = kFftN / 2;

    float m = 0.0f;
    for (int i = lo; i < hi; ++i)
      if (amp_[i] > m) m = amp_[i];

    const float target = toUnit(m, floorDb_);
    band_[b] += (target > band_[b] ? attack : release) * (target - band_[b]);

    if (band_[b] >= peak_[b]) {
      peak_[b] = band_[b];
      peakAge_[b] = 0.0f;
    } else {
      peakAge_[b] += dtSeconds;
      if (peakAge_[b] > DSP_PEAK_HOLD)
        peak_[b] = fmaxf(band_[b], peak_[b] - DSP_PEAK_FALL * dtSeconds);
    }
  }

  float sum = 0.0f;
  for (int i = 0; i < kFftN; ++i) sum += buffer_[i] * buffer_[i];
  // x sqrt(2): a full-scale sine then reads 1.0
  const float rms = sqrtf(sum / kFftN) * 1.41421356f;
  const float target = toUnit(rms, floorDb_);
  // The overall level is driven by the raw RMS, not the ballistics of a band,
  // so it reacts faster than the bars.
  level_ += (target > level_ ? 1.0f - expf(-dtSeconds / 0.020f)
                             : 1.0f - expf(-dtSeconds / 0.200f)) *
            (target - level_);
  if (level_ >= hold_) {
    hold_ = level_;
  } else {
    hold_ -= DSP_PEAK_FALL * dtSeconds;
    if (hold_ < level_) hold_ = level_;
  }

  ++frames_;
  frameInterval_ += 0.05f * (dtSeconds - frameInterval_);
  stats_.frameHz = 1.0f / frameInterval_;
  stats_.frames = frames_;
  stats_.dcLevel = dcAvg_;
  stats_.clipping = clipping_;
  clipping_ = false;
}

float Analyzer::bandHz(int i) const {
  if (i < 0 || i >= kBands) return 0.0f;
  return edges_[i];
}

}  // namespace dsp
