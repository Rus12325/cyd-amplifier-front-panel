// config.h - hardware configuration for the CYD (ESP32-2432S028) amp panel.
//
// The board leaves very little IO: the display, the touch controller, the SD
// slot, the RGB LED and the serial port own nearly every pin.  What is left:
//
//   GPIO 22, GPIO 27   on the P3 / CN1 headers - the only free outputs
//   GPIO 35            P3 header, input only, ADC1 channel 7
//   GPIO 16            the RGB LED's green pin, borrowed by the mute relay
//
// So the panel is wired around two outputs plus one ADC input, and everything
// else this project switches has to share those.
//
// ---------------------------------------------------------------------------
// The 5.1 build (default): three stereo boards, one PC, two relays
//
//   source      the PC's three analog jacks (green FL/FR, black RL/RR, orange
//               C/SW) go straight into the three amplifier boards.  There is
//               no input relay and no master volume on the panel: the volume
//               lives in Windows, and the boards' own gain jumpers set the
//               channel balance.
//   GPIO 27 ->  relay K1: the 24 V bus for all three boards (a MOSFET key also
//               works; the sequencing below is the same either way)
//   GPIO 16 ->  relay K2: signal mute - one contact shorting all six hot input
//               lines to the star ground, so nothing thumps when the bus or a
//               mode changes
//   GPIO 22 ->  spare key, wired but not driven by the UI yet: the rear+sub
//               board's power (a "music mode"), a fan, or the RGB LED's green
//               pin if the mute relay ever moves
//   GPIO 35 <-  analog tap of one channel (front left), for the visualiser
//
//   The PC's volume is not in this list on purpose: the panel sends it over
//   Bluetooth (BLE HID, the volume keys of a multimedia keyboard), because the
//   PC's three jacks bypass every volume control this amplifier has.  The
//   inputs for that are the touch controls on the screen, plus an optional
//   rotary encoder on GPIO 22/35 - see the two blocks further down.
//
//   Power budget: 24 V / 4 A is 96 W for six channels, so the panel is the
//   only thing that switches all of them at once - see docs/wiring.md for the
//   per-channel allotment and the bulk capacitance the 4 A supply needs.
//
// ---------------------------------------------------------------------------
// Variant 2 (CYD_BT_SINK=1) - the CYD is the Bluetooth receiver itself
//   An A2DP sink decodes the phone's stream and pushes PCM into the ESP32's own
//   DAC on GPIO 26, which this board already routes into its on-board 3 W
//   amplifier and the P4 speaker header.  That is one pin instead of the three
//   an external I2S DAC would need, so nothing else has to move: the backlight
//   keeps GPIO 21 and its PWM, the touch keeps its clock on GPIO 25, the relays
//   stay on the same pins as above, and the RGB LED survives.
//   The visualiser is fed from the decoded PCM, which is free and much better
//   than the ADC tap.  The internal DAC is 8 bit, audible as a little hiss at
//   low volume.
//   -DCYD_BT_I2S_OUT=1 switches the audio to an external 16-bit I2S DAC on
//   GPIO 21/22/27 instead, at the price of the backlight and BOTH relays - in
//   the 5.1 build those pins are the mute relay and the 24 V bus.  Pick one:
//   either the panel switches the amplifier, or the panel feeds an I2S DAC.
//
// Variant 3 (USE_IO_EXPANDER=1, either of the above) - a PCF8574 on I2C gives
//   eight switched channels out of two pins, so the panel can power each board
//   separately (music / movie / night modes), select inputs and keep the RGB
//   LED.  In the A2DP variant there are no pins left for I2C, so the expander
//   only makes sense with CYD_BT_SINK=0.
// ---------------------------------------------------------------------------
#pragma once

#include <Arduino.h>

#ifndef CYD_BT_SINK
#define CYD_BT_SINK 0
#endif
#ifndef USE_IO_EXPANDER
#define USE_IO_EXPANDER 0
#endif

// ------------------------------------------------------------------ display
#define CFG_TFT_ROTATION 1              // 1 = landscape, 320 x 240
#define CFG_TFT_WIDTH 320
#define CFG_TFT_HEIGHT 240
#define CFG_PIN_TFT_BL 21
#define CFG_BL_CHANNEL 7                // LEDC channel for the backlight
#define CFG_BL_FREQ 5000
#define CFG_BRIGHT_DEFAULT 200          // 0..255
// In the CYD_BT_SINK variant GPIO21 carries the I2S bit clock, so the backlight
// is not under firmware control any more (platformio.ini drops TFT_BL there).
#define CFG_BRIGHT_MIN 24

// The CYD RGB LED is common anode: writing LOW lights a colour.
// GPIO 16 is the green pin AND the mute relay output in the 5.1 build; the LED
// driver skips pins that a switched channel already owns (see io.cpp), so the
// relay wins and the status LED simply goes without its green.
#define CFG_PIN_LED_R 4
#define CFG_PIN_LED_G 16
#define CFG_PIN_LED_B 17

// ----------------------------------------------------------------- audio in
#if CYD_BT_SINK
#define CFG_SAMPLE_RATE 22050           // decoded PCM, decimated 2:1
#define CFG_SAMPLE_DECIM 2
#else
#define CFG_SAMPLE_RATE 8000            // ADC sampler: 4 kHz of bandwidth
#endif
#define CFG_PIN_AUDIO_ADC 35            // GPIO35 = ADC1 channel 7

// Where the decoded audio leaves the board (CYD_BT_SINK only).
//
// Default: the ESP32's own DAC on GPIO 26 - DAC channel 2, and the pin this
// board wires into its on-board amplifier.  One pin, so the backlight (GPIO 21)
// and the touch clock (GPIO 25) stay where they are; driving the backlight pin
// with an I2S bit clock is exactly what made the panel flicker.
//
// -DCYD_BT_I2S_OUT=1: 16-bit audio to an external I2S DAC (PCM5102A, MAX98357A)
// on GPIO 21 (BCLK) / 22 (LRCK) / 27 (DIN).  Those three pins are the
// backlight, the spare key and the 24 V bus relay in the 5.1 build, so this
// option is only for a panel that does not switch the amplifier itself.
#ifndef CYD_BT_I2S_OUT
#define CYD_BT_I2S_OUT 0
#endif

// The A2DP library enables both internal DAC channels; GPIO 25 is the touch
// clock here, so only channel 2 (GPIO 26) may be enabled.  If the audio comes
// out distorted, -DCYD_DAC_BOTH=1 restores the library's behaviour.
#ifndef CYD_DAC_BOTH
#define CYD_DAC_BOTH 0
#endif

#if CYD_BT_SINK
#if CYD_BT_I2S_OUT
// Overridable, because the 5.1 panel needs 21/22/27 for the backlight, the
// spare key and the 24 V bus: -DCFG_PIN_I2S_BCLK=4 -DCFG_PIN_I2S_LRCK=17
// -DCFG_PIN_I2S_DOUT=22 moves the DAC onto the RGB-LED pins instead.
#ifndef CFG_PIN_I2S_BCLK
#define CFG_PIN_I2S_BCLK 21
#endif
#ifndef CFG_PIN_I2S_LRCK
#define CFG_PIN_I2S_LRCK 22
#endif
#ifndef CFG_PIN_I2S_DOUT
#define CFG_PIN_I2S_DOUT 27
#endif
#else
#define CFG_PIN_AUDIO_DAC 26
#endif
#define CFG_BT_DEVICE_NAME "Amp panel"
#endif

// True when the backlight is under firmware control (LEDC PWM on GPIO 21).
// It is off whenever the I2S bit clock shares GPIO 21 - override it with
// -DCFG_BL_FITTED=1 if the I2S pins were moved off the backlight pin.
#ifndef CFG_BL_FITTED
#if CYD_BT_SINK && CYD_BT_I2S_OUT
#define CFG_BL_FITTED 0
#else
#define CFG_BL_FITTED 1
#endif
#endif

// -DCYD_BL_STATIC=1 (env cyd_bt_static): the pin is never attached to a PWM
// timer - it stays at the level TFT_eSPI set during init and cannot change in
// firmware at all.  If the panel still pulses with this image, the cause is on
// the board's power rails or in the backlight circuit, not in the code.
#ifndef CYD_BL_STATIC
#define CYD_BL_STATIC 0
#endif

// -------------------------------------------------------------------- touch
// The XPT2046 sits on its own SPI bus on this board.  TFT_eSPI's touch handler
// talks over the display bus instead, which is why taps never registered, so
// touch.cpp bit-bangs these pins.  GPIO 25 is also DAC channel 1: keep it out
// of the internal-DAC build (see CYD_DAC_BOTH above).
#define CFG_PIN_TOUCH_SCLK 25
#define CFG_PIN_TOUCH_MOSI 32
#define CFG_PIN_TOUCH_MISO 39
#define CFG_PIN_TOUCH_CS 33
#define CFG_TOUCH_PRESS_MIN 150         // raw Z1-Z2 delta that counts as a touch
#define CFG_ADC_ATTEN ADC_11db
#define CFG_BAND_LOW_HZ 40.0f
#define CFG_BAND_HIGH_HZ 16000.0f       // clamped to 0.45 x sample rate
#define CFG_METER_FLOOR_DB -60.0f

// FFT size, band count and meter ballistics live in spectrum.h (DSP_*), so the
// DSP stays free of anything Arduino and the defaults can be overridden from
// platformio.ini with -D if a variant needs different ones.

// ------------------------------------------------- PC volume keys (BLE HID)
// The panel talks to the PC as a small Bluetooth LE HID device ("consumer
// control"): the volume up / down / mute usages, the same keys a multimedia
// keyboard sends.  Nothing has to be wired for it - it is the ESP32's own
// radio - and it is what makes the volume controls on the panel work at all:
// the PC feeds the three boards through analog jacks, so there is no amplifier
// volume to turn, the volume lives in Windows.
//
// The BLE stack and the A2DP stack need the same single Bluetooth controller,
// and the A2DP library brings it up in classic-only mode, so every CYD_BT_SINK
// build switches this off: the volume row disappears from the UI there.
#ifndef CYD_BLE_HID
#define CYD_BLE_HID 1
#endif
#if CYD_BLE_HID && CYD_BT_SINK
#error "CYD_BLE_HID=1 needs the BLE stack - the CYD_BT_SINK builds own the controller; pass -DCYD_BLE_HID=0"
#endif
#define CFG_HID_NAME "Amp panel keys"   // what the PC lists while pairing
#define CFG_HID_PRESS_MS 20             // how long a key stays down
#define CFG_HID_GAP_MS 14               // pause between two key presses
#define CFG_HID_QUEUE 16                // steps waiting for the radio
#define CFG_HID_STEP_PCT 2              // what one press is worth in Windows
#define CFG_HID_REPEAT_DELAY_MS 400     // hold a +/- chip this long to repeat
#define CFG_HID_REPEAT_MS 140           // repeat rate while it is held
#define CFG_HID_DRAG_PX 9               // touch strip: pixels per step

// ------------------------------------------------------------ rotary encoder
// Optional: a real quadrature encoder beside the touch strip.  The board has no
// free pin pair - an encoder wants two inputs and, ideally, a switch - so this
// is off by default and every pin has to be given up:
//
//   A, B     GPIO 22 (the spare key) and GPIO 35 (the ADC tap).  Both want an
//            external 10k pull-up to 3V3; GPIO 35 has no internal one.
//   switch   left out by default: GPIO 0 is the BOOT strap pin, and a knob held
//            down while the board powers up would land it in the ROM
//            bootloader.  Wire it to GPIO 0 if you accept that, or leave -1.
//
// -DCFG_ENC_ENABLED=1 (env cyd_enc) turns it on.  GPIO 35 then belongs to the
// encoder, so the visualiser loses its analog tap (CFG_ENC_SHADOWS_TAP): the
// panel is a remote control in that build, the spectrum needs the other one.
#ifndef CFG_ENC_ENABLED
#define CFG_ENC_ENABLED 0
#endif
#ifndef CFG_PIN_ENC_A
#define CFG_PIN_ENC_A 22
#endif
#ifndef CFG_PIN_ENC_B
#define CFG_PIN_ENC_B 35
#endif
#ifndef CFG_PIN_ENC_SW
#define CFG_PIN_ENC_SW -1
#endif
#ifndef CFG_ENC_REVERSE
#define CFG_ENC_REVERSE 0               // 1 swaps the direction of rotation
#endif

// GPIO 22 is the spare key - unless the encoder claims it, and then the key
// channel is simply not bound (the wiring test skips what is not bound).
#if CFG_ENC_ENABLED && (CFG_PIN_ENC_A == 22 || CFG_PIN_ENC_B == 22 || CFG_PIN_ENC_SW == 22)
#define CFG_PIN_SPARE_KEY -1
#else
#define CFG_PIN_SPARE_KEY 22
#endif

// True when the encoder sits on the audio-tap pin: no ADC, no spectrum.  (Only
// the ADC builds sample GPIO 35; in an A2DP build the meter runs on the decoded
// stream, so the pin is free there.)
#if CFG_ENC_ENABLED && !CYD_BT_SINK && \
    (CFG_PIN_ENC_A == CFG_PIN_AUDIO_ADC || CFG_PIN_ENC_B == CFG_PIN_AUDIO_ADC)
#define CFG_ENC_SHADOWS_TAP 1
#else
#define CFG_ENC_SHADOWS_TAP 0
#endif

// --------------------------------------------------------------- IO matrix
// Eight logical channels; every consumer (amp, sources) talks in channels and
// never touches a pin number.
enum : int8_t {
  CH_AMP_POWER = 0,      // 24 V bus for all three boards: relay K1 or a MOSFET
  CH_MUTE = 1,           // signal mute relay K2 (fitted in this build)
  CH_SPARE_0 = 2,        // spare key: rear+sub power, a fan, or the RGB LED
  CH_SRC_0 = 3,          // input relays - the PCF8574 build binds these
  CH_SRC_1 = 4,
  CH_SRC_2 = 5,
  CH_SRC_3 = 6,
  CH_SPARE_1 = 7,
  CH_COUNT = 8,
};

// Level that means "energised" for each channel.  Opto-isolated relay boards
// are usually active LOW: set the matching entry to false rather than adding
// an inverter.
#if USE_IO_EXPANDER
constexpr bool CFG_ACTIVE_HIGH[CH_COUNT] = {true, true, true, true, true, true, true, true};
constexpr int8_t CFG_PIN_I2C_SDA = 22;
constexpr int8_t CFG_PIN_I2C_SCL = 27;
constexpr uint8_t CFG_I2C_ADDRESS = 0x20;
#else
constexpr bool CFG_ACTIVE_HIGH[CH_COUNT] = {true, true, true, true, true, true, true, true};
constexpr int8_t CFG_DIRECT_PIN[CH_COUNT] = {
    /* bus  */ 27,     // K1: 24 V to all three boards (or the gate of a MOSFET)
    /* mute */ 16,     // K2: signal mute, borrowed from the RGB LED's green pin
    /* key  */ CFG_PIN_SPARE_KEY,   // spare key (GPIO 22) - -1 when the encoder owns it
    /* src0..spare1 */ -1, -1, -1, -1, -1,
};
#endif

// ----------------------------------------------------------------- sources
// label, channel, level.  Channels are pulled to "level" when the source is
// selected and the opposite level otherwise, so one relay can carry two inputs
// (BT on the NO contact, line-in on the NC contact).
struct SourceDef {
  const char* label;
  int8_t channel;
  bool level;
};

#if USE_IO_EXPANDER
constexpr SourceDef CFG_SOURCES[] = {
    {"PC 5.1", CH_SRC_0, true},
    {"BLUETOOTH", CH_SRC_1, true},
    {"SPARE", CH_SRC_2, true},
    {"SPARE 2", CH_SRC_3, true},
};
constexpr int CFG_SOURCE_COUNT = 4;
#else
// One source: the PC's three jacks are soldered to the three boards' inputs, so
// there is nothing to switch.  The entry still names the input on the screen.
// A Bluetooth module (or a second player) would go on CH_SPARE_0 - one relay,
// two sources - or on a PCF8574, which is what the expander build above is for.
constexpr SourceDef CFG_SOURCES[] = {
    {"PC 5.1", CH_SRC_0, true},
};
constexpr int CFG_SOURCE_COUNT = 1;
#endif

// ------------------------------------------------------------- amp control
#define CFG_AMP_MUTE_LEAD_MS 150        // mute before the power relay moves
#define CFG_AMP_WARMUP_MS 1500          // amp settles before unmuting
#define CFG_AMP_MUTE_TAIL_MS 250        // mute held after the relay drops
#define CFG_SRC_MUTE_LEAD_MS 25         // mute first, then move the relay
#define CFG_SRC_SWITCH_MS 120           // mute window while relays change
#define CFG_POWER_ON_DELAY_MS 120
#define CFG_DEFAULT_SOURCE 0

// Optional hardware power switch: the on-board BOOT button (GPIO 0) can double
// as one - it is pulled up on the module and free after boot.  Holding it down
// at reset still enters the serial bootloader, so it is safe, but leave it -1
// if the button is not easily reachable.
#define CFG_PIN_POWER_BTN -1
#define CFG_BTN_DEBOUNCE_MS 30
#define CFG_BTN_LONG_MS 900

// -------------------------------------------------------------------- misc
#define CFG_SERIAL_BAUD 115200
#define CFG_DIM_AFTER_MS 60000UL        // idle time before the backlight dims
#define CFG_DIM_BRIGHT 96              // dimmed duty: 38 %, still readable
#define CFG_AUTODIM_DEFAULT true
#define CFG_NVS_NAMESPACE "ampanel"
#define CFG_UI_PERIOD_MS 40             // ~25 fps redraws
#define CFG_ANALYZER_TASK_CORE 1
#define CFG_ANALYZER_TASK_PRIO 2
