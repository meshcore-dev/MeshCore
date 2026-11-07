#include "Sound.h"

#include <MeshCore.h>

#include <Arduino.h>
#include <Wire.h>
#include <SPIFFS.h>
#include <math.h>
#include "AudioBoard.h"      // pschatzmann/arduino-audio-driver
#include <driver/i2s.h>
#include <esp_heap_caps.h>

// Wio-S3 audio wiring: ES8311 codec on I2C (0x18), I2S MCLK 10 / BCK 11 /
// WS 12 / DOUT 16; speaker amp power (PA EN) is raised by the board init.
#define SND_I2S_MCLK 10
#define SND_I2S_BCK  11
#define SND_I2S_WS   12
#define SND_I2S_DOUT 16
#define SND_SAMPLE_RATE 44100

static DriverPins snd_pins;
static AudioBoard snd_codec(AudioDriverES8311, snd_pins);
static bool snd_ready = false;
static void (*snd_amp_ctl)(bool) = NULL;   // board hook: speaker amp power

void soundSetAmpControl(void (*fn)(bool on)) { snd_amp_ctl = fn; }

void soundInit() {
  // i2s_driver_install crashes inside IDF's cleanup if its DMA allocation
  // fails (LoadProhibited boot loop) - refuse to try without clear headroom
  if (heap_caps_get_free_size(MALLOC_CAP_DMA) < 60000) {
    MESH_DEBUG_PRINTLN("sound: low DMA heap - sound disabled this boot");
    return;
  }

  snd_pins.addI2C(PinFunction::CODEC, Wire);
  snd_pins.addI2S(PinFunction::CODEC, SND_I2S_MCLK, SND_I2S_BCK, SND_I2S_WS, SND_I2S_DOUT, -1);

  CodecConfig cfg;
  cfg.input_device = ADC_INPUT_NONE;
  cfg.output_device = DAC_OUTPUT_ALL;
  cfg.i2s.bits = BIT_LENGTH_16BITS;
  cfg.i2s.rate = RATE_44K;
  if (!snd_codec.begin(cfg)) {
    MESH_DEBUG_PRINTLN("sound: ES8311 init failed - sound disabled");
    return;
  }
  snd_codec.setVolume(70);

  i2s_config_t i2s_cfg = {};
  i2s_cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  i2s_cfg.sample_rate = SND_SAMPLE_RATE;
  i2s_cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  i2s_cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  i2s_cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  i2s_cfg.intr_alloc_flags = 0;
  i2s_cfg.dma_buf_count = 2;    // keep the DMA footprint minimal
  i2s_cfg.dma_buf_len = 256;
  i2s_cfg.tx_desc_auto_clear = true;
  if (i2s_driver_install(I2S_NUM_0, &i2s_cfg, 0, NULL) != ESP_OK) {
    MESH_DEBUG_PRINTLN("sound: i2s driver install failed");
    return;
  }
  i2s_pin_config_t pin_cfg = {};
  pin_cfg.mck_io_num = SND_I2S_MCLK;
  pin_cfg.bck_io_num = SND_I2S_BCK;
  pin_cfg.ws_io_num = SND_I2S_WS;
  pin_cfg.data_out_num = SND_I2S_DOUT;
  pin_cfg.data_in_num = I2S_PIN_NO_CHANGE;
  if (i2s_set_pin(I2S_NUM_0, &pin_cfg) != ESP_OK) {
    MESH_DEBUG_PRINTLN("sound: i2s pin config failed");
    return;
  }
  i2s_stop(I2S_NUM_0);   // clocks off until a tone plays (GNSS-adjacent EMI, battery)
  snd_ready = true;
  MESH_DEBUG_PRINTLN("sound: ES8311 + I2S ready");
}

bool soundReady() { return snd_ready; }

void soundBeep(int freq_hz, int duration_ms) {
  if (!snd_ready) return;
  if (snd_amp_ctl) snd_amp_ctl(true);
  i2s_start(I2S_NUM_0);
  delay(3);   // amp settle before the first samples
  const int total = SND_SAMPLE_RATE * duration_ms / 1000;
  const float amp = 6000.0f;   // gentle level; codec volume does the rest
  static int16_t buf[256 * 2];  // 256 stereo frames per chunk

  int written_frames = 0;
  while (written_frames < total) {
    int chunk = total - written_frames;
    if (chunk > 256) chunk = 256;
    for (int i = 0; i < chunk; i++) {
      float t = (float)(written_frames + i) / SND_SAMPLE_RATE;
      // short attack/decay envelope so the beep doesn't click
      float env = 1.0f;
      int pos = written_frames + i;
      if (pos < 220) env = pos / 220.0f;
      else if (pos > total - 220) env = (total - pos) / 220.0f;
      int16_t s = (int16_t)(amp * env * sinf(2.0f * (float)M_PI * freq_hz * t));
      buf[i * 2] = s;
      buf[i * 2 + 1] = s;
    }
    size_t written_bytes = 0;
    i2s_write(I2S_NUM_0, buf, chunk * 2 * sizeof(int16_t), &written_bytes, portMAX_DELAY);
    written_frames += chunk;
  }
  i2s_zero_dma_buffer(I2S_NUM_0);
  i2s_stop(I2S_NUM_0);
  if (snd_amp_ctl) snd_amp_ctl(false);
}

// selectable alert melodies; {0,0} terminates a style early
struct ToneNote { uint16_t freq; uint16_t ms; };
static const ToneNote TONE_STYLES[SOUND_TONE_STYLES][5] = {
  {{880, 90}, {1175, 120}, {0, 0}},                          // Classic
  {{1400, 40}, {1800, 40}, {2200, 60}, {0, 0}},              // Chirp
  {{1319, 140}, {988, 180}, {0, 0}},                         // Ding dong
  {{1568, 45}, {1245, 45}, {1568, 45}, {1245, 90}, {0, 0}},  // Trill
  {{659, 70}, {880, 70}, {1175, 70}, {1568, 110}, {0, 0}},   // Rise
  {{1047, 120}, {523, 120}, {1047, 120}, {0, 0}},            // Alarm
};
static int tone_style = 0;

void soundLoadTonePref() {
  File f = SPIFFS.open("/tone", "r");
  if (f) {
    int v = f.parseInt();
    f.close();
    if (v >= 0 && v < SOUND_TONE_STYLES) tone_style = v;
  }
}

void soundSetToneStyle(int style) {
  if (style < 0 || style >= SOUND_TONE_STYLES) return;
  tone_style = style;
  File f = SPIFFS.open("/tone", "w");
  if (f) {
    f.print(style);
    f.close();
  }
}

int soundGetToneStyle() { return tone_style; }

void soundMessageTone() {
  const ToneNote* n = TONE_STYLES[tone_style];
  for (int i = 0; i < 5 && n[i].freq != 0; i++) soundBeep(n[i].freq, n[i].ms);
}

void soundChannelTone() {
  // softer cousin of the chosen alert: first note, dropped a fourth
  soundBeep(TONE_STYLES[tone_style][0].freq * 3 / 4, 100);
}

void soundAckTone()     { soundBeep(1568, 50); }
