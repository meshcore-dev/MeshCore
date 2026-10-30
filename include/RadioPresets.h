#pragma once

// Thailand, 920-922 MHz. 921.000 MHz is the center of that window and an
// exact SX126x/SX127x PLL frequency. Thailand (Narrow) is the build default:
// a 102-byte advert is about 0.398 s on air.

#define THAILAND_NARROW_FREQ 921.000f
#define THAILAND_NARROW_BW   62.5f
#define THAILAND_NARROW_SF   7
#define THAILAND_NARROW_CR   5

#define THAILAND_FREQ 921.000f
#define THAILAND_BW   125.0f
#define THAILAND_SF   8
#define THAILAND_CR   6

struct RadioPreset {
  const char* title;
  float frequency_mhz;
  float bandwidth_khz;
  uint8_t spreading_factor;
  uint8_t coding_rate;
};

static constexpr RadioPreset RADIO_PRESETS[] = {
  { "Thailand (Narrow)", THAILAND_NARROW_FREQ, THAILAND_NARROW_BW, THAILAND_NARROW_SF, THAILAND_NARROW_CR },
  { "Thailand",          THAILAND_FREQ,        THAILAND_BW,        THAILAND_SF,        THAILAND_CR },
};

#ifndef LORA_FREQ
#define LORA_FREQ THAILAND_NARROW_FREQ
#endif
#ifndef LORA_BW
#define LORA_BW THAILAND_NARROW_BW
#endif
#ifndef LORA_SF
#define LORA_SF THAILAND_NARROW_SF
#endif
#ifndef LORA_CR
#define LORA_CR THAILAND_NARROW_CR
#endif
