#pragma once

class LoRaFEMControl {
public:
  LoRaFEMControl() {}
  virtual ~LoRaFEMControl() {}

  void init();
  void setSleepModeEnable();
  void setTxModeEnable();
  void setRxModeEnable();
  void setRxModeEnableWhenMCUSleep();
};
