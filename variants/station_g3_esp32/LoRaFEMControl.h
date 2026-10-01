#pragma once

class LoRaFEMControl {
public:
  void init();
  void setSleepModeEnable();
  void setTxModeEnable();
  void setRxModeEnable();
  void setLNAEnable(bool enabled);
  void setPAGainEnable(bool enabled);

  bool canControlLNA() const;
  bool canControlPAGain() const;
  bool isLNAEnabled() const { return lna_enabled; }
  bool isPAGainEnabled() const { return pa_gain_enabled; }

private:
  void applyPAGain();

  bool lna_enabled = true;
  bool pa_gain_enabled = false;
};
