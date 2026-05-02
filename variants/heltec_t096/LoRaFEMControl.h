#pragma once
#include <stdint.h>

class LoRaFEMControl
{
  public:
    LoRaFEMControl() {}
    virtual ~LoRaFEMControl() {}
    void init(void);
    void setSleepModeEnable(void);
    void setTxModeEnable(void);
    void setRxModeEnable(void);
    void setRxModeEnableWhenMCUSleep(void);
    void setLNAEnable(bool enabled);
    bool isLnaCanControl(void) { return lna_can_control; }
    void setLnaCanControl(bool can_control) { lna_can_control = can_control; }

  private:
    bool lna_enabled = false;
    bool lna_can_control = false;
};
