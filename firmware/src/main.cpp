// main.cpp - the amplifier front panel on a CYD (ESP32-2432S028).
//
//   setup()   brings the panel up in a safe state: relays de-energised, mute
//             engaged, amplifier off, and reports the wiring it found
//   loop()    runs the power/source state machines, the touch UI and the meter
//
// Everything the panel switches is a logical channel from config.h, so the same
// code drives the two-relay build and the PCF8574 build.
#include <Arduino.h>
#include <esp_task_wdt.h>
#include <esp_system.h>

#include "amp.h"
#include "audio.h"
#include "config.h"
#include "encoder.h"
#include "hid.h"
#include "io.h"
#include "ota.h"
#include "settings.h"
#include "touch.h"
#include "ui.h"
#include "wire_test.h"

#if CYD_BT_SINK
#include "bt_sink.h"
#endif

// Watchdog timeout: 30 s — the loop() should never stall that long.
constexpr uint32_t kWdtSec = 30;

namespace {

MuteGate gMute;
AmpController gAmp;
SourceController gSrc;
StatusLed gLed;

audio::Snapshot gSnap;

uint32_t gNextUi = 0;
uint32_t gNextLed = 0;
uint32_t gNextHb = 10000;               // first heartbeat 10 s after boot
uint32_t gNextNvs = 60000;              // flush totalRunSeconds every minute
uint32_t gTapCheckedAt = 0;
bool gTapReported = false;
bool gWireReported = true;

// Sleep / silence timer
uint32_t gSilenceSince = 0;             // millis() when audio last seen
bool gSilenceArmed = false;

// Reset reason, logged once at boot
const char* resetReasonStr() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_EXT:       return "ext-reset";
    case ESP_RST_SW:        return "sw-reset";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "int-wdt";
    case ESP_RST_TASK_WDT:  return "task-wdt";
    case ESP_RST_WDT:       return "wdt";
    case ESP_RST_DEEPSLEEP: return "deep-sleep";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_SDIO:      return "sdio";
    default:                return "unknown";
  }
}

// Optional hardware power switch (see CFG_PIN_POWER_BTN).
bool gBtnDown = false;
bool gBtnLongFired = false;
uint32_t gBtnAt = 0;

void updateLed(uint32_t now) {
  if (now < gNextLed) return;
  gNextLed = now + 100;

  if (gAmp.isBusy()) {
    const bool phase = ((now / 300) & 1) != 0;   // amber blink while switching
    gLed.set(phase ? 70 : 0, phase ? 30 : 0, 0);
    return;
  }
#if CYD_BT_SINK
  if (gAmp.isOn() && bt::streaming()) {
    gLed.set(0, 0, 90);                          // blue: music over Bluetooth
    return;
  }
  if (gAmp.isOn() && bt::connected()) {
    gLed.set(0, 20, 60);
    return;
  }
#endif
  if (gAmp.isOn()) {
    gLed.set(0, 90, 0);                          // green: on
  } else {
    gLed.set(10, 0, 0);                          // dim red: standby
  }
}

void reportTapOnce(uint32_t now) {
  if (gTapReported || now < 4000) return;
  gTapReported = true;
  const float spread = gSnap.stats.maxRaw - gSnap.stats.minRaw;
#if CYD_BT_SINK
  Serial.println("tap          : decoded PCM (no ADC in this build)");
  (void)spread;
#else
  Serial.printf("tap          : %.0f..%.0f counts (spread %.0f), dc %.2f\n",
                gSnap.stats.minRaw, gSnap.stats.maxRaw, spread,
                gSnap.stats.dcLevel);
  if (spread < 30.0f) {
    Serial.println("               WARN: the tap looks flat - check the divider and");
    Serial.println("               that something is playing");
  }
#endif
}

void selfTest() {
  Serial.printf("build        : %s\n",
                CYD_BT_SINK ? "cyd_bt - the CYD is the Bluetooth receiver"
                            : "cyd - 5.1 panel: PC source, 24 V bus + mute");
#if CYD_BLE_HID
  Serial.printf("pc volume    : BLE HID \"%s\", volume up/down/mute keys\n",
                CFG_HID_NAME);
#else
  Serial.println("pc volume    : none in this build (the A2DP stack owns the radio)");
#endif
  Serial.printf("touch        : XPT2046, sclk %d mosi %d miso %d cs %d\n",
                CFG_PIN_TOUCH_SCLK, CFG_PIN_TOUCH_MOSI, CFG_PIN_TOUCH_MISO,
                CFG_PIN_TOUCH_CS);
  Serial.printf("amp power    : %s%s\n", ioDescription(CH_AMP_POWER),
                ioChannel(CH_AMP_POWER).bound() ? "" : "   <-- NOT WIRED");
  Serial.printf("mute relay   : %s%s\n", ioDescription(CH_MUTE),
                ioChannel(CH_MUTE).bound() ? ""
                                           : "   (not fitted, switching will click)");
  Serial.printf("spare key    : %s%s\n", ioDescription(CH_SPARE_0),
                ioChannel(CH_SPARE_0).bound() ? "" : "   (not wired)");
  Serial.println("boards       : 3 x stereo on one 24 V bus, PC on three jacks");
  for (int i = 0; i < CFG_SOURCE_COUNT; ++i) {
    const SourceDef& d = CFG_SOURCES[i];
    if (!ioChannel(d.channel).bound()) {
      Serial.printf("source       : %s (wired straight to the boards)\n", d.label);
      continue;
    }
    Serial.printf("source %-9s: %s on %s\n", d.label, ioDescription(d.channel),
                  d.level ? "driven high" : "driven low");
  }
#if USE_IO_EXPANDER
  Serial.printf("i2c expander : %s\n", ioBusOk() ? "OK" : "NOT ANSWERING");
#endif
#if CFG_ENC_SHADOWS_TAP
  Serial.printf("audio tap    : off - GPIO%d belongs to the encoder\n",
                CFG_PIN_AUDIO_ADC);
#elif !CYD_BT_SINK
  Serial.printf("audio tap    : GPIO%d (ADC1_7), %d Hz, %d bands\n",
                CFG_PIN_AUDIO_ADC, CFG_SAMPLE_RATE, dsp::kBands);
#elif CYD_BT_I2S_OUT
  Serial.printf("audio out    : external I2S DAC, bclk %d, lrck %d, din %d\n",
                CFG_PIN_I2S_BCLK, CFG_PIN_I2S_LRCK, CFG_PIN_I2S_DOUT);
#else
  Serial.printf("audio out    : internal DAC, GPIO %d (8 bit) -> on-board amp\n",
                CFG_PIN_AUDIO_DAC);
#endif
  Serial.printf("settings     : %s\n",
                gSettings.loaded() ? "restored from NVS" : "defaults");
  Serial.printf("free heap    : %u bytes\n", ESP.getFreeHeap());
  Serial.println("state        : amplifier off, mute engaged");
}

// What each switched channel is for in this build, so the wiring-test report
// reads like the build rather than like a pin list.
const char* channelRole(int ch) {
  switch (ch) {
    case CH_AMP_POWER: return "bus";
    case CH_MUTE: return "mute";
    case CH_SPARE_0: return "key";
    default: return "";
  }
}

void reportWiringTest() {
  if (gWireReported || wiretest::running() || wiretest::tested() == 0) return;
  gWireReported = true;
  Serial.printf("wiring test  : %d channels, %d failed\n", wiretest::tested(),
                wiretest::failures());
  for (int i = 0; i < CH_COUNT; ++i) {
    if (!ioChannel(i).bound()) continue;
    Serial.printf("   ch%-2d %-9s %-5s %s\n", i, ioDescription(i),
                  channelRole(i), wiretest::resultText(i));
    if (wiretest::result(i) != wiretest::Result::Ok) {
      Serial.println("        line did not follow the command - wrong pin, or a");
      Serial.println("        pin another peripheral owns");
    }
  }
}

void pollHardwareButton() {
#if CFG_PIN_POWER_BTN >= 0
  const bool pressed = digitalRead(CFG_PIN_POWER_BTN) == LOW;
  const uint32_t now = millis();
  if (pressed && !gBtnDown) {
    gBtnDown = true;
    gBtnLongFired = false;
    gBtnAt = now;
  } else if (pressed && !gBtnLongFired && now - gBtnAt >= CFG_BTN_LONG_MS) {
    gBtnLongFired = true;                 // long press = force off
    gAmp.requestOff();
  } else if (!pressed && gBtnDown) {
    gBtnDown = false;
    if (!gBtnLongFired) gAmp.toggle();
  }
#endif
}

}  // namespace

void setup() {
  Serial.begin(CFG_SERIAL_BAUD);
  delay(200);
  Serial.printf("\n=== CYD amp panel ===\nboot         : %s, %.0f ms\n",
                CYD_BT_SINK ? "cyd_bt (I2S + BT sink)" : "cyd (analog tap)",
                (double)millis());
  Serial.printf("reset reason : %s\n", resetReasonStr());

  gSettings.begin();

  // Session / runtime counters.
  gSettings.data().sessionCount++;
  Serial.printf("session      : #%lu (total uptime %lu s so far)\n",
                (unsigned long)gSettings.data().sessionCount,
                (unsigned long)gSettings.data().totalRunSeconds);
  gSettings.save();

  // Hardware watchdog: resets the chip if loop() stalls for kWdtSec seconds.
  esp_task_wdt_init(kWdtSec, true);
  esp_task_wdt_add(nullptr);   // watch the calling (main Arduino) task

  ioBegin();

  // Everything that reports the wiring comes first: the display may block on
  // the first touch calibration, and a silent serial port helps nobody.
  selfTest();

  uiBegin(&gSettings);        // may wait up to 20 s for touch calibration

  gMute.begin(&ioChannel(CH_MUTE));
  gAmp.begin(&ioChannel(CH_AMP_POWER), &gMute);
  gSrc.begin(&gMute);
  gLed.begin();

  audio::begin();
  audio::setGain(gSettings.data().meterGain);

#if CYD_BLE_HID
  hid::begin();               // the PC pairs with the panel once, then the
                              // volume controls on the screen are its keys
#endif
  encoder::begin();           // no-op unless CFG_ENC_ENABLED

#if CFG_PIN_POWER_BTN >= 0
  pinMode(CFG_PIN_POWER_BTN, INPUT_PULLUP);
#endif

#if CYD_BT_SINK
  bt::begin();
#endif

#if CYD_BT_SINK && !CYD_BT_I2S_OUT
  // Starting the A2DP sink configures the internal DAC, and on this board GPIO
  // 25 is both DAC channel 1 and the touch clock: claim the touch pins again.
  touch::begin();
#endif

  // Restore the last input, then optionally bring the amplifier up.
  gSrc.select(gSettings.data().source);
  if (gSettings.data().powerOnBoot) gAmp.requestOn();

  otaBegin();

  gSilenceSince = millis();

  Serial.printf("ready        : %u bytes free\n", ESP.getFreeHeap());
  gTapCheckedAt = millis();
}

void loop() {
  const uint32_t now = millis();

  esp_task_wdt_reset();   // pet the watchdog

  otaLoop();
  if (otaBusy()) return;  // hands off everything while flashing

  gMute.loop(now);
  gAmp.loop(now);
  gSrc.loop(now);
  pollHardwareButton();

#if CYD_BLE_HID
  hid::loop(now);               // one key press at a time, the radio is slow
  encoder::loop(now);
  {
    // The knob sends exactly what the touch chips send.
    const int steps = encoder::takeSteps();
    if (steps > 0) hid::volumeUp((uint8_t)steps);
    else if (steps < 0) hid::volumeDown((uint8_t)(-steps));
    if (encoder::takePush()) hid::muteToggle();
  }
#endif

  if (wiretest::running()) wiretest::loop(now);
  reportWiringTest();

  UiStatus st;
  st.ampText = gAmp.stateText();
  st.ampOn = gAmp.isOn();
  st.ampBusy = gAmp.isBusy();
  st.source = gSrc.current();
  st.muteFitted = gMute.fitted();
  st.busOk = ioBusOk();
#if CYD_BT_SINK
  st.btPresent = true;
  st.btText = bt::stateText();
  st.btConnected = bt::connected();
  st.btState = (uint8_t)bt::state();
  st.toneAvailable = true;
  st.toneOn = bt::testTone();
  st.trackTitle = bt::trackTitle();
  st.trackArtist = bt::trackArtist();
#endif

  const UiEvent ev = uiPoll(st);
  switch (ev.action) {
    case UiAction::PowerToggle:
      gAmp.toggle();
      break;
    case UiAction::PowerOff:
      gAmp.requestOff();
      break;
    case UiAction::SelectSource:
      gSrc.select(ev.arg);
      gSettings.data().source = (int8_t)ev.arg;
      gSettings.save();
      break;
    case UiAction::RunWiringTest:
      if (!gAmp.isOn() && !gAmp.isBusy()) {
        gWireReported = false;
        wiretest::begin();
      }
      break;
    case UiAction::ToggleTestTone:
#if CYD_BT_SINK
      bt::setTestTone(!bt::testTone());
#endif
      break;
    case UiAction::AvrcPlay:
#if CYD_BT_SINK
      bt::avrcPlay();
#endif
      break;
    case UiAction::AvrcPause:
#if CYD_BT_SINK
      bt::avrcPause();
#endif
      break;
    case UiAction::AvrcNext:
#if CYD_BT_SINK
      bt::avrcNext();
#endif
      break;
    case UiAction::AvrcPrev:
#if CYD_BT_SINK
      bt::avrcPrev();
#endif
      break;
    case UiAction::VolumeUp:
    case UiAction::VolumeDown:
    case UiAction::VolumeMute:
#if CYD_BLE_HID
      if (ev.action == UiAction::VolumeUp) {
        hid::volumeUp((uint8_t)(ev.arg > 0 ? ev.arg : 1));
      } else if (ev.action == UiAction::VolumeDown) {
        hid::volumeDown((uint8_t)(ev.arg > 0 ? ev.arg : 1));
      } else {
        hid::muteToggle();
      }
#endif
      break;
    case UiAction::SettingsChanged:
    case UiAction::None:
      break;
  }

  if (now >= gNextUi) {
    gNextUi = now + CFG_UI_PERIOD_MS;
    audio::read(gSnap);
    uiRender(st, gSnap);
    reportTapOnce(now);
  }

  updateLed(now);

  // Silence sleep timer: auto-off after sleepAfterMin minutes of silence.
  {
    const uint8_t sleepMin = gSettings.data().sleepAfterMin;
    const bool hasAudio =
#if CYD_BT_SINK
      bt::streaming();
#else
      gSnap.level > 0.01f;
#endif
    if (hasAudio) {
      gSilenceSince = now;
      gSilenceArmed = gAmp.isOn();
    }
    if (gSilenceArmed && sleepMin > 0 && gAmp.isOn()) {
      const uint32_t silenceMs = (uint32_t)sleepMin * 60000UL;
      if (now - gSilenceSince >= silenceMs) {
        Serial.printf("[sleep] %u min of silence, amp off\n", sleepMin);
        gAmp.requestOff();
        gSilenceArmed = false;
      }
    }
  }

  // Flush totalRunSeconds to NVS once a minute.
  if (now >= gNextNvs) {
    gNextNvs = now + 60000;
    gSettings.data().totalRunSeconds += 60;
    gSettings.save();
  }

  // Heartbeat every 10 s.
  if (now >= gNextHb) {
    gNextHb = now + 10000;
#if CYD_BLE_HID
    Serial.printf("[hb] pc keys   : %s, vol ~%d%%%s, %lu sent, %lu dropped\n",
                  hid::stateText(), hid::volumeEstimate(),
                  hid::muteEstimate() ? " muted" : "",
                  (unsigned long)hid::sentSteps(),
                  (unsigned long)hid::droppedSteps());
#endif
    Serial.printf("[hb] t=%lu s heap=%u state=%s%s\n", now / 1000UL,
                  ESP.getFreeHeap(), gAmp.isOn() ? "amp-on" : "amp-off",
#if CYD_BT_SINK
                  bt::streaming() ? " streaming" : ""
#else
                  ""
#endif
    );
  }
  (void)gTapCheckedAt;
}
