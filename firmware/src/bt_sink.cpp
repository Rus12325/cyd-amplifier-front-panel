#include "bt_sink.h"

#if CYD_BT_SINK

#include <driver/i2s.h>

#include "BluetoothA2DPSink.h"

#include "audio.h"

namespace bt {
namespace {

BluetoothA2DPSink gSink;
State gState = State::Idle;
State gLoggedState = State::Idle;
volatile bool gStreaming = false;
uint32_t gLastPacketMs = 0;
char gPeer[32] = "";
char gTitle[64] = "";
char gArtist[64] = "";

// Decimation state: the phone sends 44.1 or 48 kHz stereo; the meter only needs
// around 16 kHz, and pushing every packet would starve the display task.
int gDecim = 3;
int gDecimPhase = 0;
float gPrev = 0.0f;

// ------------------------------------------------------------------ test tone
// 440 Hz through the very output the music uses: the fastest way to tell an
// amplifier, a relay and a speaker apart while wiring the panel up.
constexpr float kToneHz = 440.0f;
constexpr int kToneFrames = 256;
constexpr float kToneLevel = 0.25f;      // -12 dB, kind to tweeters

bool gTone = false;
TaskHandle_t gToneTask = nullptr;

void toneTask(void*) {
  int16_t block[kToneFrames * 2];
  float phase = 0.0f;
  const float step = 2.0f * PI * kToneHz / 44100.0f;

  for (;;) {
    // Never write while the library is feeding the same DMA buffer.
    if (!gTone || streaming()) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    for (int i = 0; i < kToneFrames; ++i) {
      const int16_t sample =
          (int16_t)(sinf(phase) * kToneLevel * 32767.0f);
      phase += step;
      if (phase >= 2.0f * PI) phase -= 2.0f * PI;
      block[i * 2] = sample;
      block[i * 2 + 1] = sample;
    }
#if !CYD_BT_I2S_OUT
    // The internal DAC takes offset binary, exactly like the conversion the
    // library applies in this mode.
    for (int i = 0; i < kToneFrames * 2; ++i) {
      block[i] = (int16_t)((uint16_t)(block[i] + 0x8000));
    }
#endif
    size_t written = 0;
    i2s_write(I2S_NUM_0, block, sizeof(block), &written, portMAX_DELAY);
  }
}

void onSampleRate(uint16_t rate) {
  gDecim = (int)((float)rate / 16000.0f + 0.5f);
  if (gDecim < 1) gDecim = 1;
  if (gDecim > 8) gDecim = 8;
  audio::setSampleRate((float)rate / (float)gDecim);
  Serial.printf("[bt] stream %u Hz, meter at %.0f Hz\n", rate,
                (float)rate / (float)gDecim);
}

// Passive tap: i2s_output stays true, so the library keeps streaming to the DAC
// while we take a copy for the meter.
void onPcm(const uint8_t* data, uint32_t length) {
  const int16_t* samples = reinterpret_cast<const int16_t*>(data);
  const uint32_t count = length / 2;
  gStreaming = true;
  gLastPacketMs = millis();

  // Music and the tone would take turns on the same DMA buffer.
  if (gTone) {
    gTone = false;
    Serial.println("[bt] stream arrived, test tone off");
  }

  for (uint32_t i = 0; i + 1 < count; i += 2) {
    if (gDecimPhase++ < gDecim) continue;
    gDecimPhase = 0;
    // Downmix and scale to -1..1
    const float mono =
        ((float)samples[i] + (float)samples[i + 1]) * (0.5f / 32768.0f);
    // Cheap anti-alias: two-tap average before dropping every gDecim-th frame
    audio::push(0.5f * (mono + gPrev));
    gPrev = mono;
  }
}

void onAvrcMetadata(uint8_t id, const uint8_t* text) {
  // id values: ESP_AVRC_MD_ATTR_TITLE=1, ESP_AVRC_MD_ATTR_ARTIST=2
  switch (id) {
    case ESP_AVRC_MD_ATTR_TITLE:
      strncpy(gTitle, (const char*)text, sizeof(gTitle) - 1);
      gTitle[sizeof(gTitle) - 1] = '\0';
      Serial.printf("[bt] title : %s\n", gTitle);
      break;
    case ESP_AVRC_MD_ATTR_ARTIST:
      strncpy(gArtist, (const char*)text, sizeof(gArtist) - 1);
      gArtist[sizeof(gArtist) - 1] = '\0';
      Serial.printf("[bt] artist: %s\n", gArtist);
      break;
    default:
      break;
  }
}

void onConnectionState(esp_a2d_connection_state_t state, void*) {
  switch (state) {
    case ESP_A2D_CONNECTION_STATE_CONNECTED:
      gState = State::Connected;
      break;
    case ESP_A2D_CONNECTION_STATE_CONNECTING:
      gState = State::Connecting;
      break;
    case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
    default:
      gState = State::Idle;
      gStreaming = false;
      break;
  }
}

}  // namespace

void begin() {
#if CYD_BT_I2S_OUT
  // External I2S DAC: three pins, 16 bit stereo.
  i2s_pin_config_t pins = {
      .mck_io_num = I2S_PIN_NO_CHANGE,
      .bck_io_num = CFG_PIN_I2S_BCLK,
      .ws_io_num = CFG_PIN_I2S_LRCK,
      .data_out_num = CFG_PIN_I2S_DOUT,
      .data_in_num = I2S_PIN_NO_CHANGE,
  };
  gSink.set_pin_config(pins);
#else
  // The board's own amplifier sits on GPIO 26, which is the ESP32's DAC channel
  // 2, so the audio can leave the chip right there: one pin instead of three,
  // and the backlight on GPIO 21 keeps its PWM.  Same settings as the library's
  // own defaults, only the mode changes.
  i2s_config_t cfg = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX |
                           I2S_MODE_DAC_BUILT_IN),
      .sample_rate = 44100,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = (i2s_comm_format_t)I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = 0,
      .dma_buf_count = 8,
      .dma_buf_len = 64,
      .use_apll = false,
      .tx_desc_auto_clear = true,
      .fixed_mclk = 0,
      .mclk_multiple = (i2s_mclk_multiple_t)0,   // I2S_MCLK_MULTIPLE_DEFAULT
      .bits_per_chan = I2S_BITS_PER_CHAN_DEFAULT,
  };
  gSink.set_i2s_config(cfg);
#endif

  gSink.set_stream_reader(onPcm, true);
  gSink.set_sample_rate_callback(onSampleRate);
  gSink.set_on_connection_state_changed(onConnectionState);
  gSink.set_avrc_metadata_callback(onAvrcMetadata);
  // Request only title and artist — album/genre waste the tiny BT bandwidth.
  gSink.set_avrc_metadata_attribute_mask(
      ESP_AVRC_MD_ATTR_TITLE | ESP_AVRC_MD_ATTR_ARTIST);
  gSink.set_auto_reconnect(true, 5);
  gSink.start(CFG_BT_DEVICE_NAME);

#if !CYD_BT_I2S_OUT
  // start() enables both internal DAC channels, and channel 1 is GPIO 25 -
  // which is the touch clock on this board.  Drop back to channel 2 (GPIO 26,
  // the on-board amplifier) only.
#if CYD_DAC_BOTH
  i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
#else
  i2s_set_dac_mode(I2S_DAC_CHANNEL_DISABLE);
  i2s_set_dac_mode(I2S_DAC_CHANNEL_LEFT_EN);      // LEFT = DAC2 = GPIO 26
#endif
#endif

  Serial.printf("[bt] advertising as \"%s\"\n", CFG_BT_DEVICE_NAME);
#if CYD_BT_I2S_OUT
  Serial.printf("[bt] i2s output: bclk %d, lrck %d, din %d\n", CFG_PIN_I2S_BCLK,
                CFG_PIN_I2S_LRCK, CFG_PIN_I2S_DOUT);
#else
  Serial.printf("[bt] audio output: internal DAC, GPIO %d, 8 bit\n",
                CFG_PIN_AUDIO_DAC);
#endif

  if (gToneTask == nullptr) {
    xTaskCreatePinnedToCore(toneTask, "tone", 3072, nullptr, 1, &gToneTask, 0);
  }
}

void end() {
  gTone = false;
  gSink.end();
}

void setTestTone(bool on) {
  if (on == gTone) return;
  gTone = on;
  Serial.printf("[bt] test tone %s\n", on ? "on, 440 Hz" : "off");
}

bool testTone() { return gTone; }

State state() {
  State s = gState;
  if (gStreaming && millis() - gLastPacketMs < 1500) s = State::Streaming;
  // Log every transition: a link that flaps while the music plays repaints
  // the status bar each time, and without this line such a flap is invisible
  // in the serial log while the panel visibly flickers.
  if (s != gLoggedState) {
    gLoggedState = s;
    Serial.printf("[bt] link: %s\n",
                  s == State::Streaming ? "playing"
                  : s == State::Connected ? "paused"
                  : s == State::Connecting ? "linking"
                                           : "pair me");
  }
  return s;
}

bool connected() { return gState != State::Idle; }

bool streaming() { return state() == State::Streaming; }

const char* stateText() {
  switch (state()) {
    case State::Idle: return "PAIR ME";
    case State::Connecting: return "LINKING";
    case State::Connected: return "PAUSED";
    case State::Streaming: return "PLAYING";
  }
  return "?";
}

// The peer name callback only exists in newer library releases; keeping the
// accessor means the UI can grow one without touching this file.
const char* peerName() { return gPeer; }

const char* trackTitle()  { return gTitle; }
const char* trackArtist() { return gArtist; }

void avrcPlay()  { if (gState != State::Idle) gSink.play(); }
void avrcPause() { if (gState != State::Idle) gSink.pause(); }
void avrcNext()  { if (gState != State::Idle) gSink.next(); }
void avrcPrev()  { if (gState != State::Idle) gSink.previous(); }

}  // namespace bt

#endif  // CYD_BT_SINK
