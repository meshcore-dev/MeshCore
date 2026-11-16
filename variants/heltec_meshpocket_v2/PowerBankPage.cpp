#include "PowerBankPage.h"
#include "PowerBank.h"

namespace {
constexpr ColorVal background = 0x0063;
constexpr ColorVal blue = 0x653d;
constexpr ColorVal purple = 0x611f;
constexpr ColorVal card = 0xdefc;
constexpr ColorVal dark = 0x31c8;
constexpr ColorVal white = 0xffff;
constexpr ColorVal green = 0x06ae;
constexpr ColorVal gold = 0xff0e;
constexpr ColorVal red = 0xfa69;
constexpr ColorVal off = 0x3a6b;
constexpr ColorVal well = 0x1127, shine = 0x47d5, track = 0x94b8;
constexpr ColorVal badgeFace = 0xffbf, tempFace = 0xf776, tempInk = 0xa465;
constexpr ColorVal divider = 0x9515, lightDivider = 0xd71e, boltInk = 0x93ba;
constexpr int temperatureCenterX = 134;

void label(DisplayDriver& d, int x, int y, const char* value, ColorVal ink, int size = 1) {
  d.setColor(ink);
  d.setTextSize(size);
  d.setCursor(x, y);
  d.print(value);
}

void panel(DisplayDriver& d, int x, int y, int w, int h, ColorVal color, int r = 6) {
  d.setColor(color);
  if (h > 2 * r) d.fillRect(x, y + r, w, h - 2 * r);
  int span = 0;
  for (int row = 0; row < r; ++row) {
    int dy = r - row;
    while ((span + 1) * (span + 1) + dy * dy <= r * (r + 1)) ++span;
    int inset = r - span;
    d.fillRect(x + inset, y + row, w - 2 * inset, 1);
    d.fillRect(x + inset, y + h - 1 - row, w - 2 * inset, 1);
  }
}

void drawPortCard(DisplayDriver& d, int x, const char* name) {
  panel(d, x, 72, 70, 54, card);
  label(d, x + 5, 78, name, dark, 2);
  d.setColor(divider);
  d.fillRect(x + 5, 97, 60, 1);
}

void drawUsbConnector(DisplayDriver& d, int x, ColorVal ink) {
  panel(d, x + 33, 82, 25, 10, ink, 4);
  panel(d, x + 34, 83, 23, 8, card, 3);
  d.setColor(ink);
  d.fillRect(x + 38, 87, 15, 1);
}

void drawUsbCard(DisplayDriver& d, int x, const char* name) {
  drawPortCard(d, x, name);
  drawUsbConnector(d, x, dark);
}

void drawLayout(DisplayDriver& d) {
  d.startFrame(background);
  panel(d, 2, 2, 164, 66, blue);
  label(d, 8, 10, "Battery", white);
  panel(d, 89, 23, 4, 9, white, 1);
  panel(d, 49, 16, 40, 23, white, 4);
  panel(d, 50, 17, 38, 21, well, 3);
  d.setColor(white);
  d.fillRect(8, 50, 85, 8);
  d.setColor(track);
  d.fillRect(9, 51, 83, 6);
  d.setColor(white);
  for (int i = 1; i < 10; ++i) d.fillRect(9 + i * 83 / 10, 51, 1, 6);
  d.setColor(lightDivider);
  d.fillRect(8, 44, 85, 1);
  d.fillRect(101, 9, 1, 51);
  panel(d, temperatureCenterX - 11, 13, 23, 23, badgeFace, 11);
  panel(d, temperatureCenterX - 8, 16, 17, 17, tempFace, 8);
  label(d, temperatureCenterX - 2, 21, "T", tempInk);
  panel(d, 169, 2, 49, 66, purple);
  panel(d, 182, 13, 23, 23, badgeFace, 11);
  drawUsbCard(d, 2, "C1");
  drawUsbCard(d, 75, "C2");
  drawPortCard(d, 148, "Qi2");
  label(d, 154, 103, "FOD:", dark);
}

bool connected(uint8_t port) {
  return port == HLT331Serial::PORT_CHARGING || port == HLT331Serial::PORT_DISCHARGING;
}

// Write only the changed span inside each segment, skipping the white dividers.
void updateBar(DisplayDriver& d, int x, int y, int w, int h, int before, int after,
               ColorVal filled, ColorVal empty, int segments = 1) {
  int oldWidth = before > 0 ? (w * before + 50) / 100 : 0;
  int newWidth = after > 0 ? (w * after + 50) / 100 : 0;
  if (oldWidth == newWidth) return;
  d.setColor(newWidth > oldWidth ? filled : empty);
  for (int i = 0; i < segments; ++i) {
    int left = max(min(oldWidth, newWidth), i * w / segments + (i != 0));
    int right = min(max(oldWidth, newWidth), (i + 1) * w / segments);
    if (left < right) d.fillRect(x + left, y, right - left, h);
  }
}

void clearField(DisplayDriver& d, int x, int y, int w, int h, ColorVal color) {
  d.setColor(color);
  d.fillRect(x, y, w, h);
}

void drawTemperature(DisplayDriver& d, const char* value) {
  const bool hasPoint = strchr(value, '.') != nullptr;
  const int glyphs = strlen(value) - hasPoint;
  const int size = glyphs <= 3 ? 2 : 1;  // Leave room for the unit on longer readings.
  const int advance = 5 * size + 1;
  const int width = glyphs * advance + (hasPoint ? 3 : 0);
  int x = temperatureCenterX - (width - 1) / 2;

  clearField(d, 102, 43, 64, 14, blue);
  d.setColor(white);
  d.setTextSize(size);
  for (const char* p = value; *p; ++p) {
    if (*p == '.') {
      d.fillRect(x, 55, 2, 2);
      x += 3;
    } else {
      char glyph[] = {*p, '\0'};
      d.setCursor(x, 57 - 7 * size);
      d.print(glyph);
      x += advance;
    }
  }
  d.drawRect(x + 1, 49, 3, 3);
  label(d, x + 5, 50, "C", white);
}

void drawPowerRays(DisplayDriver& d, ColorVal ink) {
  d.setColor(ink);
  d.fillRect(193, 7, 1, 4);
  d.fillRect(193, 38, 1, 4);
  d.fillRect(176, 24, 4, 1);
  d.fillRect(207, 24, 4, 1);
  for (int offset = 10; offset <= 12; ++offset) {
    d.fillRect(193 - offset, 24 - offset, 1, 1);
    d.fillRect(193 + offset, 24 - offset, 1, 1);
    d.fillRect(193 - offset, 24 + offset, 1, 1);
    d.fillRect(193 + offset, 24 + offset, 1, 1);
  }
}

void drawWatts(DisplayDriver& d, int32_t wattsX10) {
  char value[16] = "--";
  if (wattsX10 >= 0) {
    snprintf(value, sizeof(value), "%ld.%ld", long(wattsX10 / 10), long(wattsX10 % 10));
  }
  clearField(d, 170, 43, 47, 14, purple);
  if (wattsX10 >= 1000) {  // Three integer digits only fit in the small font.
    d.setTextSize(1);
    int digits = d.getTextWidth(value);
    int x = 193 - (digits + 7) / 2;
    label(d, x, 50, value, white);
    label(d, x + digits + 1, 50, "W", white);
    return;
  }
  // Budget 11 pixels per digit, 3 for the decimal point and 7 for the unit.
  int width = 7;
  for (const char* p = value; *p; ++p) width += (*p == '.') ? 3 : 11;
  int x = 193 - width / 2;
  d.setColor(white);
  d.setTextSize(2);
  for (const char* p = value; *p; ++p) {
    if (*p == '.') {
      d.fillRect(x, 55, 2, 2);
      x += 3;
    } else {
      char digit[] = {*p, '\0'};
      d.setCursor(x, 43);
      d.print(digit);
      x += 11;
    }
  }
  label(d, x + 1, 50, "W", white);
}
}

void PowerBankPage::drawUsb(DisplayDriver& d, int x, UsbPort& last, uint8_t state, uint8_t mode, bool shared, bool force) {
  if (!painted || force || state != last.state) {
    const char* name = "--";
    ColorVal badge = off;
    switch (state) {
      case HLT331Serial::PORT_NOT_CONNECTED: name = "OFF"; break;
      case HLT331Serial::PORT_CHARGING: name = "CHG"; badge = green; break;
      case HLT331Serial::PORT_DISCHARGING: name = "DCHG"; badge = gold; break;
    }
    clearField(d, x + 5, 102, 60, 10, card);
    panel(d, x + 6, 102, 28, 10, badge, 5);
    d.setTextSize(1);
    label(d, x + 21 - d.getTextWidth(name) / 2, 104, name, badge == off ? white : dark);
    last.state = state;
  }
  char protocol[sizeof(last.protocol)] = "";
  if (connected(state) && mode != HLT331Serial::FAST_CHARGE_NONE) {
    snprintf(protocol, sizeof(protocol), "%s%s", HLT331Serial::fastChargeName(mode), shared ? "*" : "");
  }
  if (!painted || force || strcmp(last.protocol, protocol) != 0) {
    clearField(d, x + 6, 116, 63, 7, card);
    label(d, x + 6, 116, protocol, dark);
    strcpy(last.protocol, protocol);
  }
}

void PowerBankPage::drawUsbDataMode(DisplayDriver& d, int x) {
  drawUsbConnector(d, x, blue);

  clearField(d, x + 5, 102, 60, 10, card);
  panel(d, x + 21, 102, 28, 10, blue, 5);
  d.setTextSize(1);
  const char* badge = "USB";
  label(d, x + 35 - d.getTextWidth(badge) / 2, 104, badge, white);

  clearField(d, x + 6, 116, 63, 7, card);
  const char* blocked = "CHG/DCHG";
  const int width = d.getTextWidth(blocked);
  const int textX = x + 35 - width / 2;
  label(d, textX, 116, blocked, dark);
  d.setColor(red);
  const int dischargeX = textX + d.getTextWidth("CHG/");
  d.fillRect(dischargeX, 119, d.getTextWidth("DCHG"), 1);
}

void PowerBankPage::drawBattery(DisplayDriver& d, const PowerBankSnapshot& status) {
  const auto& cw = status.cw;
  const int nextPercent = cw.ready ? min(100, int(cw.socPercent)) : -1;
  char value[16];
  if (!painted || percent != nextPercent) {
    if (nextPercent < 0) strcpy(value, "--");
    else snprintf(value, sizeof(value), "%d", nextPercent);
    clearField(d, 8, 25, 41, 14, blue);
    label(d, 8, 25, value, white, 2);
    label(d, min(43, 10 + d.getTextWidth(value)), 31, "%", white);
    int before = painted ? percent : 0;
    updateBar(d, 52, 19, 34, 7, before, nextPercent, shine, well);
    updateBar(d, 52, 26, 34, 10, before, nextPercent, green, well);
    updateBar(d, 9, 51, 83, 6, before, nextPercent, green, track, 10);
    percent = nextPercent;
  }
  const int32_t nextTemp = cw.ready ? cw.temperatureX10C : INT32_MIN;
  if (!painted || nextTemp != temperature) {
    if (!cw.ready) strcpy(value, "--");
    else {
      const int temp = cw.temperatureX10C;
      snprintf(value, sizeof(value), "%s%d.%d", temp < 0 ? "-" : "", abs(temp) / 10, abs(temp) % 10);
    }
    drawTemperature(d, value);
    temperature = nextTemp;
  }
}

void PowerBankPage::drawPower(DisplayDriver& d, const PowerBankSnapshot& status, bool live) {
  const auto& hlt = status.hlt;
  const bool active = live && (hlt.charging || connected(hlt.c1State) || connected(hlt.c2State) || hlt.wirelessWork);
  if (!painted || active != boltActive) {
    static const uint8_t bolt[] = {0x0c, 0x18, 0x30, 0x7e, 0x0c, 0x18, 0x30};
    drawPowerRays(d, active ? badgeFace : purple);
    d.setColor(active ? dark : boltInk);
    d.drawXbm(190, 21, bolt, 8, 7);
    boltActive = active;
  }
  const int32_t nextWatts = live ? int32_t(uint32_t(hlt.busMv) * hlt.busMa / 100000) : -1;
  if (!painted || nextWatts != wattsX10) {
    drawWatts(d, nextWatts);
    wattsX10 = nextWatts;
  }
}

void PowerBankPage::drawWireless(DisplayDriver& d, const PowerBankSnapshot& status, bool live) {
  const auto& hlt = status.hlt;
  const char* state = "--";
  if (live) {
    if (hlt.wirelessFod) state = "FOD!";
    else state = hlt.wirelessWork ? "ON" : "OFF";
  }
  if (!painted || strcmp(state, qiState) != 0) {
    const bool fault = live && hlt.wirelessFod;
    const bool on = live && hlt.wirelessWork;
    ColorVal fill = fault ? red : off;
    if (on && !fault) fill = green;
    panel(d, 188, 82, 28, 11, fill, 5);
    if (fault) {
      label(d, 190, 84, state, dark);
    } else {
      panel(d, on ? 207 : 190, 84, 7, 7, white, 3);
      label(d, on ? 191 : 197, 84, state, on ? dark : white);
    }
    qiState = state;
  }
  const int nextFod = live ? int(hlt.wirelessFod) : -1;
  if (!painted || nextFod != fod) {
    ColorVal fill = off;
    if (live) fill = hlt.wirelessFod ? red : gold;
    panel(d, 182, 102, 13, 10, fill, 4);
    const char* value = "--";
    if (live) value = hlt.wirelessFod ? "1" : "0";
    label(d, live ? 186 : 183, 104, value, live ? dark : white);
    fod = nextFod;
  }
  const char* protocol = "";
  if (live && hlt.wirelessWork && hlt.wirelessProtocol != HLT331Serial::WIRELESS_NULL) {
    protocol = HLT331Serial::wirelessProtocolName(hlt.wirelessProtocol);
  }
  if (!painted || strcmp(protocol, qiProtocol) != 0) {
    clearField(d, 154, 116, 63, 7, card);
    label(d, 154, 116, protocol, dark);
    snprintf(qiProtocol, sizeof(qiProtocol), "%s", protocol);
  }
}

void PowerBankPage::draw(DisplayDriver& d, const PowerBankSnapshot& status, bool usbDataEnabled) {
  if (!painted) drawLayout(d);
  const auto& hlt = status.hlt;
  const bool live = status.hltLive();
  const bool shared = !usbDataEnabled && connected(hlt.c1State) && connected(hlt.c2State);
  const bool usbModeChanged = !painted || usbDataMode != usbDataEnabled;
  drawBattery(d, status);
  drawPower(d, status, live);
  drawUsb(d, 2, c1, live ? hlt.c1State : HLT331Serial::PORT_RESERVED, hlt.fastCharge, shared);
  if (usbDataEnabled) {
    if (usbModeChanged) drawUsbDataMode(d, 75);
  } else {
    if (usbModeChanged) drawUsbConnector(d, 75, dark);
    drawUsb(d, 75, c2, live ? hlt.c2State : HLT331Serial::PORT_RESERVED, hlt.fastCharge, shared, usbModeChanged);
  }
  drawWireless(d, status, live);
  usbDataMode = usbDataEnabled;
  painted = true;
}
