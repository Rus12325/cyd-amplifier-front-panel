#include "wire_test.h"

namespace wiretest {
namespace {

constexpr uint32_t kOnMs = 400;      // long enough to see and hear the relay
constexpr uint32_t kSettleMs = 25;   // the line needs a moment after a write
constexpr uint32_t kOffMs = 120;

Result sResult[CH_COUNT];
int sStep = -1;
uint8_t sPhase = 0;
uint32_t sAt = 0;
bool sRunning = false;
bool sFinished = false;
bool sOnOk = false;
int sTested = 0;
int sFailures = 0;

int firstBound(int from) {
  for (int i = from; i < CH_COUNT; ++i)
    if (ioChannel(i).bound()) return i;
  return -1;
}

void finishChannel(Result r) {
  sResult[sStep] = r;
  ++sTested;
  if (r != Result::Ok) ++sFailures;
  sStep = firstBound(sStep + 1);
  sPhase = 0;
}

void releaseAll() {
  for (int i = 0; i < CH_COUNT; ++i) {
    OutputPin& p = ioChannel(i);
    if (p.bound()) p.write(false);
  }
}

}  // namespace

void begin() {
  releaseAll();
  for (int i = 0; i < CH_COUNT; ++i)
    sResult[i] = ioChannel(i).bound() ? Result::Pending : Result::Skipped;
  sStep = firstBound(0);
  sPhase = 0;
  sAt = millis();
  sRunning = sStep >= 0;
  sFinished = !sRunning;
  sTested = 0;
  sFailures = 0;
  sOnOk = false;
}

void cancel() {
  releaseAll();
  sRunning = false;
  sFinished = true;
  sStep = -1;
}

void loop(uint32_t now) {
  if (!sRunning || sStep < 0) return;
  if ((int32_t)(now - sAt) < 0) return;

  OutputPin& pin = ioChannel(sStep);
  switch (sPhase) {
    case 0:                       // energise and check that the line follows
      sOnOk = pin.verify(true);
      sPhase = 1;
      sAt = now + kOnMs;
      break;
    case 1:                       // release and check that it goes back
      pin.verify(false);
      sPhase = 2;
      sAt = now + kSettleMs;
      break;
    case 2: {
      const bool offOk = pin.state() == false;
      finishChannel(sOnOk && offOk ? Result::Ok : Result::NoAnswer);
      if (sStep < 0) {
        sRunning = false;
        sFinished = true;
      } else {
        sAt = now + kOffMs;
      }
      break;
    }
    default:
      sPhase = 0;
      break;
  }
}

bool running() { return sRunning; }
bool finished() { return sFinished; }
int step() { return sRunning ? sStep : -1; }
Result result(int channel) {
  if (channel < 0 || channel >= CH_COUNT) return Result::Skipped;
  return sResult[channel];
}
int tested() { return sTested; }
int failures() { return sFailures; }

const char* resultText(int channel) {
  switch (result(channel)) {
    case Result::Pending: return "...";
    case Result::Ok: return "OK";
    case Result::NoAnswer: return "FAIL";
    case Result::Skipped: return "";
  }
  return "";
}

}  // namespace wiretest
