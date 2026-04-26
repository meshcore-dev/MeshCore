#pragma once

#include <MeshCore.h>
#include <Arduino.h>

#ifndef USER_BTN_PRESSED
#define USER_BTN_PRESSED LOW
#endif

#if defined(ESP_PLATFORM)

#include <rom/rtc.h>
#include <sys/time.h>
#include <Wire.h>
#include "soc/rtc.h"
#include "esp_system.h"

class ESP32Board : public mesh::MainBoard {
protected:
  uint8_t startup_reason;
  bool inhibit_sleep = false;
  static inline portMUX_TYPE sleepMux = portMUX_INITIALIZER_UNLOCKED;

public:
  void begin() {
    // for future use, sub-classes SHOULD call this from their begin()
    startup_reason = BD_STARTUP_NORMAL;    

  #ifdef ESP32_CPU_FREQ
    setCpuFrequencyMhz(ESP32_CPU_FREQ);
  #endif

  #ifdef PIN_VBAT_READ
    // battery read support
    pinMode(PIN_VBAT_READ, INPUT);
    adcAttachPin(PIN_VBAT_READ);
  #endif

  #ifdef P_LORA_TX_LED
    pinMode(P_LORA_TX_LED, OUTPUT);
    digitalWrite(P_LORA_TX_LED, LOW);
  #endif

  #if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
   #if PIN_BOARD_SDA >= 0 && PIN_BOARD_SCL >= 0
    Wire.begin(PIN_BOARD_SDA, PIN_BOARD_SCL);
   #endif
  #else
    Wire.begin();
  #endif    
  }

  // Temperature from ESP32 MCU
  float getMCUTemperature() override {
    uint32_t raw = 0;

    // To get and average the temperature so it is more accurate, especially in low temperature
    for (int i = 0; i < 4; i++) {
      raw += temperatureRead();
    }

    return raw / 4;
  }

  uint32_t getIRQGpio() {
    return P_LORA_DIO_1; // default for SX1262
  }

  // 27 mins drift in 28 days and 40 mins = 669 ppm
  const int64_t MICROSECONDS_PER_SECOND_DRIFT = 669;
  const int64_t DRIFT_THRESHOLD_US = 60000000LL; // 1 minute in microseconds

  void applyTimeTrim() {
    static int64_t last_trim_us = 0;
    static int64_t accumulated_drift_us = 0;

    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t now_us = (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec;

    if (last_trim_us == 0) {
      last_trim_us = now_us;
      return;
    }

    // Calculate elapsed time
    int64_t elapsed_us = now_us - last_trim_us;

    // Add this interval's drift to accumulated drift
    accumulated_drift_us += (elapsed_us * MICROSECONDS_PER_SECOND_DRIFT) / 1000000LL;

    // Only trim when accumulated drift exceeds threshold
    if (accumulated_drift_us >= DRIFT_THRESHOLD_US) {
      // Calculate full seconds to trim
      uint32_t seconds_to_trim = (uint32_t)(accumulated_drift_us / 1000000LL);

      // Set back the trimmed time to RTC
      tv.tv_sec -= (time_t)seconds_to_trim;
      settimeofday(&tv, NULL);

      // Keep the fractional remainder (anything less than a full second)
      accumulated_drift_us %= 1000000LL;

      // Save for next trim
      gettimeofday(&tv, NULL);
      last_trim_us = (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec;
    } else {
      // Skip trimming
      last_trim_us = now_us;
    }
  }

  void sleep(uint32_t secs) override {
    // Skip if not allow to sleep
    if (inhibit_sleep) {
      delay(1); // Give MCU to OTA to run
      return;
    }

    // Use more accurate clock in sleep
#if SOC_RTC_SLOW_CLK_SUPPORT_RC_FAST_D256
    if (rtc_clk_slow_src_get() != SOC_RTC_SLOW_CLK_SRC_RC_FAST) {

      // Switch slow clock source to RC_FAST / 256 (~31.25 kHz)
      rtc_clk_slow_src_set(SOC_RTC_SLOW_CLK_SRC_RC_FAST);

      // Calibrate slow clock
      esp_clk_slow_boot_cal(1024);
    }
#endif

    // Keep RTC 8M during sleep
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC8M, ESP_PD_OPTION_ON);

    // Set GPIO wakeup
    gpio_num_t wakeupPin = (gpio_num_t)getIRQGpio();    

    // Configure timer wakeup
    if (secs > 0) {
      esp_sleep_enable_timer_wakeup(secs * 1000000ULL); // Wake up periodically to do scheduled jobs
    }

    // Disable CPU interrupt servicing
    portENTER_CRITICAL(&sleepMux);

    // Skip sleep if there is a LoRa packet
    if (gpio_get_level(wakeupPin) == HIGH) {
      portEXIT_CRITICAL(&sleepMux);
      delay(1);
      return;
    }

    // Configure GPIO wakeup
    esp_sleep_enable_gpio_wakeup();
    gpio_wakeup_enable((gpio_num_t)wakeupPin, GPIO_INTR_HIGH_LEVEL); // Wake up when receiving a LoRa packet

    // MCU enters light sleep
    esp_light_sleep_start();

    // Avoid ISR flood during wakeup due to HIGH LEVEL interrupt
    gpio_wakeup_disable(wakeupPin);
    gpio_set_intr_type(wakeupPin, GPIO_INTR_POSEDGE);

    // Enable CPU interrupt servicing
    portEXIT_CRITICAL(&sleepMux);

    // Apply the software trim to correct for RC drift
    applyTimeTrim();
  }

  uint8_t getStartupReason() const override { return startup_reason; }

#if defined(P_LORA_TX_LED)
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
  }
  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED off
  }
#elif defined(P_LORA_TX_NEOPIXEL_LED)
  #define NEOPIXEL_BRIGHTNESS    64  // white brightness (max 255)

  void onBeforeTransmit() override {
    neopixelWrite(P_LORA_TX_NEOPIXEL_LED, NEOPIXEL_BRIGHTNESS, NEOPIXEL_BRIGHTNESS, NEOPIXEL_BRIGHTNESS);   // turn TX neopixel on (White)
  }
  void onAfterTransmit() override {
    neopixelWrite(P_LORA_TX_NEOPIXEL_LED, 0, 0, 0);   // turn TX neopixel off
  }
#endif

  uint16_t getBattMilliVolts() override {
    #ifdef PIN_VBAT_READ
    analogReadResolution(12);

    uint32_t raw = 0;
    for (int i = 0; i < 4; i++) {
      raw += analogReadMilliVolts(PIN_VBAT_READ);
    }
    raw = raw / 4;

    return (2 * raw);
  #else
    return 0;  // not supported
  #endif
  }

  const char* getManufacturerName() const override {
    return "Generic ESP32";
  }

  void reboot() override {
    esp_restart();
  }

  bool startOTAUpdate(const char* id, char reply[]) override;

  void setInhibitSleep(bool inhibit) {
    inhibit_sleep = inhibit;
  }

  uint32_t getResetReason() const override {
    return esp_reset_reason();
  }

  // https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/system/system.html
  const char *getResetReasonString(uint32_t reason) {
    switch (reason) {
    case ESP_RST_UNKNOWN:
      return "Unknown or first boot";
    case ESP_RST_POWERON:
      return "Power-on reset";
    case ESP_RST_EXT:
      return "External reset";
    case ESP_RST_SW:
      return "Software reset";
    case ESP_RST_PANIC:
      return "Panic / exception reset";
    case ESP_RST_INT_WDT:
      return "Interrupt watchdog reset";
    case ESP_RST_TASK_WDT:
      return "Task watchdog reset";
    case ESP_RST_WDT:
      return "Other watchdog reset";
    case ESP_RST_DEEPSLEEP:
      return "Wake from deep sleep";
    case ESP_RST_BROWNOUT:
      return "Brownout (low voltage)";
    case ESP_RST_SDIO:
      return "SDIO reset";
    default:
      static char buf[40];
      snprintf(buf, sizeof(buf), "Unknown reset reason (%d)", reason);
      return buf;
    }
  }
};

static RTC_NOINIT_ATTR uint32_t _rtc_backup_time;
static RTC_NOINIT_ATTR uint32_t _rtc_backup_magic;
#define RTC_BACKUP_MAGIC  0xAA55CC33
#define RTC_TIME_MIN      1772323200  // 1 Mar 2026

class ESP32RTCClock : public mesh::RTCClock {
public:
  ESP32RTCClock() { }
  void begin() {
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_DEEPSLEEP) {
      return;  // ESP-IDF preserves system time across deep sleep
    }
    // All other resets (power-on, crash, WDT, brownout) lose system time.
    // Restore from RTC backup if valid, otherwise use hardcoded seed.
    struct timeval tv;
    if (_rtc_backup_magic == RTC_BACKUP_MAGIC && _rtc_backup_time > RTC_TIME_MIN) {
      tv.tv_sec = _rtc_backup_time;
    } else {
      tv.tv_sec = 1772323200;  // 1 Mar 2026
    }
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
  }
  uint32_t getCurrentTime() override {
    time_t _now;
    time(&_now);
    return _now;
  }
  void setCurrentTime(uint32_t time) override {
    struct timeval tv;
    tv.tv_sec = time;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
    _rtc_backup_time = time;
    _rtc_backup_magic = RTC_BACKUP_MAGIC;
  }
  void tick() override {
    time_t now;
    time(&now);
    if (now > RTC_TIME_MIN && (uint32_t)now != _rtc_backup_time) {
      _rtc_backup_time = (uint32_t)now;
      _rtc_backup_magic = RTC_BACKUP_MAGIC;
    }
  }
};

#endif
