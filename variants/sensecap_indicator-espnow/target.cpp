#include <Arduino.h>
#include "target.h"

ESP32Board board;

// ── SX1262 SPI data pins (hardware FSPI / SPI2) ────────────────────────
// Shared with the ST7701 display config SPI (bit-banged by LovyanGFX only
// during lcd.begin(), so no runtime conflict).
#define LORA_SCLK  41
#define LORA_MISO  47
#define LORA_MOSI  48

// ── SX1262 control pins via TCA9535 I2C IO Expander (7-bit addr 0x20) ──
// Encoded as (pin_index | IO_EXPANDER) per LovyanGFX / Seeed convention.
// IO_EXPANDER = 0x40  and  IO_EXPANDER_IRQ = 42  defined in platformio.ini
#define LORA_NSS    (0 | IO_EXPANDER)   // TCA9535 port-0 pin 0  (0x40)
#define LORA_RESET  (1 | IO_EXPANDER)   // TCA9535 port-0 pin 1  (0x41)
#define LORA_BUSY   (2 | IO_EXPANDER)   // TCA9535 port-0 pin 2  (0x42)
#define LORA_DIO1   (3 | IO_EXPANDER)   // TCA9535 port-0 pin 3  (0x43)
                                        // → interrupt fires on GPIO 42

static SPIClass spi(FSPI);

// Custom RadioLib HAL: routes expander pins through TCA9535 (I2C 0x20)
// and inits SPI with the correct pins on first use.
static SenseCapHAL radio_hal(spi, LORA_SCLK, LORA_MISO, LORA_MOSI, 0x20, &Wire);

// SX1262 module using the custom HAL
RADIO_CLASS radio = new Module(&radio_hal, LORA_NSS, LORA_DIO1, LORA_RESET, LORA_BUSY);

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock    fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

EnvironmentSensorManager sensors;

// ── radio_init ─────────────────────────────────────────────────────────

bool radio_init() {
  Serial.println("[radio_init] Starting...");
  Serial.printf("[radio_init] SPI  : SCLK=%d  MISO=%d  MOSI=%d\n",
                LORA_SCLK, LORA_MISO, LORA_MOSI);
  Serial.printf("[radio_init] Expander (0x20): NSS=pin%d  RST=pin%d  BUSY=pin%d  DIO1=pin%d\n",
                LORA_NSS & 0x0F, LORA_RESET & 0x0F, LORA_BUSY & 0x0F, LORA_DIO1 & 0x0F);
  Serial.printf("[radio_init] DIO1 interrupt → GPIO %d\n", IO_EXPANDER_IRQ);

  // Wire must be up before TCA9535 access.
  // (lcd.begin() may have already called Wire.begin, but re-init is safe.)
  Wire.begin(PIN_BOARD_SDA, PIN_BOARD_SCL);

  // Verify TCA9535 is present on the bus
  if (radio_hal.scanExpander()) {
    Serial.println("[radio_init] TCA9535 FOUND at 0x20");
  } else {
    Serial.println("[radio_init] WARNING: TCA9535 NOT FOUND at 0x20 – check I2C wiring");
  }

  // Set TCA9535 Port 0 defaults: all outputs HIGH (NSS/RESET de-asserted)
  radio_hal.initExpander();

  fallback_clock.begin();
  Serial.println("[radio_init] RTC initialized");

  // std_init() calls radio.begin() internally, which calls our spiBegin()
  // to initialise the FSPI bus with pins 41/47/48, then communicates with
  // the SX1262 using our expander-aware digitalWrite/digitalRead.
  bool ok = radio.std_init();
  Serial.printf("[radio_init] std_init result: %s\n", ok ? "OK" : "FAILED");
  return ok;
}

uint32_t radio_get_rng_seed() {
  return radio.random(0x7FFFFFFF);
}

void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr) {
  radio.setFrequency(freq);
  radio.setSpreadingFactor(sf);
  radio.setBandwidth(bw);
  radio.setCodingRate(cr);
}

void radio_set_tx_power(uint8_t dbm) {
  radio.setOutputPower(dbm);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}
