#include <Arduino.h>
#include <RTClib.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>

RAK3401Board board;

#ifndef PIN_USER_BTN
  #define PIN_USER_BTN (-1)
#endif

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true, true);

  #if defined(PIN_USER_BTN_ANA)
  MomentaryButton analog_btn(PIN_USER_BTN_ANA, 1000, 20);
  #endif
#endif

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);

WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

#if ENV_INCLUDE_GPS
  #include <helpers/sensors/MicroNMEALocationProvider.h>

  // RAK3401 1W Booster GPS builds use the RAK12501 / Quectel L76K by default.
  // This config is deliberately local to the RAK3401 target so the shared GPS
  // manager does not learn another board-specific L76K init path.
  #ifndef RAK12501_L76K_GPS
    #define RAK12501_L76K_GPS 1
  #endif

  #if defined(RAK12501_L76K_GPS)
    #ifndef RAK12501_L76K_NAV_MODE
      #define RAK12501_L76K_NAV_MODE 3
    #endif

    #ifndef RAK12501_L76K_BOOT_DELAY_MS
      #define RAK12501_L76K_BOOT_DELAY_MS 1000
    #endif

    static void rak12501SendNMEACommand(Stream& serial, const char* body) {
      uint8_t checksum = 0;
      for (const char* p = body; *p; ++p) {
        checksum ^= (uint8_t)*p;
      }

      serial.print('$');
      serial.print(body);
      serial.print('*');
      if (checksum < 0x10) {
        serial.print('0');
      }
      serial.print(checksum, HEX);
      serial.print("\r\n");
    }

    static void rak12501InjectKnownTime() {
      uint32_t now = rtc_clock.getCurrentTime();

      // Only inject if the board clock is plausible. Avoid feeding the GPS the
      // zero/default RTC value on first boot. 2020-01-01 is a conservative floor.
      if (now < 1577836800UL) {
        return;
      }

      DateTime dt(now);
      char body[40];
      snprintf(body, sizeof(body), "PMTK740,%04u,%02u,%02u,%02u,%02u,%02u",
               dt.year(), dt.month(), dt.day(), dt.hour(), dt.minute(), dt.second());
      rak12501SendNMEACommand(Serial1, body);
      delay(250);
    }

    static void rak12501ConfigureL76K() {
      MESH_DEBUG_PRINTLN("Configuring RAK12501/L76K GPS");

      // Best-effort MTK aiding: if RTC has a known time, provide UTC time before
      // the main config. Location/almanac injection is intentionally not included;
      // it needs a fresh/accurate location source or downloadable EPO/almanac data.
      rak12501InjectKnownTime();

      // Meshtastic-style L76K init:
      //   GPS + GLONASS + BeiDou
      //   NMEA RMC + GGA only to reduce serial traffic for the companion app
      //   Vehicle mode by default ($PCAS11,3)
      rak12501SendNMEACommand(Serial1, "PCAS04,7");
      delay(250);
      rak12501SendNMEACommand(Serial1, "PCAS03,1,0,0,0,1,0,0,0,0,0,,,0,0");
      delay(250);

      char nav_mode[16];
      uint8_t mode = (RAK12501_L76K_NAV_MODE <= 7) ? RAK12501_L76K_NAV_MODE : 3;
      snprintf(nav_mode, sizeof(nav_mode), "PCAS11,%u", mode);
      rak12501SendNMEACommand(Serial1, nav_mode);
      delay(250);
    }

    class RAK12501L76KLocationProvider : public MicroNMEALocationProvider {
    public:
      using MicroNMEALocationProvider::MicroNMEALocationProvider;

      void reset() override {
        MicroNMEALocationProvider::reset();
        delay(RAK12501_L76K_BOOT_DELAY_MS);
        rak12501ConfigureL76K();
      }
    };

    RAK12501L76KLocationProvider nmea = RAK12501L76KLocationProvider(Serial1, &rtc_clock);
  #else
    MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock);
  #endif

  EnvironmentSensorManager sensors = EnvironmentSensorManager(nmea);
#else
  EnvironmentSensorManager sensors;
#endif

bool radio_init() {
  rtc_clock.begin(Wire);
  return radio.std_init(&SPI);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}
