// spectrum.h - spectrum analyser: radix-2 FFT, log-spaced bands, VU level.
//
// Deliberately free of Arduino: it only needs <math.h>, so the same code can be
// exercised on a host.  The band maths here and tools/check_spectrum.py are
// kept in step by hand - run the script after touching the defaults.
#pragma once

#include <math.h>
#include <stdint.h>

#ifndef DSP_FFT_N
#define DSP_FFT_N 512          // power of two
#endif
#ifndef DSP_BANDS
#define DSP_BANDS 16
#endif
#ifndef DSP_TAU_ATTACK
#define DSP_TAU_ATTACK 0.015f  // seconds: how fast a bar rises
#endif
#ifndef DSP_TAU_RELEASE
#define DSP_TAU_RELEASE 0.250f  // seconds: how slowly it falls back
#endif
#ifndef DSP_PEAK_HOLD
#define DSP_PEAK_HOLD 1.200f    // seconds: peak hold before it drops
#endif
#ifndef DSP_PEAK_FALL
#define DSP_PEAK_FALL 0.35f     // per second, in meter units
#endif

namespace dsp {

constexpr int kFftN = DSP_FFT_N;
constexpr int kBands = DSP_BANDS;
constexpr double kPi = 3.14159265358979323846;

static_assert((kFftN & (kFftN - 1)) == 0, "DSP_FFT_N must be a power of two");
static_assert(kFftN >= 64, "DSP_FFT_N is too small to be useful");

// --------------------------------------------------------------- FFT
class Fft {
 public:
  Fft();

  // In-place forward transform.  re/im hold kFftN samples; afterwards bin i is
  // (re[i], im[i]) and the result is not normalised.
  void forward(float* re, float* im) const;

 private:
  float cos_[kFftN / 2];
  float sin_[kFftN / 2];
  uint16_t rev_[kFftN];
};

// ------------------------------------------------------------ analyser
struct Stats {
  float frameHz = 0.0f;      // measured frame rate
  uint32_t frames = 0;       // frames analysed since reset()
  float dcLevel = 0.0f;      // slow average of the input, 0..1 of full scale
  float minRaw = 4095.0f;    // raw ADC extremes, for the diagnostics screen
  float maxRaw = 0.0f;
  bool clipping = false;     // input pinned at a rail for the last frame
};

class Analyzer {
 public:
  // sampleRateHz    rate of push()ed samples
  // lowHz/highHz    band range; highHz is clamped to 0.45 x sampleRateHz
  // floorDb         level mapped to 0.0
  void begin(float sampleRateHz, float lowHz, float highHz, float floorDb);

  // Feed one sample in -1..1 (or 0..1 raw: the DC blocker removes the offset).
  // Returns true when a complete frame has just been buffered.
  bool push(float sample);

  // Analyse the buffered frame.  dtSeconds is the time since the previous
  // call, used for the meter ballistics.
  void analyse(float dtSeconds);

  float band(int i) const { return band_[i]; }
  float bandPeak(int i) const { return peak_[i]; }
  float bandHz(int i) const;                 // lower edge of band i
  float bandWidthOctaves() const { return octaves_; }
  float level() const { return level_; }     // 0..1, RMS based
  float peak() const { return hold_; }       // 0..1, slow falling peak
  float sample(int i) const { return scope_[i]; }   // last frame, DC removed
  int bandCount() const { return kBands; }
  float sampleRate() const { return rate_; }
  float bandwidthHz() const { return rate_ * 0.5f; }
  const Stats& stats() const { return stats_; }
  void reset();

 private:
  Fft fft_;
  float rate_ = 8000.0f;
  float floorDb_ = -60.0f;
  float edges_[kBands + 1];
  float octaves_ = 1.0f;

  // Transform scratch instead of locals: the analyser runs in its own task and
  // a few kilobytes of stack would be a hazard there.
  float buffer_[kFftN];
  float scope_[kFftN];
  float re_[kFftN];
  float im_[kFftN];
  float amp_[kFftN / 2];
  float window_[kFftN / 2];
  float band_[kBands];
  float peak_[kBands];
  float peakAge_[kBands];
  float level_ = 0.0f;
  float hold_ = 0.0f;

  // DC blocker / high-pass state
  float hpX1_ = 0.0f;
  float hpY1_ = 0.0f;
  float dcAvg_ = 0.5f;
  bool clipping_ = false;
  uint32_t fill_ = 0;
  uint32_t frames_ = 0;
  float frameInterval_ = 0.064f;
  uint32_t pushCount_ = 0;
  float pushWindowSecs_ = 0.0f;
  Stats stats_;

};

}  // namespace dsp
