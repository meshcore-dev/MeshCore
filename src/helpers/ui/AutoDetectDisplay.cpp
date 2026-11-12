#include "AutoDetectDisplay.h"
#include <MeshCore.h>

int AutoDetectDisplay::getDisplayStatus(TwoWire& wire, uint8_t addr) {
  int status = -1;
  for(int i = 0; i < 4; i++){

    // request display status
    wire.beginTransmission(addr);
    wire.write((uint8_t)0x00);

    // ensure response received
    if (wire.endTransmission() != 0) return -1; // expect success
    if (wire.requestFrom((int)addr, 1) == 0) return -1; // expect at least 1 byte

    // ensure two consecutive reads are the same
    int previous = status;
    status = wire.read() & 0x0F;
    if(status == previous){
      break;
    }

  }
  return status;
}

bool AutoDetectDisplay::begin() {

  // request status from display address
  int status = getDisplayStatus(Wire, DISPLAY_ADDRESS);
  MESH_DEBUG_PRINTLN("AutoDetectDisplay: status=0x%02X", status);

  // detect SH1106
  if(status == 0x00 || status == 0x08){
    MESH_DEBUG_PRINTLN("AutoDetectDisplay: SH1106 detected");
    _impl = &_sh1106;
    return _sh1106.begin();
  }

  // fallback to SSD1306
  MESH_DEBUG_PRINTLN("AutoDetectDisplay: falling back to SSD1306");
  _impl = &_ssd1306;
  return _ssd1306.begin();

}
