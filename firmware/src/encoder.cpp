#include "encoder.h"

namespace encoder {

#if CFG_ENC_ENABLED
namespace {

// Quadrature edges per mechanical detent: an EC11-style encoder makes four
// transitions per click, so four net edges are one step.
constexpr int8_t kEdgesPerStep = 4;
constexpr uint32_t kBounceMs = 30;

// index = (previous state << 2) | new state, state = (A << 1) | B.
// The table lives in DRAM so the interrupt handler never fetches from flash:
// during an OTA update the flash cache is off, and an ISR that reads flash
// would crash the panel mid-flash.
DRAM_ATTR const int8_t kQuad[16] = {
    0, -1,  1,  0,
    1,  0,  0, -1,
   -1,  0,  0,  1,
    0,  1, -1,  0,
};

volatile int8_t gState = 0;
volatile int8_t gAccum = 0;
volatile int16_t gSteps = 0;

#if CFG_PIN_ENC_SW >= 0
bool gSwDown = false;
bool gPush = false;
uint32_t gSwAt = 0;
#endif

void IRAM_ATTR isr() {
  const int8_t now =
      (int8_t)((digitalRead(CFG_PIN_ENC_A) << 1) | digitalRead(CFG_PIN_ENC_B));
  int8_t accum = (int8_t)(gAccum + kQuad[(gState << 2) | now]);
  gState = now;
  if (accum >= kEdgesPerStep) {
    accum = (int8_t)(accum - kEdgesPerStep);
    if (gSteps < 1000) ++gSteps;
  } else if (accum <= -kEdgesPerStep) {
    accum = (int8_t)(accum + kEdgesPerStep);
    if (gSteps > -1000) --gSteps;
  }
  gAccum = accum;
}

}  // namespace

void begin() {
  pinMode(CFG_PIN_ENC_A, INPUT_PULLUP);
  pinMode(CFG_PIN_ENC_B, INPUT_PULLUP);   // GPIO 35 has no pull-up - add 10k
  gState = (int8_t)((digitalRead(CFG_PIN_ENC_A) << 1) |
                    digitalRead(CFG_PIN_ENC_B));
  gAccum = 0;
  gSteps = 0;
  attachInterrupt(digitalPinToInterrupt(CFG_PIN_ENC_A), isr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(CFG_PIN_ENC_B), isr, CHANGE);

#if CFG_PIN_ENC_SW >= 0
  pinMode(CFG_PIN_ENC_SW, INPUT_PULLUP);
  gSwDown = (digitalRead(CFG_PIN_ENC_SW) == LOW);
#endif

  Serial.printf("encoder      : A%d B%d, %d edges/step, %s, switch %s\n",
                CFG_PIN_ENC_A, CFG_PIN_ENC_B, kEdgesPerStep,
                CFG_ENC_REVERSE ? "reversed" : "normal",
#if CFG_PIN_ENC_SW >= 0
                "wired");
#else
                "not wired");
#endif
}

void loop(uint32_t now) {
#if CFG_PIN_ENC_SW >= 0
  const bool down = digitalRead(CFG_PIN_ENC_SW) == LOW;
  if (down != gSwDown && now - gSwAt >= kBounceMs) {
    gSwAt = now;
    gSwDown = down;
    if (down) gPush = true;    // act on the press, not on the release
  }
#else
  (void)now;
#endif
}

bool fitted() { return true; }

int takeSteps() {
  noInterrupts();
  const int16_t steps = gSteps;
  gSteps = 0;
  interrupts();
#if CFG_ENC_REVERSE
  return -(int)steps;
#else
  return (int)steps;
#endif
}

bool takePush() {
#if CFG_PIN_ENC_SW >= 0
  const bool pressed = gPush;
  gPush = false;
  return pressed;
#else
  return false;
#endif
}

#else  // !CFG_ENC_ENABLED - the panel runs on touch alone

void begin() {
  Serial.println("encoder      : not fitted - touch only (env cyd_enc adds one)");
}

void loop(uint32_t now) { (void)now; }
bool fitted() { return false; }
int takeSteps() { return 0; }
bool takePush() { return false; }

#endif  // CFG_ENC_ENABLED

}  // namespace encoder
