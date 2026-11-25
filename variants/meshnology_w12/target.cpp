#include <Arduino.h>
#include "target.h"

MeshnologyW12Board board;

#if defined(P_LORA_SCLK)
  static SPIClass spi;
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);
#else
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY);
#endif

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

#if ENV_INCLUDE_GPS
  #include <helpers/sensors/MicroNMEALocationProvider.h>
  MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock);
  EnvironmentSensorManager sensors = EnvironmentSensorManager(nmea);
#else
  EnvironmentSensorManager sensors;
#endif

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display(NULL);
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif

const uint32_t rfswitch_dios[] = {
  RADIOLIB_LR2021_DIO5,   // RFX2402E 2.4G_TX_EN
  RADIOLIB_LR2021_DIO6,   // RFX2402E 2.4G_RX_EN
  RADIOLIB_LR2021_DIO9,   // GC1109 CTX (Transmit mode)
  RADIOLIB_LR2021_DIO10,  // GC1109 CPS (Bypass mode)
  RADIOLIB_LR2021_DIO11,  // GC1109 CSD (Shutdown)
};

static const Module::RfSwitchMode_t rfswitch_table[] = {
  //                    DIO5   DIO6  DIO9  DIO10  DIO11
  { LR2021::MODE_STBY,  { LOW,  LOW,  LOW,  LOW,  LOW  } },  // everything off
  { LR2021::MODE_RX,    { LOW,  LOW,  LOW,  LOW,  HIGH } },  // rx_lf: GC1109 LNA
  { LR2021::MODE_TX,    { LOW,  LOW,  HIGH, HIGH, HIGH } },  // tx_lf: GC1109 full PA
  { LR2021::MODE_RX_HF, { LOW,  HIGH, LOW,  LOW,  LOW  } },  // rx_hf: 2G4 LNA, GC1109 off
  { LR2021::MODE_TX_HF, { HIGH, LOW,  LOW,  LOW,  LOW  } },  // tx_hf: 2G4 PA,  GC1109 off
  END_OF_MODE_TABLE,
};


bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);

#if defined(P_LORA_SCLK)
  int err = radio.std_init(&spi);
  if (err != 1) return err;
#else
  int err = radio.std_init();
  if (err != 1) return err;
#endif

#ifdef RF_SWITCH_TABLE 
  radio.setRfSwitchTable(rfswitch_dios, rfswitch_table);
#endif

  return true;
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}

