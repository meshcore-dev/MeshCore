#pragma once

#include "CustomLR1121.h"
#include "RadioLibWrappers.h"

class CustomLR1121Wrapper : public RadioLibWrapper {
public:
  CustomLR1121Wrapper(CustomLR1121& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }

  int recvRaw(uint8_t* bytes, int sz) override {
    int len = RadioLibWrapper::recvRaw(bytes, sz);
#if defined(ESP32) && defined(P_LORA_TX_NEOPIXEL_LED)
    if (len > 0) {
      _rx_led_until = millis() + RX_LED_HOLD_MILLIS;
      // Don't override TX white while a transmit is in progress.
      if (!_tx_active) {
        setLedGreen();
      }
    }
#endif
    return len;
  }

  bool startSendRaw(const uint8_t* bytes, int len) override {
    bool ok = RadioLibWrapper::startSendRaw(bytes, len);
#if defined(ESP32) && defined(P_LORA_TX_NEOPIXEL_LED)
    if (ok) {
      _tx_active = true;
      // Keep TX indicator deterministic (white), even if RX pulse was active.
      setLedWhite();
    }
#endif
    return ok;
  }

  void loop() override {
    RadioLibWrapper::loop();
#if defined(ESP32) && defined(P_LORA_TX_NEOPIXEL_LED)
    if (!_tx_active && _rx_led_until != 0 && millis() > _rx_led_until) {
      _rx_led_until = 0;
      setLedOff();
    }
#endif
  }

  bool isReceivingPacket() override {
    return ((CustomLR1121*)_radio)->isReceiving();
  }

  float getCurrentRSSI() override {
    float rssi = -110;
    ((CustomLR1121*)_radio)->getRssiInst(&rssi);
    return rssi;
  }

  void onSendFinished() override {
    RadioLibWrapper::onSendFinished();
    ((CustomLR1121*)_radio)->setPreambleLength(16);
#if defined(ESP32) && defined(P_LORA_TX_NEOPIXEL_LED)
    _tx_active = false;
    if (_rx_led_until != 0 && millis() <= _rx_led_until) {
      setLedGreen();
    } else {
      setLedOff();
    }
#endif
  }

  float getLastRSSI() const override { return ((CustomLR1121*)_radio)->getRSSI(); }
  float getLastSNR() const override { return ((CustomLR1121*)_radio)->getSNR(); }
  int16_t setRxBoostedGainMode(bool en) { return ((CustomLR1121*)_radio)->setRxBoostedGainMode(en); }

private:
#if defined(ESP32) && defined(P_LORA_TX_NEOPIXEL_LED)
  static constexpr uint8_t TX_LED_BRIGHTNESS = 64;
  static constexpr uint8_t RX_LED_BRIGHTNESS = 64;
  static constexpr uint32_t RX_LED_HOLD_MILLIS = 80;
  uint32_t _rx_led_until = 0;
  bool _tx_active = false;

  void setLedOff() {
    neopixelWrite(P_LORA_TX_NEOPIXEL_LED, 0, 0, 0);
  }

  void setLedWhite() {
    neopixelWrite(P_LORA_TX_NEOPIXEL_LED, TX_LED_BRIGHTNESS, TX_LED_BRIGHTNESS, TX_LED_BRIGHTNESS);
  }

  void setLedGreen() {
    neopixelWrite(P_LORA_TX_NEOPIXEL_LED, 0, RX_LED_BRIGHTNESS, 0);
  }
#endif
};
