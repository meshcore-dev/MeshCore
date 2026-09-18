#include "target.h"
#include "MeshnologyW10Hal.h"
#if ENV_INCLUDE_GPS
#include <helpers/sensors/MicroNMEALocationProvider.h>
#endif

MeshnologyW10Board board;
static MeshnologyW10Hal radio_hal(SPI, board.io);
static Module radio_module(&radio_hal, P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY);
static RADIO_CLASS radio(&radio_module);
WRAPPER_CLASS radio_driver(radio, board, board.io);

static ESP32RTCClock fallback_clock;
MeshnologyW10RTC rtc_clock(fallback_clock, Wire);

#if ENV_INCLUDE_GPS
class W10LocationProvider : public MicroNMEALocationProvider {
  bool _enabled = false;
public:
  W10LocationProvider() : MicroNMEALocationProvider(Serial1, &rtc_clock) { }
  void begin() override {
    if (board.io.digitalWrite(MeshnologyW10Board::EXIO_GPS_WAKE, HIGH)) {
      MicroNMEALocationProvider::begin();
      _enabled = true;
    }
  }
  void stop() override {
    if (board.io.digitalWrite(MeshnologyW10Board::EXIO_GPS_WAKE, LOW)) {
      MicroNMEALocationProvider::stop();
      _enabled = false;
    }
  }
  bool isEnabled() override { return _enabled; }
};

static W10LocationProvider nmea;
EnvironmentSensorManager sensors(nmea);
#else
EnvironmentSensorManager sensors;
#endif
#ifdef DISPLAY_CLASS
DISPLAY_CLASS display;
MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif

bool radio_init() {
  if (!board.isReady()) {
    Serial.println("ERROR: W10 radio init aborted: board power/expander setup failed");
    return false;
  }
  fallback_clock.begin();
  if (!rtc_clock.begin()) Serial.println("W10: RTC time unavailable; waiting for GPS/client time");
  uint32_t errors = board.io.getErrorCount();
  bool initialized = radio.std_init();
  if (board.io.getErrorCount() != errors) {
    Serial.println("ERROR: W10 radio init failed: MCP23017 I2C error at 0x20");
    return false;
  }
  return initialized;
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}
