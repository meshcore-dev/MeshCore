#include "UITask.h"
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"
#include "target.h"
#ifdef WIFI_SSID
  #include <WiFi.h>
#endif

#ifndef AUTO_OFF_MILLIS
  #define AUTO_OFF_MILLIS     15000   // 15 seconds
#endif
#define BOOT_SCREEN_MILLIS   3000   // 3 seconds

#ifdef PIN_STATUS_LED
#define LED_ON_MILLIS     20
#define LED_ON_MSG_MILLIS 200
#define LED_CYCLE_MILLIS  4000
#endif

#define LONG_PRESS_MILLIS   1200

#ifndef UI_RECENT_LIST_SIZE
  #define UI_RECENT_LIST_SIZE 4
#endif

#if UI_HAS_JOYSTICK
  #define PRESS_LABEL "press Enter"
#else
  #define PRESS_LABEL "long press"
#endif

#include "icons.h"

#ifndef BATT_MIN_MILLIVOLTS
  #define BATT_MIN_MILLIVOLTS 3000
#endif
#ifndef BATT_MAX_MILLIVOLTS
  #define BATT_MAX_MILLIVOLTS 4200
#endif

#define UI_TXT_OFS    10
#define UI_MARGIN     5

struct UILayout {
  bool compact;
  int hud_text;
  int hud_sep;
  int title_y;
  int title_rule;
  int body_y0;
  int row_h;
  int icon_y;
  int icon_label;
  int footer_sep;
  int footer_dot;
  int footer_y;
};

static const UILayout UI_TALL    = { false, 14, 20, 36, 40, 56, 16, 46, 100, 112, 118, 124 };
static const UILayout UI_COMPACT = { true,   0, 11, 12, 20, 22,  9, 20,  52,  60,  62,  54 };

static UILayout g_ui = UI_TALL;

#define UI_HUD_TEXT   (g_ui.hud_text)
#define UI_HUD_SEP    (g_ui.hud_sep)
#define UI_TITLE_Y    (g_ui.title_y)
#define UI_TITLE_RULE (g_ui.title_rule)
#define UI_BODY_Y0    (g_ui.body_y0)
#define UI_ROW_H      (g_ui.row_h)
#define UI_ICON_Y     (g_ui.icon_y)
#define UI_ICON_LABEL (g_ui.icon_label)
#define UI_FOOTER_SEP (g_ui.footer_sep)
#define UI_FOOTER_DOT (g_ui.footer_dot)
#define UI_FOOTER_Y   (g_ui.footer_y)

static int uiOfs(DisplayDriver& d) { return d.isEink() ? UI_TXT_OFS : 0; }

static void uiText(DisplayDriver& d, int x, int gy, const char* s) {
  d.setCursor(x, gy - uiOfs(d));
  d.print(s);
}
static void uiTextCentered(DisplayDriver& d, int mx, int gy, const char* s) {
  d.drawTextCentered(mx, gy - uiOfs(d), s);
}
static void uiTextRight(DisplayDriver& d, int ax, int gy, const char* s) {
  d.drawTextRightAlign(ax, gy - uiOfs(d), s);
}
static void uiTextEllip(DisplayDriver& d, int x, int gy, int maxw, const char* s) {
  d.drawTextEllipsized(x, gy - uiOfs(d), maxw, s);
}

static void uiAbbrevName(char* dst, const char* src) {
  int n = strlen(src);
  if (n <= 13) {
    strcpy(dst, src);
  } else {
    memcpy(dst, src, 5);
    dst[5] = '.'; dst[6] = '.'; dst[7] = '.';
    memcpy(dst + 8, src + n - 5, 5);
    dst[13] = 0;
  }
}

static int uiBatteryPercent(uint16_t mv) {
  // Convert millivolts to percentage
  int p = ((int)mv - BATT_MIN_MILLIVOLTS) * 100 / (BATT_MAX_MILLIVOLTS - BATT_MIN_MILLIVOLTS);
  if (p < 0) p = 0;
  if (p > 100) p = 100;
  return p;
}

static void uiFillRoundRect(DisplayDriver& d, int x, int y, int w, int h, DisplayDriver::Color c) {
  d.setColor(c);
  d.fillRect(x, y, w, h);
  d.setColor(DisplayDriver::DARK);
  d.fillRect(x, y, 1, 1);
  d.fillRect(x + w - 1, y, 1, 1);
  d.fillRect(x, y + h - 1, 1, 1);
  d.fillRect(x + w - 1, y + h - 1, 1, 1);
  d.setColor(c);
}

static void uiSectionTitle(DisplayDriver& d, const char* t) {
  d.setTextSize(1);
  d.setColor(DisplayDriver::LIGHT);
  uiTextCentered(d, d.width() / 2, UI_TITLE_Y, t);
  int w = d.getTextWidth(t);
  d.fillRect((d.width() - w) / 2, UI_TITLE_RULE, w, 1);
}

static void uiRow(DisplayDriver& d, int y, const char* label, const char* value) {
  d.setTextSize(1);
  d.setColor(DisplayDriver::LIGHT);
  uiText(d, UI_MARGIN, y, label);
  uiTextRight(d, d.width() - UI_MARGIN, y, value);
}

static void uiBattery(DisplayDriver& d, uint16_t mv, bool charging) {
  int pct = uiBatteryPercent(mv);
  int bw = 22, bh = g_ui.compact ? 9 : 11;
  int bx = d.width() - bw - 3, by = g_ui.compact ? 0 : 3;
  d.setColor(DisplayDriver::LIGHT);
  d.drawRect(bx, by, bw, bh);
  d.fillRect(bx + bw, by + 3, 2, bh - 6);
  int fw = (pct * (bw - 4)) / 100;
  d.fillRect(bx + 2, by + 2, fw, bh - 4);
  if (charging) {
    d.drawXbm(bx - 11, by + 1, charging_icon, 8, 8);
  }
}

static void uiWrapText(DisplayDriver& d, int x, int gy, int maxw, int lineH, int maxLines, const char* s) {
  int ofs = uiOfs(d);
  char line[96];
  line[0] = 0;
  int ll = 0, lines = 0;
  char word[64];
  const char* p = s;
  while (*p && lines < maxLines) {
    while (*p == ' ') p++;
    int wl = 0;
    while (*p && *p != ' ' && wl < (int)sizeof(word) - 1) word[wl++] = *p++;
    word[wl] = 0;
    if (wl == 0) break;
    char trial[128];
    if (ll == 0) {
      strcpy(trial, word);
    } else {
      snprintf(trial, sizeof(trial), "%s %s", line, word);
    }
    if (d.getTextWidth(trial) <= maxw) {
      strcpy(line, trial);
      ll = strlen(line);
    } else if (ll > 0) {
      d.setCursor(x, gy - ofs);
      d.print(line);
      gy += lineH;
      lines++;
      strcpy(line, word);
      ll = wl;
    } else {
      d.setCursor(x, gy - ofs);
      d.print(word);
      gy += lineH;
      lines++;
      line[0] = 0;
      ll = 0;
    }
  }
  if (ll > 0 && lines < maxLines) {
    d.setCursor(x, gy - ofs);
    d.print(line);
  }
}

class SplashScreen : public UIScreen {
  UITask* _task;
  unsigned long _start;
  unsigned long dismiss_after;
  char _version_info[12];

public:
  SplashScreen(UITask* task) : _task(task) {
    // e.g: v1.2.3-abcdef -> v1.2.3
    const char *ver = FIRMWARE_VERSION;
    const char *dash = strchr(ver, '-');

    int len = dash ? dash - ver : strlen(ver);
    if (len >= sizeof(_version_info)) len = sizeof(_version_info) - 1;
    memcpy(_version_info, ver, len);
    _version_info[len] = 0;

    _start = millis();
    dismiss_after = _start + BOOT_SCREEN_MILLIS;
  }

  int render(DisplayDriver& display) override {
    int W = display.width();

    // meshcore logo
    float pf = (float)(millis() - _start) / (float)BOOT_SCREEN_MILLIS;
    if (pf < 0) pf = 0;
    if (pf > 1) pf = 1;

    if (g_ui.compact) {
      display.setColor(DisplayDriver::BLUE);
      display.drawXbm((W - 128) / 2, 4, meshcore_logo, 128, 13);

      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(1);
      uiTextCentered(display, W / 2, 22, "COMPANION");
      uiTextCentered(display, W / 2, 34, _version_info);
      uiTextCentered(display, W / 2, 44, FIRMWARE_BUILD_DATE);

      int bx = 28, bw = W - 56;
      display.drawRect(bx, 55, bw, 7);
      display.fillRect(bx + 2, 57, (int)((bw - 4) * pf), 3);
      return 400;
    }

    display.setColor(DisplayDriver::BLUE);
    display.drawXbm((W - 128) / 2, 30, meshcore_logo, 128, 13);

    int aw = 50;
    display.setColor(DisplayDriver::LIGHT);
    display.fillRect((W - aw) / 2, 58, aw, 1);

    display.setTextSize(1);
    uiTextCentered(display, W / 2, 74, "COMPANION");
    uiTextCentered(display, W / 2, 90, _version_info);
    uiTextCentered(display, W / 2, 102, FIRMWARE_BUILD_DATE);

    int bx = 28, bw = W - 56;
    display.drawRect(bx, 110, bw, 7);
    display.fillRect(bx + 2, 112, (int)((bw - 4) * pf), 3);

    return 400;
  }

  void poll() override {
    if (millis() >= dismiss_after) {
      _task->gotoHomeScreen();
    }
  }
};

class HomeScreen : public UIScreen {
  enum HomePage {
    FIRST,
    RECENT,
    RADIO,
    BLUETOOTH,
    ADVERT,
#if ENV_INCLUDE_GPS == 1
    GPS,
#endif
#if UI_SENSORS_PAGE == 1
    SENSORS,
#endif
    ACTIVITY,
    SHUTDOWN,
    Count    // keep as last
  };

  UITask* _task;
  mesh::RTCClock* _rtc;
  SensorManager* _sensors;
  NodePrefs* _node_prefs;
  uint8_t _page;
  bool _shutdown_init;
  AdvertPath recent[UI_RECENT_LIST_SIZE];

  void drawHud(DisplayDriver& d) {
    char nm[sizeof(_node_prefs->node_name)];
    char shortnm[16];
    d.translateUTF8ToBlocks(nm, _node_prefs->node_name, sizeof(nm));
    uiAbbrevName(shortnm, nm);
    d.setTextSize(1);
    d.setColor(DisplayDriver::LIGHT);
    uiText(d, UI_MARGIN, UI_HUD_TEXT, shortnm);
    uiBattery(d, _task->getBattMilliVolts(), _task->isExternalPowered());
    d.setColor(DisplayDriver::LIGHT);
    d.fillRect(0, UI_HUD_SEP, d.width(), 1);
  }

  void drawFooter(DisplayDriver& d, const char* hint) {
    d.setColor(DisplayDriver::LIGHT);
    if (!g_ui.compact) d.fillRect(0, UI_FOOTER_SEP, d.width(), 1);
    int n = HomePage::Count;
    int spacing = 7, x = UI_MARGIN;
    for (int i = 0; i < n; i++, x += spacing) {
      if (i == _page) {
        d.fillRect(x - 1, UI_FOOTER_DOT - 1, 4, 4);
      } else {
        d.fillRect(x, UI_FOOTER_DOT, 2, 2);
      }
    }
    if (hint && hint[0] && !g_ui.compact) {
      d.setTextSize(1);
      uiTextRight(d, d.width() - UI_MARGIN, UI_FOOTER_Y, hint);
    }
  }

  CayenneLPP sensors_lpp;
  int sensors_nb = 0;
  bool sensors_scroll = false;
  int sensors_scroll_offset = 0;
  int next_sensors_refresh = 0;

  void refresh_sensors() {
    if (millis() > next_sensors_refresh) {
      sensors_lpp.reset();
      sensors_nb = 0;
      sensors_lpp.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
      sensors.querySensors(0xFF, sensors_lpp);
      LPPReader reader (sensors_lpp.getBuffer(), sensors_lpp.getSize());
      uint8_t channel, type;
      while(reader.readHeader(channel, type)) {
        reader.skipData(type);
        sensors_nb ++;
      }
      sensors_scroll = sensors_nb > 4;
#if AUTO_OFF_MILLIS > 0
      next_sensors_refresh = millis() + 5000; // refresh sensor values every 5 sec
#else
      next_sensors_refresh = millis() + 60000; // refresh sensor values every 1 min
#endif
    }
  }

public:
  HomeScreen(UITask* task, mesh::RTCClock* rtc, SensorManager* sensors, NodePrefs* node_prefs)
     : _task(task), _rtc(rtc), _sensors(sensors), _node_prefs(node_prefs), _page(0),
       _shutdown_init(false), sensors_lpp(200) {  }

  void poll() override {
    if (_shutdown_init && !_task->isButtonPressed()) {  // must wait for USR button to be released
      _task->shutdown();
    }
  }

  int render(DisplayDriver& display) override {
    int W = display.width();

    if (_page == HomePage::SHUTDOWN && _shutdown_init) {
      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(2);
      uiTextCentered(display, W / 2, g_ui.compact ? 24 : 70, "Hibernating");
      return 60000;
    }

    char v[48];
    drawHud(display);
    const char* hint = "";

    if (_page == HomePage::FIRST) {
      uiSectionTitle(display, "STATUS");
      sprintf(v, "%d", _task->getMsgCount());
      uiRow(display, UI_BODY_Y0, "Messages", v);
      const char* bt;
      if (_task->isSerialEnabled() && _task->isPairingAllowed() && !_task->hasConnection() && the_mesh.getBLEPin() != 0) {
        sprintf(v, "PIN %d", the_mesh.getBLEPin());
        bt = v;
      } else if (_task->hasConnection()) {
        bt = "linked";
      } else if (!_task->isSerialEnabled()) {
        bt = "off";
      } else {
        bt = "ready";
      }
      uiRow(display, UI_BODY_Y0 + UI_ROW_H, "Bluetooth", bt);
      char b2[16];
      int pct = uiBatteryPercent(_task->getBattMilliVolts());
      if (_task->isExternalPowered()) {
        sprintf(b2, "%d%% charging", pct);
      } else {
        sprintf(b2, "%d%%", pct);
      }
      uiRow(display, UI_BODY_Y0 + UI_ROW_H * 2, "Battery", b2);
#ifdef WIFI_SSID
      IPAddress ip = WiFi.localIP();
      snprintf(v, sizeof(v), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
      uiRow(display, UI_BODY_Y0 + UI_ROW_H * 3, "IP", v);
#endif
    } else if (_page == HomePage::RECENT) {
      uiSectionTitle(display, "RECENTLY HEARD");
      the_mesh.getRecentlyHeard(recent, UI_RECENT_LIST_SIZE);
      display.setColor(DisplayDriver::LIGHT);
      int y = UI_BODY_Y0, shown = 0;
      for (int i = 0; i < UI_RECENT_LIST_SIZE && shown < 4; i++) {
        auto a = &recent[i];
        if (a->name[0] == 0) continue;  // empty slot
        int secs = _rtc->getCurrentTime() - a->recv_timestamp;
        if (secs < 60) {
          sprintf(v, "%ds", secs);
        } else if (secs < 60*60) {
          sprintf(v, "%dm", secs / 60);
        } else {
          sprintf(v, "%dh", secs / (60*60));
        }
        int tw = display.getTextWidth(v);
        char nm[sizeof(a->name)];
        display.translateUTF8ToBlocks(nm, a->name, sizeof(nm));
        uiTextEllip(display, UI_MARGIN, y, W - tw - UI_MARGIN - 6, nm);
        uiTextRight(display, W - UI_MARGIN, y, v);
        y += UI_ROW_H;
        shown++;
      }
      if (shown == 0) {
        uiTextCentered(display, W / 2, UI_BODY_Y0 + UI_ROW_H, "no nodes yet");
      }
    } else if (_page == HomePage::RADIO) {
      uiSectionTitle(display, "RADIO");
      sprintf(v, "%.3f", _node_prefs->freq);
      uiRow(display, UI_BODY_Y0, "Freq MHz", v);
      sprintf(v, "%d / %.0f", _node_prefs->sf, _node_prefs->bw);
      uiRow(display, UI_BODY_Y0 + UI_ROW_H, "SF / BW", v);
      sprintf(v, "%d / %ddBm", _node_prefs->cr, _node_prefs->tx_power_dbm);
      uiRow(display, UI_BODY_Y0 + UI_ROW_H * 2, "CR / TX", v);
      sprintf(v, "%d", radio_driver.getNoiseFloor());
      uiRow(display, UI_BODY_Y0 + UI_ROW_H * 3, "Noise", v);
    } else if (_page == HomePage::BLUETOOTH) {
      uiSectionTitle(display, "BLUETOOTH");
      display.setColor(DisplayDriver::LIGHT);
      display.drawXbm((W - 32) / 2, UI_ICON_Y,
          _task->isSerialEnabled() ? bluetooth_on : bluetooth_off, 32, 32);
      display.setTextSize(1);
      if (_task->isSerialEnabled() && _task->isPairingAllowed() && !_task->hasConnection() && the_mesh.getBLEPin() != 0) {
        char pinbuf[20];
        snprintf(pinbuf, sizeof(pinbuf), "PIN %u", (unsigned)the_mesh.getBLEPin());
        uiTextCentered(display, W / 2, UI_ICON_LABEL, pinbuf);
      } else {
        uiTextCentered(display, W / 2, UI_ICON_LABEL,
            _task->isSerialEnabled()
              ? (_task->hasConnection() ? "linked"
                  : (_task->isPairingAllowed() ? "on - pairing open" : "on - pairing locked"))
              : "off");
      }
      hint = "hold = options";
    } else if (_page == HomePage::ADVERT) {
      uiSectionTitle(display, "ADVERT");
      display.setColor(DisplayDriver::LIGHT);
      display.drawXbm((W - 32) / 2, UI_ICON_Y, advert_icon, 32, 32);
      display.setTextSize(1);
      uiTextCentered(display, W / 2, UI_ICON_LABEL, "Broadcast presence");
      hint = "hold = options";
#if ENV_INCLUDE_GPS == 1
    } else if (_page == HomePage::GPS) {
      uiSectionTitle(display, "GPS");
      LocationProvider* nmea = sensors.getLocationProvider();
      bool gps_state = _task->getGPSState();
#ifdef PIN_GPS_SWITCH
      bool hw_gps_state = digitalRead(PIN_GPS_SWITCH);
      if (gps_state != hw_gps_state) {
        uiRow(display, UI_BODY_Y0, "Power", gps_state ? "on (hw off)" : "off (hw)");
      } else {
        uiRow(display, UI_BODY_Y0, "Power", gps_state ? "on" : "off");
      }
#else
      uiRow(display, UI_BODY_Y0, "Power", gps_state ? "on" : "off");
#endif
      if (nmea == NULL) {
        uiTextCentered(display, W / 2, UI_BODY_Y0 + UI_ROW_H * 2, "GPS unavailable");
      } else {
        sprintf(v, "%s / %d", nmea->isValid() ? "yes" : "no", nmea->satellitesCount());
        uiRow(display, UI_BODY_Y0 + UI_ROW_H, "Fix / Sat", v);
        sprintf(v, "%.4f", sensors.node_lat);
        uiRow(display, UI_BODY_Y0 + UI_ROW_H * 2, "Lat", v);
        sprintf(v, "%.4f", sensors.node_lon);
        uiRow(display, UI_BODY_Y0 + UI_ROW_H * 3, "Lon", v);
      }
      hint = "hold = options";
#endif
#if UI_SENSORS_PAGE == 1
    } else if (_page == HomePage::SENSORS) {
      uiSectionTitle(display, "SENSORS");
      refresh_sensors();
      char name[30];
      LPPReader r(sensors_lpp.getBuffer(), sensors_lpp.getSize());

      for (int i = 0; i < sensors_scroll_offset; i++) {
        uint8_t channel, type;
        r.readHeader(channel, type);
        r.skipData(type);
      }

      if (sensors_nb == 0) {
        uiTextCentered(display, W / 2, UI_BODY_Y0 + UI_ROW_H, "no sensors");
      }

      int y = UI_BODY_Y0;
      int rows = sensors_scroll ? 4 : sensors_nb;
      for (int i = 0; i < rows; i++) {
        uint8_t channel, type;
        if (!r.readHeader(channel, type)) { // reached end, reset
          r.reset();
          r.readHeader(channel, type);
        }
        float val;
        switch (type) {
          case LPP_GPS: // GPS
            float lat, lon, alt;
            r.readGPS(lat, lon, alt);
            strcpy(name, "gps"); sprintf(v, "%.4f %.4f", lat, lon);
            break;
          case LPP_VOLTAGE:
            r.readVoltage(val);
            strcpy(name, "voltage"); sprintf(v, "%.2f", val);
            break;
          case LPP_CURRENT:
            r.readCurrent(val);
            strcpy(name, "current"); sprintf(v, "%.3f", val);
            break;
          case LPP_TEMPERATURE:
            r.readTemperature(val);
            strcpy(name, "temperature"); sprintf(v, "%.2f", val);
            break;
          case LPP_RELATIVE_HUMIDITY:
            r.readRelativeHumidity(val);
            strcpy(name, "humidity"); sprintf(v, "%.2f", val);
            break;
          case LPP_BAROMETRIC_PRESSURE:
            r.readPressure(val);
            strcpy(name, "pressure"); sprintf(v, "%.2f", val);
            break;
          case LPP_ALTITUDE:
            r.readAltitude(val);
            strcpy(name, "altitude"); sprintf(v, "%.0f", val);
            break;
          case LPP_POWER:
            r.readPower(val);
            strcpy(name, "power"); sprintf(v, "%.2f", val);
            break;
          default:
            r.skipData(type);
            strcpy(name, "unk"); v[0] = 0;
        }
        uiRow(display, y, name, v);
        y += UI_ROW_H;
      }
      if (sensors_scroll) sensors_scroll_offset = (sensors_scroll_offset + 1) % sensors_nb;
      else sensors_scroll_offset = 0;
#endif
    } else if (_page == HomePage::ACTIVITY) {
      uiSectionTitle(display, "ACTIVITY");
      int count = _task->getActivityCount();
      if (count == 0) {
        uiTextCentered(display, W / 2, UI_BODY_Y0 + UI_ROW_H, "nothing yet");
      } else {
        int y = UI_BODY_Y0;
        int shown = count < 4 ? count : 4;
        for (int i = 0; i < shown; i++) {
          int secs = _rtc->getCurrentTime() - _task->getActivityTime(i);
          if (secs < 0) secs = 0;
          if (secs < 60) {
            sprintf(v, "%ds", secs);
          } else if (secs < 60*60) {
            sprintf(v, "%dm", secs / 60);
          } else {
            sprintf(v, "%dh", secs / (60*60));
          }
          int tw = display.getTextWidth(v);
          char line[40];
          display.translateUTF8ToBlocks(line, _task->getActivityText(i), sizeof(line));
          uiTextEllip(display, UI_MARGIN, y, W - tw - UI_MARGIN - 6, line);
          uiTextRight(display, W - UI_MARGIN, y, v);
          y += UI_ROW_H;
        }
      }
    } else if (_page == HomePage::SHUTDOWN) {
      uiSectionTitle(display, "POWER");
      display.setColor(DisplayDriver::LIGHT);
      display.drawXbm((W - 32) / 2, UI_ICON_Y, power_icon, 32, 32);
      display.setTextSize(1);
      uiTextCentered(display, W / 2, UI_ICON_LABEL, "Hibernate device");
      hint = "hold = sleep";
    }

    drawFooter(display, hint);
    return 10000;   // next render after 10000 ms
  }

  bool handleInput(char c) override {
    if (c == KEY_LEFT || c == KEY_PREV) {
      _page = (_page + HomePage::Count - 1) % HomePage::Count;
      return true;
    }
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      _page = (_page + 1) % HomePage::Count;
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::BLUETOOTH) {
      _task->openMenu(UITask::MENU_BLE);
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::ADVERT) {
      _task->openMenu(UITask::MENU_ADVERT);
      return true;
    }
#if ENV_INCLUDE_GPS == 1
    if (c == KEY_ENTER && _page == HomePage::GPS) {
      _task->openMenu(UITask::MENU_GPS);
      return true;
    }
#endif
#if UI_SENSORS_PAGE == 1
    if (c == KEY_ENTER && _page == HomePage::SENSORS) {
      next_sensors_refresh = 0;
      return true;
    }
#endif
    if (c == KEY_ENTER && _page == HomePage::SHUTDOWN) {
      _shutdown_init = true;  // need to wait for button to be released
      return true;
    }
    return false;
  }
};

class MsgPreviewScreen : public UIScreen {
  UITask* _task;
  mesh::RTCClock* _rtc;

  struct MsgEntry {
    uint32_t timestamp;
    char origin[62];
    char msg[78];
  };
  #define MAX_UNREAD_MSGS   32
  int num_unread;
  int head = MAX_UNREAD_MSGS - 1; // index of latest unread message
  MsgEntry unread[MAX_UNREAD_MSGS];

public:
  MsgPreviewScreen(UITask* task, mesh::RTCClock* rtc) : _task(task), _rtc(rtc) { num_unread = 0; }

  void addPreview(uint8_t path_len, const char* from_name, const char* msg) {
    head = (head + 1) % MAX_UNREAD_MSGS;
    if (num_unread < MAX_UNREAD_MSGS) num_unread++;

    auto p = &unread[head];
    p->timestamp = _rtc->getCurrentTime();
    if (path_len == 0xFF) {
      sprintf(p->origin, "(D) %s", from_name);
    } else {
      sprintf(p->origin, "(%d) %s", (uint32_t) path_len, from_name);
    }
    StrHelper::strncpy(p->msg, msg, sizeof(p->msg));
  }

  int render(DisplayDriver& display) override {
    int W = display.width();
    char tmp[24];
    auto p = &unread[head];

    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    uiText(display, UI_MARGIN, UI_HUD_TEXT, "INBOX");
    sprintf(tmp, "%d unread", num_unread);
    uiTextRight(display, W - UI_MARGIN, UI_HUD_TEXT, tmp);
    display.fillRect(0, UI_HUD_SEP, W, 1);

    int secs = _rtc->getCurrentTime() - p->timestamp;
    if (secs < 60) {
      sprintf(tmp, "%ds", secs);
    } else if (secs < 60*60) {
      sprintf(tmp, "%dm", secs / 60);
    } else {
      sprintf(tmp, "%dh", secs / (60*60));
    }

    int tw = display.getTextWidth(tmp);
    char filtered_origin[sizeof(p->origin)];
    display.translateUTF8ToBlocks(filtered_origin, p->origin, sizeof(filtered_origin));
    display.setColor(DisplayDriver::LIGHT);
    int origin_y = g_ui.compact ? 14 : 38;
    int msg_y = g_ui.compact ? 24 : 58;
    int msg_lines = g_ui.compact ? 3 : 4;
    uiTextEllip(display, UI_MARGIN, origin_y, W - tw - UI_MARGIN - 6, filtered_origin);
    uiTextRight(display, W - UI_MARGIN, origin_y, tmp);

    char filtered_msg[sizeof(p->msg)];
    display.translateUTF8ToBlocks(filtered_msg, p->msg, sizeof(filtered_msg));
    uiWrapText(display, UI_MARGIN, msg_y, W - UI_MARGIN * 2, UI_ROW_H, msg_lines, filtered_msg);

    if (g_ui.compact) {
      uiTextCentered(display, W / 2, 55, "hold = clear all");
    } else {
      display.fillRect(0, UI_FOOTER_SEP, W, 1);
      uiTextCentered(display, W / 2, UI_FOOTER_Y, "hold = clear all");
    }

#if AUTO_OFF_MILLIS==0 // probably e-ink
    return 10000; // 10 s
#else
    return 1000;  // next render after 1000 ms
#endif
  }

  bool handleInput(char c) override {
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      head = (head + MAX_UNREAD_MSGS - 1) % MAX_UNREAD_MSGS;
      num_unread--;
      if (num_unread == 0) {
        _task->gotoHomeScreen();
      }
      return true;
    }
    if (c == KEY_ENTER) {
      num_unread = 0;  // clear unread queue
      _task->gotoHomeScreen();
      return true;
    }
    return false;
  }
};

class MenuScreen : public UIScreen {
  UITask* _task;
  UITask::MenuKind _kind;
  int _sel;
  int _count;

  void buildLabels(const char* out[]) {
    if (_kind == UITask::MENU_ADVERT) {
      out[0] = "Zero-hop advert";
      out[1] = "Flood advert";
      out[2] = "Back";
    } else if (_kind == UITask::MENU_GPS) {
      out[0] = _task->getGPSState() ? "Disable GPS" : "Enable GPS";
      out[1] = "Resync GPS";
      out[2] = "Back";
    } else {
      out[0] = _task->isSerialEnabled() ? "Disable BLE" : "Enable BLE";
      out[1] = _task->isPairingAllowed() ? "Lock pairing" : "Unlock pairing";
      out[2] = "Back";
    }
  }

  const char* title() {
    if (_kind == UITask::MENU_ADVERT) return "ADVERTS";
    if (_kind == UITask::MENU_GPS) return "GPS OPTIONS";
    return "BLE OPTIONS";
  }

public:
  MenuScreen(UITask* task) : _task(task), _kind(UITask::MENU_ADVERT), _sel(0), _count(3) {  }

  void configure(UITask::MenuKind kind) {
    _kind = kind;
    _sel = 0;
    _count = 3;
  }

  int render(DisplayDriver& d) override {
    int W = d.width();
    uiSectionTitle(d, title());

    const char* labels[4];
    buildLabels(labels);

    int y = UI_BODY_Y0;
    for (int i = 0; i < _count; i++) {
      if (i == _sel) {
        if (g_ui.compact) {
          uiFillRoundRect(d, 2, y - 1, W - 4, UI_ROW_H, DisplayDriver::LIGHT);
        } else {
          uiFillRoundRect(d, 2, y - 12, W - 4, 16, DisplayDriver::LIGHT);
        }
        d.setColor(DisplayDriver::DARK);
      } else {
        d.setColor(DisplayDriver::LIGHT);
      }
      d.setTextSize(1);
      uiText(d, UI_MARGIN + 2, y, labels[i]);
      y += UI_ROW_H;
    }

    d.setColor(DisplayDriver::LIGHT);
    if (g_ui.compact) {
      uiTextRight(d, W - UI_MARGIN, 55, "click=move hold=ok");
    } else {
      d.fillRect(0, UI_FOOTER_SEP, W, 1);
      uiTextRight(d, W - UI_MARGIN, UI_FOOTER_Y, "click=move hold=ok");
    }
    return 10000;
  }

  bool handleInput(char c) override {
    if (c == KEY_DOWN || c == KEY_NEXT) {
      _sel = (_sel + 1) % _count;
      return true;
    }
    if (c == KEY_UP || c == KEY_PREV) {
      _sel = (_sel + _count - 1) % _count;
      return true;
    }
    if (c == KEY_ENTER) {
      if (_sel == _count - 1) {
        _task->gotoHomeScreen();
      } else {
        _task->menuSelect(_kind, _sel);
      }
      return true;
    }
    if (c == KEY_LEFT || c == KEY_SELECT) {
      _task->gotoHomeScreen();
      return true;
    }
    return false;
  }
};

void UITask::openMenu(MenuKind kind) {
  ((MenuScreen*) menu)->configure(kind);
  setCurrScreen(menu);
}

void UITask::menuSelect(MenuKind kind, int item) {
  if (kind == MENU_ADVERT) {
    bool flood = (item == 1);
    notify(UIEventType::ack);
    if (the_mesh.advert(flood)) {
      showAlert(flood ? "Flood advert sent" : "Zero-hop sent", 1000);
      onClientActivity(flood ? "Sent flood advert" : "Sent zero-hop advert");
    } else {
      showAlert("Advert failed", 1000);
    }
    gotoHomeScreen();
  } else if (kind == MENU_GPS) {
    if (item == 0) {
      toggleGPS();
    } else if (item == 1) {
      resyncGPS();
    }
    _next_refresh = 0;
  } else {
    if (item == 0) {
      if (isSerialEnabled()) disableSerial(); else enableSerial();
      showAlert(isSerialEnabled() ? "BLE enabled" : "BLE disabled", 900);
      onClientActivity(isSerialEnabled() ? "BLE enabled" : "BLE disabled");
    } else if (item == 1) {
      setPairingAllowed(!isPairingAllowed());
      _node_prefs->pairing_locked = isPairingAllowed() ? 0 : 1;
      the_mesh.savePrefs();
      showAlert(isPairingAllowed() ? "Pairing open" : "Pairing locked", 900);
      onClientActivity(isPairingAllowed() ? "Pairing unlocked" : "Pairing locked");
    }
    _next_refresh = 0;
  }
}

void UITask::resyncGPS() {
  if (_sensors != NULL) {
    _sensors->setSettingValue("gps", "0");
    _sensors->setSettingValue("gps", "1");
    _node_prefs->gps_enabled = 1;
    the_mesh.savePrefs();
    notify(UIEventType::ack);
    onClientActivity("GPS resync");
    showAlert("GPS resync...", 900);
    _next_refresh = 0;
  }
}

void UITask::onClientActivity(const char* text) {
  _activity_head = (_activity_head + 1) % UI_ACTIVITY_SIZE;
  if (_activity_count < UI_ACTIVITY_SIZE) _activity_count++;
  _activity[_activity_head].ts = rtc_clock.getCurrentTime();
  StrHelper::strncpy(_activity[_activity_head].text, text, sizeof(_activity[_activity_head].text));
}

const char* UITask::getActivityText(int i) const {
  if (i < 0 || i >= _activity_count) return "";
  int idx = (_activity_head - i + UI_ACTIVITY_SIZE) % UI_ACTIVITY_SIZE;
  return _activity[idx].text;
}

uint32_t UITask::getActivityTime(int i) const {
  if (i < 0 || i >= _activity_count) return 0;
  int idx = (_activity_head - i + UI_ACTIVITY_SIZE) % UI_ACTIVITY_SIZE;
  return _activity[idx].ts;
}

void UITask::drawHibernation() {
  if (_display == NULL) return;
  DisplayDriver& d = *_display;
  d.fullRefresh();
  d.startFrame();
  int W = d.width();

  uiBattery(d, getBattMilliVolts(), false);
  char buf[44];
  sprintf(buf, "%d%%", uiBatteryPercent(getBattMilliVolts()));
  d.setTextSize(1);
  d.setColor(DisplayDriver::LIGHT);
  uiTextRight(d, W - 28, UI_HUD_TEXT, buf);

  int logo_y = g_ui.compact ? 4 : 46;
  int asleep_y = g_ui.compact ? 20 : 76;
  int gps_y = g_ui.compact ? 32 : 92;
  int lowbatt_y = g_ui.compact ? 44 : 104;
  int wake_y = g_ui.compact ? 54 : 116;

  d.setColor(DisplayDriver::BLUE);
  d.drawXbm((W - 128) / 2, logo_y, meshcore_logo, 128, 13);

  d.setColor(DisplayDriver::LIGHT);
  d.setTextSize(1);
  uint32_t t = rtc_clock.getCurrentTime();
  int hh = (t / 3600) % 24, mm = (t / 60) % 60;
  snprintf(buf, sizeof(buf), "Asleep at %02d:%02d UTC", hh, mm);
  uiTextCentered(d, W / 2, asleep_y, buf);

  if (getGPSState()) {
    if (sensors.node_lat != 0 || sensors.node_lon != 0) {
      snprintf(buf, sizeof(buf), "%.4f, %.4f", sensors.node_lat, sensors.node_lon);
    } else {
      strcpy(buf, "location unknown");
    }
    uiTextCentered(d, W / 2, gps_y, buf);
  }

  if (_low_batt_shutdown) {
    d.setColor(DisplayDriver::RED);
    uiTextCentered(d, W / 2, lowbatt_y, "Battery was almost empty");
    d.setColor(DisplayDriver::LIGHT);
  }

  uiTextCentered(d, W / 2, wake_y, "press button to wake");
  d.endFrame();
}

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;

  g_ui = (display != NULL && display->height() < 100) ? UI_COMPACT : UI_TALL;
  _auto_off = millis() + AUTO_OFF_MILLIS;

#if defined(PIN_USER_BTN)
  user_btn.begin();
#endif
#if defined(PIN_USER_BTN_ANA)
  analog_btn.begin();
#endif

  _node_prefs = node_prefs;

  if (_display != NULL) {
    _display->turnOn();
  }

#ifdef PIN_BUZZER
  buzzer.begin();
  buzzer.quiet(_node_prefs->buzzer_quiet);
  buzzer.startup();
#endif

#ifdef PIN_VIBRATION
  vibration.begin();
#endif

  ui_started_at = millis();
  _alert_expiry = 0;

  splash = new SplashScreen(this);
  home = new HomeScreen(this, &rtc_clock, sensors, node_prefs);
  msg_preview = new MsgPreviewScreen(this, &rtc_clock);
  menu = new MenuScreen(this);
  setCurrScreen(splash);
}

void UITask::showAlert(const char* text, int duration_millis) {
  strcpy(_alert, text);
  _alert_expiry = millis() + duration_millis;
}

void UITask::notify(UIEventType t) {
#if defined(PIN_BUZZER)
switch(t){
  case UIEventType::contactMessage:
    // gemini's pick
    buzzer.play("MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7");
    break;
  case UIEventType::channelMessage:
    buzzer.play("kerplop:d=16,o=6,b=120:32g#,32c#");
    break;
  case UIEventType::ack:
    buzzer.play("ack:d=32,o=8,b=120:c");
    break;
  case UIEventType::roomMessage:
  case UIEventType::newContactMessage:
  case UIEventType::none:
  default:
    break;
}
#endif

#ifdef PIN_VIBRATION
  // Trigger vibration for all UI events except none
  if (t != UIEventType::none) {
    vibration.trigger();
  }
#endif
}


void UITask::msgRead(int msgcount) {
  _msgcount = msgcount;
  if (msgcount == 0) {
    gotoHomeScreen();
  }
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) {
  _msgcount = msgcount;

  ((MsgPreviewScreen *) msg_preview)->addPreview(path_len, from_name, text);
  setCurrScreen(msg_preview);

  if (_display != NULL) {
    if (!_display->isOn() && !hasConnection()) {
      _display->turnOn();
    }
    if (_display->isOn()) {
    _auto_off = millis() + AUTO_OFF_MILLIS;  // extend the auto-off timer
    _next_refresh = 100;  // trigger refresh
    }
  }
}

void UITask::userLedHandler() {
#ifdef PIN_STATUS_LED
  int cur_time = millis();
  if (cur_time > next_led_change) {
    if (led_state == 0) {
      led_state = 1;
      if (_msgcount > 0) {
        last_led_increment = LED_ON_MSG_MILLIS;
      } else {
        last_led_increment = LED_ON_MILLIS;
      }
      next_led_change = cur_time + last_led_increment;
    } else {
      led_state = 0;
      next_led_change = cur_time + LED_CYCLE_MILLIS - last_led_increment;
    }
    digitalWrite(PIN_STATUS_LED, led_state == LED_STATE_ON);
  }
#endif
}

void UITask::setCurrScreen(UIScreen* c) {
  curr = c;
  _next_refresh = 100;
}

/*
  hardware-agnostic pre-shutdown activity should be done here
*/
void UITask::shutdown(bool restart){

  #ifdef PIN_BUZZER
  /* note: we have a choice here -
     we can do a blocking buzzer.loop() with non-deterministic consequences
     or we can set a flag and delay the shutdown for a couple of seconds
     while a non-blocking buzzer.loop() plays out in UITask::loop()
  */
  buzzer.shutdown();
  uint32_t buzzer_timer = millis(); // fail-safe shutdown
  while (buzzer.isPlaying() && (millis() - 2500) < buzzer_timer)
    buzzer.loop();

  #endif // PIN_BUZZER

  if (restart) {
    _board->reboot();
  } else {
    drawHibernation();
    _display->turnOff();
    radio_driver.powerOff();
    _board->powerOff();
  }
}

bool UITask::isButtonPressed() const {
#ifdef PIN_USER_BTN
  return user_btn.isPressed();
#else
  return false;
#endif
}

void UITask::loop() {
  char c = 0;

  bool conn = hasConnection();
  if (conn != _prev_conn) {
    onClientActivity(conn ? "App connected" : "App disconnected");
    _prev_conn = conn;
  }

  bool charging = isExternalPowered();
  if (charging != _prev_charging) {
    _prev_charging = charging;
    if (_display != NULL && !_display->isOn()) _display->turnOn();
    _next_refresh = 0;
  }

#if UI_HAS_JOYSTICK
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_ENTER);  // REVISIT: could be mapped to different key code
  }
  ev = joystick_left.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_LEFT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_LEFT);
  }
  ev = joystick_right.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_RIGHT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_RIGHT);
  }
#if defined(JOYSTICK_UP) && defined(JOYSTICK_DOWN)
  ev = joystick_up.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_UP);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_UP);
  }
  ev = joystick_down.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_DOWN);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_DOWN);
  }
#endif
  ev = back_btn.check();
  if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    c = handleTripleClick(KEY_SELECT);
  }
#elif defined(PIN_USER_BTN)
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_NEXT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
    c = handleDoubleClick(KEY_PREV);
  } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    c = handleTripleClick(KEY_SELECT);
  }
#endif
#if defined(PIN_USER_BTN_ANA)
  if (abs(millis() - _analogue_pin_read_millis) > 10) {
    int ev = analog_btn.check();
    if (ev == BUTTON_EVENT_CLICK) {
      c = checkDisplayOn(KEY_NEXT);
    } else if (ev == BUTTON_EVENT_LONG_PRESS) {
      c = handleLongPress(KEY_ENTER);
    } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
      c = handleDoubleClick(KEY_PREV);
    } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
      c = handleTripleClick(KEY_SELECT);
    }
    _analogue_pin_read_millis = millis();
  }
#endif
#if defined(BACKLIGHT_BTN)
  if (millis() > next_backlight_btn_check) {
    bool touch_state = digitalRead(PIN_BUTTON2);
#if defined(DISP_BACKLIGHT)
    digitalWrite(DISP_BACKLIGHT, !touch_state);
#elif defined(EXP_PIN_BACKLIGHT)
    expander.digitalWrite(EXP_PIN_BACKLIGHT, !touch_state);
#endif
    next_backlight_btn_check = millis() + 300;
  }
#endif

  if (c != 0 && curr) {
    curr->handleInput(c);
    _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
    _next_refresh = 100;  // trigger refresh
  }

  userLedHandler();

#ifdef PIN_BUZZER
  if (buzzer.isPlaying())  buzzer.loop();
#endif

  if (curr) curr->poll();

  if (_display != NULL && _display->isOn()) {
    if (millis() >= _next_refresh && curr) {
      _display->startFrame();
      int delay_millis = curr->render(*_display);
      if (millis() < _alert_expiry) {  // render alert popup
        _display->setTextSize(1);
        int tw = _display->getTextWidth(_alert);
        int bw = tw + 16;
        if (bw > _display->width() - 8) bw = _display->width() - 8;
        int bh = 26;
        int bx = (_display->width() - bw) / 2;
        int by = (_display->height() - bh) / 2;
        uiFillRoundRect(*_display, bx, by, bw, bh, DisplayDriver::LIGHT);
        _display->setColor(DisplayDriver::DARK);
        uiTextCentered(*_display, _display->width() / 2, by + bh / 2 + 5, _alert);
        _next_refresh = _alert_expiry;   // will need refresh when alert is dismissed
      } else {
        _next_refresh = millis() + delay_millis;
      }
      _display->endFrame();
    }
#if AUTO_OFF_MILLIS > 0
#ifdef KEEP_DISPLAY_ON_USB
    // Opt-in: refresh the auto-off deadline while externally powered, so the
    // timer counts from the moment external power is removed. Off by default
    // because OLED panels burn in quickly; only enable for LCD targets or
    // where the display is replaceable.
    if (board.isExternalPowered()) {
      _auto_off = millis() + AUTO_OFF_MILLIS;
    }
#endif
    if (millis() > _auto_off) {
      _display->turnOff();
    }
#endif
  }

#ifdef PIN_VIBRATION
  vibration.loop();
#endif

#ifdef AUTO_SHUTDOWN_MILLIVOLTS
  if (millis() > next_batt_chck) {
    uint16_t milliVolts = getBattMilliVolts();
    if (milliVolts > 0 && milliVolts < AUTO_SHUTDOWN_MILLIVOLTS) {
      if(!board.isExternalPowered()) {
        if (_display != NULL) {
          int W = _display->width();
          _display->startFrame();
          _display->setColor(DisplayDriver::RED);
          _display->setTextSize(2);
          uiTextCentered(*_display, W / 2, g_ui.compact ? 8 : 56, "LOW");
          uiTextCentered(*_display, W / 2, g_ui.compact ? 26 : 82, "BATTERY");
          _display->setColor(DisplayDriver::LIGHT);  // draw box border
          _display->setTextSize(1);
          uiTextCentered(*_display, W / 2, g_ui.compact ? 48 : 104, "shutting down");
          _display->endFrame();
          if (_display->isEink() == false) { delay(3000); }
        }
        _low_batt_shutdown = true;
        shutdown();
      }
    }
    next_batt_chck = millis() + 8000;
  }
#endif
}

char UITask::checkDisplayOn(char c) {
  if (_display != NULL) {
    if (!_display->isOn()) {
      _display->turnOn();   // turn display on and consume event
      c = 0;
    }
    _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
    _next_refresh = 0;
  }
  return c;
}

char UITask::handleLongPress(char c) {
  if (millis() - ui_started_at < 8000) {   // long press in first 8 seconds since startup -> CLI/rescue
    the_mesh.enterCLIRescue();
    c = 0;   // consume event
  }
  return c;
}

char UITask::handleDoubleClick(char c) {
  MESH_DEBUG_PRINTLN("UITask: double-click triggered");
  checkDisplayOn(c);
  return c;
}

char UITask::handleTripleClick(char c) {
  MESH_DEBUG_PRINTLN("UITask: triple click triggered");
  checkDisplayOn(c);
  toggleBuzzer();
  c = 0;
  return c;
}

bool UITask::getGPSState() {
  if (_sensors != NULL) {
    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
        return !strcmp(_sensors->getSettingValue(i), "1");
      }
    }
  }
  return false;
}

void UITask::toggleGPS() {
    if (_sensors != NULL) {
      // toggle GPS on/off
    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
        if (strcmp(_sensors->getSettingValue(i), "1") == 0) {
          _sensors->setSettingValue("gps", "0");
          _node_prefs->gps_enabled = 0;
          notify(UIEventType::ack);
        } else {
          _sensors->setSettingValue("gps", "1");
          _node_prefs->gps_enabled = 1;
          notify(UIEventType::ack);
        }
        the_mesh.savePrefs();
        showAlert(_node_prefs->gps_enabled ? "GPS: Enabled" : "GPS: Disabled", 800);
        _next_refresh = 0;
        break;
      }
    }
  }
}

void UITask::toggleBuzzer() {
  // Toggle buzzer quiet mode
  #ifdef PIN_BUZZER
    if (buzzer.isQuiet()) {
      buzzer.quiet(false);
      notify(UIEventType::ack);
    } else {
      buzzer.quiet(true);
    }
    _node_prefs->buzzer_quiet = buzzer.isQuiet();
    the_mesh.savePrefs();
    showAlert(buzzer.isQuiet() ? "Buzzer: OFF" : "Buzzer: ON", 800);
    _next_refresh = 0;  // trigger refresh
  #endif
}
