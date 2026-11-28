#pragma once

#include <helpers/ui/DisplayDriver.h>

struct PowerBankSnapshot;

class PowerBankPage {
public:
  void invalidate() { painted = false; }
  void draw(DisplayDriver& display, const PowerBankSnapshot& status, bool usbDataEnabled);

private:
  struct UsbPort {
    uint8_t state = 0;
    char protocol[16]{};
  };

  void drawBattery(DisplayDriver& d, const PowerBankSnapshot& status);
  void drawPower(DisplayDriver& d, const PowerBankSnapshot& status, bool live);
  void drawUsb(DisplayDriver& d, int x, UsbPort& last, uint8_t state, uint8_t mode, bool shared, bool force = false);
  void drawUsbDataMode(DisplayDriver& d, int x);
  void drawWireless(DisplayDriver& d, const PowerBankSnapshot& status, bool live);

  bool painted = false;
  int percent = -1;
  int32_t temperature = 0;
  int32_t wattsX10 = -1;
  bool boltActive = false;
  bool usbDataMode = false;
  UsbPort c1, c2;
  const char* qiState = "";
  int fod = -1;
  char qiProtocol[16]{};
};
