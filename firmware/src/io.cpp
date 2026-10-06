#include "io.h"

#if USE_IO_EXPANDER
#include <Wire.h>
#endif

namespace {

OutputPin gChannels[CH_COUNT];
bool gBusOk = true;

#if USE_IO_EXPANDER
// Shadow of the PCF8574 output register: bit set = pin driven HIGH.
uint8_t gPort = 0xFF;

void portFlush() {
  Wire.beginTransmission(CFG_I2C_ADDRESS);
  Wire.write(gPort);
  gBusOk = (Wire.endTransmission() == 0);
}
#endif

#if !USE_IO_EXPANDER
// True when one of the switched channels already drives this pin.  The CYD
// shares its RGB LED with the relay pins - GPIO 16 is the mute relay in the 5.1
// build - and a PWM'd LED must not fight a relay.
bool pinOwnedByChannel(int8_t gpio) {
  if (gpio < 0) return false;
  for (int i = 0; i < CH_COUNT; ++i)
    if (CFG_DIRECT_PIN[i] == gpio) return true;
  return false;
}
#endif

}  // namespace

// ------------------------------------------------------------------ OutputPin
void OutputPin::bindGpio(int8_t gpio, bool activeHigh) {
  bound_ = (gpio >= 0);
  expander_ = false;
  gpio_ = gpio;
  activeHigh_ = activeHigh;
  if (bound_) {
    pinMode(gpio_, OUTPUT);
    write(false);
  }
}

void OutputPin::bindExpander(uint8_t bit, bool activeHigh) {
  bound_ = true;
  expander_ = true;
  bit_ = bit;
  activeHigh_ = activeHigh;
  state_ = false;
}

void OutputPin::write(bool on) {
  state_ = on;
  if (!bound_) return;
  const uint8_t level = (on == activeHigh_) ? HIGH : LOW;
  if (expander_) {
#if USE_IO_EXPANDER
    const uint8_t mask = (uint8_t)(1u << bit_);
    if (level) {
      gPort |= mask;
    } else {
      gPort &= (uint8_t)~mask;
    }
    portFlush();
#endif
  } else {
    digitalWrite(gpio_, level);
  }
}

bool OutputPin::verify(bool on) {
  if (!bound_) return false;
  write(on);
  delayMicroseconds(300);
  const bool expected = (on == activeHigh_);

  if (expander_) {
#if USE_IO_EXPANDER
    const uint8_t mask = (uint8_t)(1u << bit_);
    if (Wire.requestFrom((uint8_t)CFG_I2C_ADDRESS, (uint8_t)1) != 1) return false;
    const int value = Wire.read();
    if (value < 0) return false;
    return ((value & mask) != 0) == expected;
#else
    return false;
#endif
  }
  return (digitalRead(gpio_) == HIGH) == expected;
}

// ---------------------------------------------------------------------- setup
void ioBegin() {
#if USE_IO_EXPANDER
  Wire.begin(CFG_PIN_I2C_SDA, CFG_PIN_I2C_SCL);
  Wire.beginTransmission(CFG_I2C_ADDRESS);
  gBusOk = (Wire.endTransmission() == 0);
  for (int i = 0; i < CH_COUNT; ++i)
    gChannels[i].bindExpander((uint8_t)i, CFG_ACTIVE_HIGH[i]);
  // Everything off first: a relay board must not chatter while the panel boots.
  for (int i = 0; i < CH_COUNT; ++i) gChannels[i].write(false);
#else
  for (int i = 0; i < CH_COUNT; ++i)
    gChannels[i].bindGpio(CFG_DIRECT_PIN[i], CFG_ACTIVE_HIGH[i]);
#endif
}

OutputPin& ioChannel(int channel) {
  static OutputPin sDummy;
  if (channel < 0 || channel >= CH_COUNT) return sDummy;
  return gChannels[channel];
}

bool ioBusOk() { return gBusOk; }

const char* ioDescription(int channel) {
  static char buf[16];
  if (channel < 0 || channel >= CH_COUNT) return "--";
  OutputPin& p = gChannels[channel];
  if (!p.bound()) return "unused";
#if USE_IO_EXPANDER
  snprintf(buf, sizeof(buf), "PCF8574.%d", channel);
#else
  snprintf(buf, sizeof(buf), "GPIO%d", CFG_DIRECT_PIN[channel]);
#endif
  return buf;
}

// ------------------------------------------------------------------- StatusLed
void StatusLed::begin() {
  pin_[0] = CFG_PIN_LED_R;
  pin_[1] = CFG_PIN_LED_G;
  pin_[2] = CFG_PIN_LED_B;
  for (int i = 0; i < 3; ++i) {
    ledc_[i] = -1;
#if !USE_IO_EXPANDER
    if (pinOwnedByChannel(pin_[i])) pin_[i] = -1;   // a relay owns this pin
#endif
    if (pin_[i] < 0) continue;
    ledc_[i] = (int8_t)i;          // LEDC channels 0..2, the backlight owns 7
    ledcSetup(ledc_[i], 2000, 8);
    ledcAttachPin(pin_[i], ledc_[i]);
    writeChannel(i, 0);
  }
}

void StatusLed::writeChannel(int i, uint8_t value) {
  if (ledc_[i] < 0) return;
  ledcWrite(ledc_[i], 255 - value);   // the LED is common anode: low lights up
}

void StatusLed::set(uint8_t r, uint8_t g, uint8_t b) {
  writeChannel(0, r);
  writeChannel(1, g);
  writeChannel(2, b);
}
