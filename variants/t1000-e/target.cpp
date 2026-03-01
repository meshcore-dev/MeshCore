#include <Arduino.h>
#include "t1000e_sensors.h"
#include "target.h"
#include <helpers/sensors/MicroNMEALocationProvider.h>

class T1000eLocationProvider : public MicroNMEALocationProvider {
    bool _configured = false;

public:
    T1000eLocationProvider(HardwareSerial& ser, mesh::RTCClock* clock)
        : MicroNMEALocationProvider(ser, clock) { }

    void begin() override {
        enable();

        if (!_configured) {
            // First start: set up T1000-E specific control pins
            MESH_DEBUG_PRINTLN("GPS(T1000-E): begin (first start, GPIO init)");
            pinMode(GPS_VRTC_EN, OUTPUT);
            digitalWrite(GPS_VRTC_EN, HIGH);
            delay(10);

            pinMode(GPS_SLEEP_INT, OUTPUT);
            digitalWrite(GPS_SLEEP_INT, HIGH);  // not sleeping
            pinMode(GPS_RTC_INT, OUTPUT);
            digitalWrite(GPS_RTC_INT, LOW);
            pinMode(GPS_RESETB, INPUT_PULLUP);

            // Reset and wait for boot
            reset();
            delay(1000);
            while (_gps_serial->available()) { // drain boot messages
                char c = _gps_serial->read();
            }
            configure();
            _configured = true;
        } else {
            // Wake from backup mode via RTC_INT pulse
            MESH_DEBUG_PRINTLN("GPS(T1000-E): begin (wake from backup)");
            digitalWrite(GPS_RTC_INT, HIGH);
            delay(3);
            digitalWrite(GPS_RTC_INT, LOW);
            delay(100);
        }

        // Lock sleep to keep module active during scan
        MESH_DEBUG_PRINTLN("GPS(T1000-E): locking sleep");
        sendSentence("$PAIR382,1");
        _active = true;
    }

    void stop() override {
        MESH_DEBUG_PRINTLN("GPS(T1000-E): stop, entering backup mode");
        // Unlock sleep + enter backup mode (VRTC keeps RTC alive for warm start)
        sendSentence("$PAIR382,0");
        sendSentence("$PAIR650,0");
        delay(50);
        _active = false;
        disable();
    }

    void configure() override {
        MESH_DEBUG_PRINTLN("GPS(T1000-E): configure AG3335");
        // Enable GPS+GLONASS+Galileo+BDS
        sendSentence("$PAIR066,1,1,1,1,0,0");
        // NMEA sentence filter: only GGA and RMC
        sendSentence("$PAIR062,0,1");  // GGA ON
        sendSentence("$PAIR062,1,0");  // GLL OFF
        sendSentence("$PAIR062,2,0");  // GSA OFF
        sendSentence("$PAIR062,3,0");  // GSV OFF
        sendSentence("$PAIR062,4,1");  // RMC ON
        sendSentence("$PAIR062,5,0");  // VTG OFF
        sendSentence("$PAIR062,6,0");  // ZDA OFF
        // Save config to flash
        delay(250);
        sendSentence("$PAIR513");
    }
};

T1000eBoard board;

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);

WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock rtc_clock;
T1000eLocationProvider nmea(Serial1, &rtc_clock);
T1000SensorManager sensors(nmea);

#ifdef DISPLAY_CLASS
  NullDisplayDriver display;
#endif

#ifndef LORA_CR
  #define LORA_CR      5
#endif

#ifdef RF_SWITCH_TABLE
static const uint32_t rfswitch_dios[Module::RFSWITCH_MAX_PINS] = {
  RADIOLIB_LR11X0_DIO5,
  RADIOLIB_LR11X0_DIO6,
  RADIOLIB_LR11X0_DIO7,
  RADIOLIB_LR11X0_DIO8,
  RADIOLIB_NC
};

static const Module::RfSwitchMode_t rfswitch_table[] = {
  // mode                 DIO5  DIO6  DIO7  DIO8
  { LR11x0::MODE_STBY,   {LOW,  LOW,  LOW,  LOW  }},
  { LR11x0::MODE_RX,     {HIGH, LOW,  LOW,  HIGH }},
  { LR11x0::MODE_TX,     {HIGH, HIGH, LOW,  HIGH }},
  { LR11x0::MODE_TX_HP,  {LOW,  HIGH, LOW,  HIGH }},
  { LR11x0::MODE_TX_HF,  {LOW,  LOW,  LOW,  LOW  }},
  { LR11x0::MODE_GNSS,   {LOW,  LOW,  HIGH, LOW  }},
  { LR11x0::MODE_WIFI,   {LOW,  LOW,  LOW,  LOW  }},
  END_OF_MODE_TABLE,
};
#endif

bool radio_init() {
  
#ifdef LR11X0_DIO3_TCXO_VOLTAGE
  float tcxo = LR11X0_DIO3_TCXO_VOLTAGE;
#else
  float tcxo = 1.6f;
#endif

  SPI.setPins(P_LORA_MISO, P_LORA_SCLK, P_LORA_MOSI);
  SPI.begin();
  int status = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR, RADIOLIB_LR11X0_LORA_SYNC_WORD_PRIVATE, LORA_TX_POWER, 16, tcxo);
  if (status != RADIOLIB_ERR_NONE) {
    Serial.print("ERROR: radio init failed: ");
    Serial.println(status);
    return false;  // fail
  }

  radio.setCRC(2);
  radio.explicitHeader();

#ifdef RF_SWITCH_TABLE
  radio.setRfSwitchTable(rfswitch_dios, rfswitch_table);
#endif
#ifdef RX_BOOSTED_GAIN
  radio.setRxBoostedGainMode(RX_BOOSTED_GAIN);
#endif

  return true;  // success
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

void radio_set_tx_power(int8_t dbm) {
  radio.setOutputPower(dbm);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}

bool T1000SensorManager::querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) {
  SensorManager::querySensors(requester_permissions, telemetry);
  if (requester_permissions & TELEM_PERM_ENVIRONMENT) {
    // Firmware reports light as a 0-100 % scale, but expose it via Luminosity so app labels it "Luminosity".
    telemetry.addLuminosity(TELEM_CHANNEL_SELF, t1000e_get_light());
    telemetry.addTemperature(TELEM_CHANNEL_SELF, t1000e_get_temperature());
  }
  return true;
}
