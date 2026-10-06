#include "touch.h"

namespace touch {
namespace {

constexpr int8_t PIN_CLK = CFG_PIN_TOUCH_SCLK;
constexpr int8_t PIN_MOSI = CFG_PIN_TOUCH_MOSI;
constexpr int8_t PIN_MISO = CFG_PIN_TOUCH_MISO;
constexpr int8_t PIN_CS = CFG_PIN_TOUCH_CS;

// XPT2046 control bytes: start bit | channel | 12 bit | single ended.  These are
// the sequences TFT_eSPI sends, and the controller expects exactly them.
constexpr uint8_t CMD_X = 0xD0;
constexpr uint8_t CMD_Y = 0x90;
constexpr uint8_t CMD_Z1 = 0xB0;
constexpr uint8_t CMD_Z2 = 0xC0;

// Raw spans smaller than this are not a calibration, they are noise.
constexpr int16_t kMinSpan = 300;
// Conversions repeated per channel before the answer is trusted.
constexpr int kSettle = 3;
// How far a raw reading has to leave the idle baseline before it counts as a
// touch.  The pressure reading is the primary signal; this is the safety net,
// because a panel that looks dead is far worse than one stray touch.
constexpr uint16_t kMoveFromIdle = 400;
// Seconds of quiet after which the idle baseline is relearned.
constexpr uint32_t kLearnEveryMs = 2000;

Raw gRaw;
bool gDown = false;
int16_t gX = -1;
int16_t gY = -1;
int16_t gCal[6] = {0, 0, 0, 0, 0, 0};

// What "nothing is being pressed" looks like on this panel, learned at run time.
uint16_t gIdleX = 0;
uint16_t gIdleY = 0;
bool gIdleKnown = false;
uint32_t gQuietSince = 0;
int gMoveRun = 0;

inline int16_t iabs(int16_t v) { return v < 0 ? (int16_t)-v : v; }

// The controller is specified for 2 MHz; a microsecond per half clock stays
// below that and still costs only ~30 us per byte.
inline void tick() { delayMicroseconds(1); }

// SPI mode 0, MSB first: the controller latches MOSI on the rising edge and
// shifts MISO out on the falling one.
uint8_t transfer(uint8_t out) {
  uint8_t in = 0;
  for (int bit = 7; bit >= 0; --bit) {
    digitalWrite(PIN_CLK, LOW);
    digitalWrite(PIN_MOSI, (out >> bit) & 1);
    tick();
    digitalWrite(PIN_CLK, HIGH);
    tick();
    in = (uint8_t)((in << 1) | (digitalRead(PIN_MISO) ? 1 : 0));
  }
  digitalWrite(PIN_CLK, LOW);
  return in;
}

// One 12-bit conversion.  The answer follows the control byte, most significant
// bit first, so two dummy bytes carry all twelve of them.
uint16_t convert(uint8_t cmd) {
  digitalWrite(PIN_CS, LOW);
  tick();
  transfer(cmd);
  const uint8_t hi = transfer(0x00);
  const uint8_t lo = transfer(0x00);
  digitalWrite(PIN_CS, HIGH);
  return (uint16_t)((((uint16_t)hi << 8) | lo) >> 3);
}

// The first conversion after the panel was left alone is off, because the
// controller powers its reference down between reads: convert a few times and
// keep the last answer.
uint16_t convertSettled(uint8_t cmd) {
  uint16_t value = 0;
  for (int i = 0; i < kSettle; ++i) value = convert(cmd);
  return value;
}

void sample() {
  gRaw.x = convertSettled(CMD_X);
  gRaw.y = convertSettled(CMD_Y);
  gRaw.z1 = convertSettled(CMD_Z1);
  gRaw.z2 = convertSettled(CMD_Z2);

  // Unpressed the two readings sit at the rails and track each other; a
  // finger pulls them apart.  The canonical XPT2046 pressure measure is
  // z1 + 4095 - z2: it reads about 4 with nothing touched and climbs as soon
  // as contact is made.  A bare z1 - z2 can never fire on this panel (idle is
  // 0 against 4091, so the difference is always negative) - touches only
  // registered through the safety net below.
  const int16_t z1 = (int16_t)(gRaw.z1 > 4095 ? 4095 : gRaw.z1);
  const int16_t z2 = (int16_t)(gRaw.z2 > 4095 ? 4095 : gRaw.z2);
  const int16_t delta = (int16_t)(z1 + 4095 - z2);
  gRaw.pressure = delta > 0 ? delta : 0;
  const bool pressure = gRaw.pressure >= CFG_TOUCH_PRESS_MIN;

  // Safety net: readings that moved away from the idle baseline also count.
  // Two samples in a row, so one stray conversion cannot fake a finger.
  bool moved = false;
  if (gIdleKnown) {
    moved = (uint16_t)iabs((int16_t)(gRaw.x - gIdleX)) >= kMoveFromIdle ||
            (uint16_t)iabs((int16_t)(gRaw.y - gIdleY)) >= kMoveFromIdle;
  }
  gMoveRun = moved ? (gMoveRun < 2 ? gMoveRun + 1 : 2) : 0;

  gDown = pressure || gMoveRun >= 2;

  const uint32_t now = millis();
  if (gDown) {
    gQuietSince = now;
  } else if (!gIdleKnown || now - gQuietSince > kLearnEveryMs) {
    gIdleX = gRaw.x;
    gIdleY = gRaw.y;
    gIdleKnown = true;
    gQuietSince = now;
  }

  if (!gDown) {
    gX = -1;
    gY = -1;
  }
}

int32_t mapAxis(int16_t raw, int16_t rawLow, int16_t rawHigh, int16_t screenLow,
                int16_t screenHigh) {
  const int32_t span = (int32_t)rawHigh - (int32_t)rawLow;
  if (span == 0) return screenLow;
  return (int32_t)screenLow +
         ((int32_t)raw - (int32_t)rawLow) * (screenHigh - screenLow) / span;
}

bool usable(const int16_t cal[6]) {
  if (cal[0] != 0 && cal[0] != 1) return false;
  if (cal[3] != 0 && cal[3] != 1) return false;
  if (cal[0] == cal[3]) return false;              // both axes off one channel
  return iabs((int16_t)(cal[2] - cal[1])) >= kMinSpan &&
         iabs((int16_t)(cal[5] - cal[4])) >= kMinSpan;
}

}  // namespace

void begin() {
  // The idle baseline is relearned from the first samples; anything that stole
  // the pins in the meantime may also have moved the readings.
  gIdleKnown = false;
  gMoveRun = 0;
  gDown = false;

  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);
  pinMode(PIN_CLK, OUTPUT);
  digitalWrite(PIN_CLK, LOW);
  pinMode(PIN_MOSI, OUTPUT);
  digitalWrite(PIN_MOSI, LOW);
  pinMode(PIN_MISO, INPUT);
}

void update() {
  sample();
  if (!gDown || !calibrated()) return;

  const uint16_t raw[2] = {gRaw.x, gRaw.y};
  const int32_t sx = mapAxis((int16_t)raw[gCal[0]], gCal[1], gCal[2], kCalX0,
                             kCalX1);
  const int32_t sy = mapAxis((int16_t)raw[gCal[3]], gCal[4], gCal[5], kCalY0,
                             kCalY1);
  gX = (int16_t)constrain(sx, 0L, (long)CFG_TFT_WIDTH - 1);
  gY = (int16_t)constrain(sy, 0L, (long)CFG_TFT_HEIGHT - 1);
}

const Raw& last() { return gRaw; }

bool pressed() { return gDown; }

bool read(int16_t& x, int16_t& y) {
  if (!gDown || gX < 0 || gY < 0) return false;
  x = gX;
  y = gY;
  return true;
}

bool setCalibration(const int16_t cal[6]) {
  if (!usable(cal)) return false;
  for (int i = 0; i < 6; ++i) gCal[i] = cal[i];
  return true;
}

const int16_t* calibration() { return gCal; }

bool calibrated() { return usable(gCal); }

void clearCalibration() {
  for (int i = 0; i < 6; ++i) gCal[i] = 0;
}

}  // namespace touch
