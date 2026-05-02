#include <Arduino.h>
#include "target.h"
#include <driver/gpio.h>

// Recover a stuck I2C bus by manually clocking SCL up to 9 times until
// SDA is released, then issuing a STOP condition.
static void i2c_bus_recovery() {
  Serial.println("[i2c_recover] Attempting I2C bus recovery...");
  pinMode(PIN_BOARD_SCL, OUTPUT_OPEN_DRAIN);
  pinMode(PIN_BOARD_SDA, INPUT_PULLUP);
  digitalWrite(PIN_BOARD_SCL, HIGH);
  delayMicroseconds(10);

  bool released = false;
  for (int i = 0; i < 9; i++) {
    digitalWrite(PIN_BOARD_SCL, LOW);
    delayMicroseconds(10);
    digitalWrite(PIN_BOARD_SCL, HIGH);
    delayMicroseconds(10);
    if (digitalRead(PIN_BOARD_SDA) == HIGH) {
      released = true;
      Serial.printf("[i2c_recover] SDA released after %d clocks\n", i + 1);
      break;
    }
  }
  if (!released) {
    Serial.println("[i2c_recover] SDA still LOW after 9 clocks!");
  }

  // Generate STOP condition: SDA LOW→HIGH while SCL HIGH
  pinMode(PIN_BOARD_SDA, OUTPUT_OPEN_DRAIN);
  digitalWrite(PIN_BOARD_SDA, LOW);
  delayMicroseconds(10);
  digitalWrite(PIN_BOARD_SCL, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_BOARD_SDA, HIGH);
  delayMicroseconds(10);

  // Restore pins to input so Wire can take over
  pinMode(PIN_BOARD_SCL, INPUT_PULLUP);
  pinMode(PIN_BOARD_SDA, INPUT_PULLUP);
  delay(5);
  Serial.println("[i2c_recover] Done.");
}

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

// Custom RadioLib HAL: routes expander pins through TCA9535 (I2C 0x20).
// Uses software (bit-bang) SPI on SCLK/MOSI/MISO to avoid taking the
// hardware FSPI peripheral, which would disrupt LovyanGFX's LCD_CAM output.
// SPI is passed as a placeholder only — all SPI methods are overridden.
static SenseCapHAL radio_hal(SPI, LORA_SCLK, LORA_MISO, LORA_MOSI, 0x20, &Wire);

// SX1262 module using the custom HAL
RADIO_CLASS radio = new Module(&radio_hal, LORA_NSS, LORA_DIO1, LORA_RESET, LORA_BUSY);

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock    fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

EnvironmentSensorManager sensors;

// Shared Wire mutex — created in radio_init(), exported via target.h
SemaphoreHandle_t g_i2c_mutex = nullptr;

// ── radio_init ─────────────────────────────────────────────────────────

bool radio_init() {
  // Create the shared Wire mutex FIRST so both the radio HAL and the
  // LVGL touch callback (my_touchpad_read) serialise their Wire access.
  g_i2c_mutex = xSemaphoreCreateMutex();
  radio_hal.setMutex(g_i2c_mutex);

  Serial.println("[radio_init] Starting...");
  Serial.printf("[radio_init] SPI  : SCLK=%d  MISO=%d  MOSI=%d\n",
                LORA_SCLK, LORA_MISO, LORA_MOSI);
  Serial.printf("[radio_init] Expander (0x20): NSS=pin%d  RST=pin%d  BUSY=pin%d  DIO1=pin%d\n",
                LORA_NSS & 0x0F, LORA_RESET & 0x0F, LORA_BUSY & 0x0F, LORA_DIO1 & 0x0F);
  Serial.printf("[radio_init] DIO1 interrupt → GPIO %d\n", IO_EXPANDER_IRQ);

  // Take mutex for bus recovery + Wire re-init + scan
  // (prevents LVGL touch task from accessing Wire concurrently)
  xSemaphoreTake(g_i2c_mutex, portMAX_DELAY);

  // Recover stuck I2C bus (may be left in bad state after display init)
  i2c_bus_recovery();

  // Wire must be up before TCA9535 access — force full re-init after recovery
  Wire.end();
  delay(5);
  Wire.begin(PIN_BOARD_SDA, PIN_BOARD_SCL, 400000);
  Wire.setTimeOut(15);  // 15 ms per address for fast scan
  delay(10);

  // ── Full I2C bus scan ───────────────────────────────────────────────────
  Serial.println("[radio_init] I2C scan:");
  uint8_t found_addr = 0;
  for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      Serial.printf("[radio_init]   Device found at 0x%02X\n", addr);
      found_addr = addr;
    }
  }
  if (found_addr == 0) {
    Serial.println("[radio_init]   No I2C devices found!");
  }
  Serial.println("[radio_init] I2C scan done.");

  xSemaphoreGive(g_i2c_mutex);

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
