#include "UITask.h"
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"
#include "donate_qr.h"
#include "MenuModel.h"
#include "target.h"
#ifdef WIFI_SSID
  #include <WiFi.h>
#endif

#ifndef AUTO_OFF_MILLIS
  #define AUTO_OFF_MILLIS     15000   // 15 seconds
#endif
#ifdef PIN_STATUS_LED
#define LED_ON_MILLIS     20
#define LED_ON_MSG_MILLIS 200
#define LED_CYCLE_MILLIS  4000
#endif

#define LONG_PRESS_MILLIS   1200

#if defined(T_ECHO_LITE_KEYPAD)
  #define TCA8418_ADDRESS       0x34
  #define TCA8418_CONFIG        0x01
  #define TCA8418_INT_STAT      0x02
  #define TCA8418_KEY_LCK_EC    0x03
  #define TCA8418_KEY_EVENT_A   0x04
  #define TCA8418_KP_GPIO1      0x1D
  #define TCA8418_KP_GPIO2      0x1E

static const char* keypadLabel(uint8_t row, uint8_t col) {
  static const char* labels[5][4] = {
    { "Yes",    "*",   "0",    "#" },
    { "No",     "7",   "8",    "9" },
    { "Down",   "4",   "5",    "6" },
    { "Center", "1",   "2",    "3" },
    { "Up",     "Esc", "Home", "Mail" }
  };
  return row < 5 && col < 4 ? labels[row][col] : "Unknown";
}
#endif

#ifndef UI_RECENT_LIST_SIZE
  #define UI_RECENT_LIST_SIZE 4
#endif

#if UI_HAS_JOYSTICK
  #define PRESS_LABEL "press Enter"
#else
  #define PRESS_LABEL "long press"
#endif

#include "icons.h"

class SplashScreen : public UIScreen {
  UITask* _task;
  char _version_info[12];

public:
  SplashScreen(UITask* task) : _task(task) {
    // strip off dash and commit hash by changing dash to null terminator
    // e.g: v1.2.3-abcdef -> v1.2.3
    const char *ver = FIRMWARE_VERSION;
    const char *dash = strchr(ver, '-');

    int len = dash ? dash - ver : strlen(ver);
    if (len >= sizeof(_version_info)) len = sizeof(_version_info) - 1;
    memcpy(_version_info, ver, len);
    _version_info[len] = 0;

  }

  int render(DisplayDriver& display) override {
    // meshcore logo
    display.setColor(DisplayDriver::BLUE);
    int logoWidth = 128;
    display.drawXbm((display.width() - logoWidth) / 2, 2, meshcore_logo, logoWidth, 13);

    // meshcore website
    const char* website = "https://meshcore.io";
    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(1);
    uint16_t websiteWidth = display.getTextWidth(website);
    display.setCursor((display.width() - websiteWidth) / 2, 25);
    display.print(website);

    // Version and build date share one line to leave breathing room below the
    // website and above the edition mark.
    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(1);
    char build_info[32];
    snprintf(build_info, sizeof(build_info), "%s  %s", _version_info, FIRMWARE_BUILD_DATE);
    display.drawTextCentered(display.width()/2, 37, build_info);

#if defined(T_ECHO_LITE_KEYPAD)
    display.setColor(DisplayDriver::GREEN);
    display.drawXbm((display.width() - 48) / 2, 44, ksk_lizdeika_logo, 48, 48);
    display.setTextSize(1);
    display.drawTextCentered(display.width() / 2, 107, "AIDAS9");
    display.drawTextCentered(display.width() / 2, 120, "LIZDEIKA EDITION");
#endif

    return 30000;
  }

  bool handleInput(char c) override {
    if (c == 0) return false;
    _task->gotoHomeScreen();
    return true;
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
    INFO,
    SHUTDOWN,
    Count    // keep as last
  };

  UITask* _task;
  mesh::RTCClock* _rtc;
  SensorManager* _sensors;
  NodePrefs* _node_prefs;
  uint8_t _page;
  bool _grid_mode;
  bool _shutdown_init;
  AdvertPath recent[UI_RECENT_LIST_SIZE];

#if UI_ICON_GRID == 1
  struct MenuItem {
    const char* label;
    const uint8_t* icon;
  };

  void renderGrid(DisplayDriver& display) {
    static const MenuItem items[] = {
      { "Messages", menu_message_icon },
      { "Nodes", menu_nodes_icon },
      { "Radio", menu_radio_icon },
      { "Bluetooth", menu_bluetooth_icon },
      { "Advert", menu_advert_icon },
#if ENV_INCLUDE_GPS == 1
      { "GPS", menu_gps_icon },
#endif
#if UI_SENSORS_PAGE == 1
      { "Sensors", menu_sensor_icon },
#endif
      { "Info", menu_info_icon },
      { "Power", menu_power_icon }
    };

    const int top = 13;
    const int cell_w = display.width() / 3;
    const int cell_h = (display.height() - top) / 3;
    display.setTextSize(1);

    for (uint8_t i = 0; i < HomePage::Count; i++) {
      int col = i % 3;
      int row = i / 3;
      int x = col * cell_w;
      int y = top + row * cell_h;
      bool selected = i == _page;

      display.setColor(selected ? DisplayDriver::LIGHT : DisplayDriver::GREEN);
      if (selected) {
        display.fillRect(x + 1, y + 1, cell_w - 2, cell_h - 2);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.drawRect(x + 1, y + 1, cell_w - 2, cell_h - 2);
      }

      display.drawXbm(x + (cell_w - 16) / 2, y + 4, items[i].icon, 16, 16);
      display.drawTextCentered(x + cell_w / 2, y + cell_h - 6, items[i].label);
    }
  }
#endif


  void renderBatteryIndicator(DisplayDriver& display, uint16_t batteryMilliVolts) {
    // Convert millivolts to percentage
#ifndef BATT_MIN_MILLIVOLTS
  #define BATT_MIN_MILLIVOLTS 3000
#endif
#ifndef BATT_MAX_MILLIVOLTS
  #define BATT_MAX_MILLIVOLTS 4200
#endif
    const int minMilliVolts = BATT_MIN_MILLIVOLTS;
    const int maxMilliVolts = BATT_MAX_MILLIVOLTS;
    int batteryPercentage = ((batteryMilliVolts - minMilliVolts) * 100) / (maxMilliVolts - minMilliVolts);
    if (batteryPercentage < 0) batteryPercentage = 0; // Clamp to 0%
    if (batteryPercentage > 100) batteryPercentage = 100; // Clamp to 100%

    // battery icon
    int iconWidth = 24;
    int iconHeight = 10;
    int iconX = display.width() - iconWidth - 5; // Position the icon near the top-right corner
    int iconY = 0;
    display.setColor(DisplayDriver::GREEN);

    // battery outline
    display.drawRect(iconX, iconY, iconWidth, iconHeight);

    // battery "cap"
    display.fillRect(iconX + iconWidth, iconY + (iconHeight / 4), 3, iconHeight / 2);

    // fill the battery based on the percentage
    int fillWidth = (batteryPercentage * (iconWidth - 4)) / 100;
    display.fillRect(iconX + 2, iconY + 2, fillWidth, iconHeight - 4);

    // show muted icon if buzzer is muted
#ifdef PIN_BUZZER
    if (_task->isBuzzerQuiet()) {
      display.setColor(DisplayDriver::RED);
      display.drawXbm(iconX - 9, iconY + 1, muted_icon, 8, 8);
    }
#endif
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
      sensors_scroll = sensors_nb > UI_RECENT_LIST_SIZE;
#if AUTO_OFF_MILLIS > 0
      next_sensors_refresh = millis() + 5000; // refresh sensor values every 5 sec
#else
      next_sensors_refresh = millis() + 60000; // refresh sensor values every 1 min
#endif
    }
  }

public:
  HomeScreen(UITask* task, mesh::RTCClock* rtc, SensorManager* sensors, NodePrefs* node_prefs)
     : _task(task), _rtc(rtc), _sensors(sensors), _node_prefs(node_prefs), _page(0), _grid_mode(true),
       _shutdown_init(false), sensors_lpp(200) {  }

  void poll() override {
    if (_shutdown_init && !_task->isButtonPressed()) {  // must wait for USR button to be released
      _task->shutdown();
    }
  }

  int render(DisplayDriver& display) override {
    char tmp[80];
    // node name
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    char filtered_name[sizeof(_node_prefs->node_name)];
    display.translateUTF8ToBlocks(filtered_name, _node_prefs->node_name, sizeof(filtered_name));
    display.setCursor(0, 0);
    display.print(filtered_name);

    // battery voltage
    renderBatteryIndicator(display, _task->getBattMilliVolts());

#if UI_ICON_GRID == 1
    if (_grid_mode) {
      renderGrid(display);
      return 30000;
    }
#endif

    // curr page indicator
    int y = 14;
    int x = display.width() / 2 - 5 * (HomePage::Count-1);
    for (uint8_t i = 0; i < HomePage::Count; i++, x += 10) {
      if (i == _page) {
        display.fillRect(x-1, y-1, 3, 3);
      } else {
        display.fillRect(x, y, 1, 1);
      }
    }

    if (_page == HomePage::FIRST) {
      display.setColor(DisplayDriver::YELLOW);
      display.setTextSize(2);
      sprintf(tmp, "MSG: %d", _task->getMsgCount());
      display.drawTextCentered(display.width() / 2, 20, tmp);

      #ifdef WIFI_SSID
        IPAddress ip = WiFi.localIP();
        snprintf(tmp, sizeof(tmp), "IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        display.setTextSize(1);
        display.drawTextCentered(display.width() / 2, 54, tmp);
      #endif
      if (_task->hasConnection()) {
        display.setColor(DisplayDriver::GREEN);
        display.setTextSize(1);
        display.drawTextCentered(display.width() / 2, 43, "< Connected >");

      } else if (the_mesh.getBLEPin() != 0) { // BT pin
        display.setColor(DisplayDriver::RED);
        display.setTextSize(2);
        sprintf(tmp, "Pin:%d", the_mesh.getBLEPin());
        display.drawTextCentered(display.width() / 2, 43, tmp);
      }
    } else if (_page == HomePage::RECENT) {
      the_mesh.getRecentlyHeard(recent, UI_RECENT_LIST_SIZE);
      display.setColor(DisplayDriver::GREEN);
      int y = 20;
      for (int i = 0; i < UI_RECENT_LIST_SIZE; i++, y += 11) {
        auto a = &recent[i];
        if (a->name[0] == 0) continue;  // empty slot
        int secs = _rtc->getCurrentTime() - a->recv_timestamp;
        if (secs < 60) {
          sprintf(tmp, "%ds", secs);
        } else if (secs < 60*60) {
          sprintf(tmp, "%dm", secs / 60);
        } else {
          sprintf(tmp, "%dh", secs / (60*60));
        }

        int timestamp_width = display.getTextWidth(tmp);
        int max_name_width = display.width() - timestamp_width - 1;

        char filtered_recent_name[sizeof(a->name)];
        display.translateUTF8ToBlocks(filtered_recent_name, a->name, sizeof(filtered_recent_name));
        display.drawTextEllipsized(0, y, max_name_width, filtered_recent_name);
        display.setCursor(display.width() - timestamp_width - 1, y);
        display.print(tmp);
      }
    } else if (_page == HomePage::RADIO) {
      display.setColor(DisplayDriver::YELLOW);
      display.setTextSize(1);
      // freq / sf
      display.setCursor(0, 20);
      sprintf(tmp, "FQ: %06.3f   SF: %d", _node_prefs->freq, _node_prefs->sf);
      display.print(tmp);

      display.setCursor(0, 31);
      sprintf(tmp, "BW: %03.2f     CR: %d", _node_prefs->bw, _node_prefs->cr);
      display.print(tmp);

      // tx power,  noise floor
      display.setCursor(0, 42);
      sprintf(tmp, "TX: %ddBm", _node_prefs->tx_power_dbm);
      display.print(tmp);
      display.setCursor(0, 53);
      sprintf(tmp, "Noise floor: %d", radio_driver.getNoiseFloor());
      display.print(tmp);
    } else if (_page == HomePage::BLUETOOTH) {
      display.setColor(DisplayDriver::GREEN);
      display.drawXbm((display.width() - 32) / 2, 18,
          _task->isSerialEnabled() ? bluetooth_on : bluetooth_off,
          32, 32);
      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, 64 - 11, "toggle: " PRESS_LABEL);
    } else if (_page == HomePage::ADVERT) {
      display.setColor(DisplayDriver::GREEN);
      display.drawXbm((display.width() - 32) / 2, 18, advert_icon, 32, 32);
      display.drawTextCentered(display.width() / 2, 64 - 11, "advert: " PRESS_LABEL);
#if ENV_INCLUDE_GPS == 1
    } else if (_page == HomePage::GPS) {
      LocationProvider* nmea = sensors.getLocationProvider();
      char buf[50];
      int y = 18;
      bool gps_state = _task->getGPSState();
#ifdef PIN_GPS_SWITCH
      bool hw_gps_state = digitalRead(PIN_GPS_SWITCH);
      if (gps_state != hw_gps_state) {
        strcpy(buf, gps_state ? "gps off(hw)" : "gps off(sw)");
      } else {
        strcpy(buf, gps_state ? "gps on" : "gps off");
      }
#else
      strcpy(buf, gps_state ? "gps on" : "gps off");
#endif
      display.drawTextLeftAlign(0, y, buf);
      if (nmea == NULL) {
        y = y + 12;
        display.drawTextLeftAlign(0, y, "Can't access GPS");
      } else {
        strcpy(buf, nmea->isValid()?"fix":"no fix");
        display.drawTextRightAlign(display.width()-1, y, buf);
        y = y + 12;
        display.drawTextLeftAlign(0, y, "sat");
        sprintf(buf, "%d", nmea->satellitesCount());
        display.drawTextRightAlign(display.width()-1, y, buf);
        y = y + 12;
        display.drawTextLeftAlign(0, y, "pos");
        sprintf(buf, "%.4f %.4f",
          nmea->getLatitude()/1000000., nmea->getLongitude()/1000000.);
        display.drawTextRightAlign(display.width()-1, y, buf);
        y = y + 12;
        display.drawTextLeftAlign(0, y, "alt");
        sprintf(buf, "%.2f", nmea->getAltitude()/1000.);
        display.drawTextRightAlign(display.width()-1, y, buf);
        y = y + 12;
      }
#endif
#if UI_SENSORS_PAGE == 1
    } else if (_page == HomePage::SENSORS) {
      int y = 18;
      refresh_sensors();
      char buf[30];
      char name[30];
      LPPReader r(sensors_lpp.getBuffer(), sensors_lpp.getSize());

      for (int i = 0; i < sensors_scroll_offset; i++) {
        uint8_t channel, type;
        r.readHeader(channel, type);
        r.skipData(type);
      }

      for (int i = 0; i < (sensors_scroll?UI_RECENT_LIST_SIZE:sensors_nb); i++) {
        uint8_t channel, type;
        if (!r.readHeader(channel, type)) { // reached end, reset
          r.reset();
          r.readHeader(channel, type);
        }

        display.setCursor(0, y);
        float v;
        switch (type) {
          case LPP_GPS: // GPS
            float lat, lon, alt;
            r.readGPS(lat, lon, alt);
            strcpy(name, "gps"); sprintf(buf, "%.4f %.4f", lat, lon);
            break;
          case LPP_VOLTAGE:
            r.readVoltage(v);
            strcpy(name, "voltage"); sprintf(buf, "%6.2f", v);
            break;
          case LPP_CURRENT:
            r.readCurrent(v);
            strcpy(name, "current"); sprintf(buf, "%.3f", v);
            break;
          case LPP_TEMPERATURE:
            r.readTemperature(v);
            strcpy(name, "temperature"); sprintf(buf, "%.2f", v);
            break;
          case LPP_RELATIVE_HUMIDITY:
            r.readRelativeHumidity(v);
            strcpy(name, "humidity"); sprintf(buf, "%.2f", v);
            break;
          case LPP_BAROMETRIC_PRESSURE:
            r.readPressure(v);
            strcpy(name, "pressure"); sprintf(buf, "%.2f", v);
            break;
          case LPP_ALTITUDE:
            r.readAltitude(v);
            strcpy(name, "altitude"); sprintf(buf, "%.0f", v);
            break;
          case LPP_POWER:
            r.readPower(v);
            strcpy(name, "power"); sprintf(buf, "%6.2f", v);
            break;
          default:
            r.skipData(type);
            strcpy(name, "unk"); sprintf(buf, "");
        }
        display.setCursor(0, y);
        display.print(name);
        display.setCursor(
          display.width()-display.getTextWidth(buf)-1, y
        );
        display.print(buf);
        y = y + 12;
      }
      if (sensors_scroll) sensors_scroll_offset = (sensors_scroll_offset+1)%sensors_nb;
      else sensors_scroll_offset = 0;
#endif
    } else if (_page == HomePage::INFO) {
      display.setColor(DisplayDriver::GREEN);
      display.setTextSize(2);
      display.drawTextCentered(display.width() / 2, 28, "MeshCore");
      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, 50, FIRMWARE_VERSION);
      display.drawTextCentered(display.width() / 2, 66, "T-Echo Lite");
    } else if (_page == HomePage::SHUTDOWN) {
      display.setColor(DisplayDriver::GREEN);
      display.setTextSize(1);
      if (_shutdown_init) {
        display.drawTextCentered(display.width() / 2, 34, "hibernating...");
      } else {
        display.drawXbm((display.width() - 32) / 2, 18, power_icon, 32, 32);
        display.drawTextCentered(display.width() / 2, 64 - 11, "hibernate:" PRESS_LABEL);
      }
    }
    return 5000;   // next render after 5000 ms
  }

  bool handleInput(char c) override {
#if UI_ICON_GRID == 1
    if (_grid_mode) {
      if (c == KEY_LEFT || c == KEY_PREV) {
        _page = (_page + HomePage::Count - 1) % HomePage::Count;
        return true;
      }
      if (c == KEY_RIGHT || c == KEY_NEXT) {
        _page = (_page + 1) % HomePage::Count;
        return true;
      }
      if (c == KEY_UP) {
        _page = (_page + HomePage::Count - 3) % HomePage::Count;
        return true;
      }
      if (c == KEY_DOWN) {
        _page = (_page + 3) % HomePage::Count;
        return true;
      }
      if (c == KEY_ENTER) {
        _grid_mode = false;
        return true;
      }
      return false;
    }
    if (c == KEY_CANCEL || c == KEY_HOME) {
      _grid_mode = true;
      return true;
    }
#endif
    if (c == KEY_LEFT || c == KEY_PREV) {
      _page = (_page + HomePage::Count - 1) % HomePage::Count;
      return true;
    }
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      _page = (_page + 1) % HomePage::Count;
      if (_page == HomePage::RECENT) {
        _task->showAlert("Recent adverts", 800);
      }
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::BLUETOOTH) {
      if (_task->isSerialEnabled()) {  // toggle Bluetooth on/off
        _task->disableSerial();
      } else {
        _task->enableSerial();
      }
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::ADVERT) {
      _task->notify(UIEventType::ack);
      if (the_mesh.advert()) {
        _task->showAlert("Advert sent!", 1000);
      } else {
        _task->showAlert("Advert failed..", 1000);
      }
      return true;
    }
#if ENV_INCLUDE_GPS == 1
    if (c == KEY_ENTER && _page == HomePage::GPS) {
      _task->toggleGPS();
      return true;
    }
#endif
#if UI_SENSORS_PAGE == 1
    if (c == KEY_ENTER && _page == HomePage::SENSORS) {
      _task->toggleGPS();
      next_sensors_refresh=0;
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

#if defined(T_ECHO_LITE_KEYPAD)
class TreeMenuScreen : public UIScreen {
  UITask* _task;
  SensorManager* _sensors;
  NodePrefs* _prefs;
  mesh::RTCClock* _rtc;
  MenuModel _menu;
  char _date_time_digits[13] = {0};
  uint8_t _date_time_field = 0;
  uint8_t _date_time_digit = 0;
  uint16_t _history_offset = 0;
  MenuModel::Page _history_page = MenuModel::Page::HOME;
  uint8_t _history_context = 0;
  bool _message_detail = false;
  uint16_t _detail_queue_index = 0;
  uint8_t _detail_scroll = 0;
  bool _compose_selected = false;
  bool _composing = false;
  char _compose_text[181] = {0};
  uint16_t _compose_length = 0;
  uint16_t _compose_cursor = 0;
  char _compose_last_key = 0;
  uint8_t _compose_cycle = 0;
  uint32_t _compose_last_press = 0;

  static const char* pageTitle(MenuModel::Page page) {
    switch (page) {
      case MenuModel::Page::MESSAGE: return "Message";
      case MenuModel::Page::CHANNELS: return "Channels";
      case MenuModel::Page::CHANNEL: return "Channel";
      case MenuModel::Page::PRIVATE_CONTACTS: return "Private";
      case MenuModel::Page::PRIVATE_CONVERSATION: return "Conversation";
      case MenuModel::Page::CONTACTS: return "Contacts";
      case MenuModel::Page::CLIENTS: return "Clients";
      case MenuModel::Page::ROOMS: return "Rooms";
      case MenuModel::Page::REPEATERS: return "Repeaters";
      case MenuModel::Page::MISC: return "Misc";
      case MenuModel::Page::CONTACT_DETAIL: return "Contact";
      case MenuModel::Page::RADIO: return "Radio";
      case MenuModel::Page::ADVERT: return "Advert";
      case MenuModel::Page::SENSORS: return "Sensors";
      case MenuModel::Page::SETTINGS: return "Settings";
      case MenuModel::Page::GPS: return "GPS";
      case MenuModel::Page::BLUETOOTH: return "Bluetooth";
      case MenuModel::Page::DATE_TIME: return "Date / Time";
      case MenuModel::Page::NOTIFICATIONS: return "Notifications";
      case MenuModel::Page::BLUE_LED: return "Blue LED";
      case MenuModel::Page::GREEN_LED: return "Green LED";
      case MenuModel::Page::LED_BEHAVIOUR: return "LED behaviour";
      case MenuModel::Page::ABOUT: return "About";
      case MenuModel::Page::DONATE: return "Donate";
      default: return "MeshCore";
    }
  }

  static const char* behaviourName(MenuModel::LedBehaviour behaviour) {
    static const char* names[] = { "off", "blink", "flicker", "triple blink", "on" };
    uint8_t index = static_cast<uint8_t>(behaviour);
    return index < 5 ? names[index] : "off";
  }

  static const char* notificationName(uint8_t index) {
    static const char* names[] = {
      "New MSG Channel", "New MSG Private", "New MSG Room", "New Channel",
      "New Client", "New Room", "New Repeater"
    };
    return index < 7 ? names[index] : "";
  }

  static uint8_t contactTypeForPage(MenuModel::Page page) {
    switch (page) {
      case MenuModel::Page::CLIENTS:
      case MenuModel::Page::PRIVATE_CONTACTS: return ADV_TYPE_CHAT;
      case MenuModel::Page::ROOMS: return ADV_TYPE_ROOM;
      case MenuModel::Page::REPEATERS: return ADV_TYPE_REPEATER;
      default: return 0xFF;
    }
  }

  bool contactAt(MenuModel::Page page, uint16_t wanted, ContactInfo& result) const {
    uint8_t type = contactTypeForPage(page);
    uint16_t found = 0;
    for (uint16_t i = 0; i < the_mesh.getNumContacts(); i++) {
      ContactInfo candidate;
      if (!the_mesh.getContactByIdx(i, candidate)) continue;
      bool matches = type == 0xFF
        ? candidate.type != ADV_TYPE_CHAT && candidate.type != ADV_TYPE_ROOM &&
          candidate.type != ADV_TYPE_REPEATER
        : candidate.type == type;
      if (matches && found++ == wanted) {
        result = candidate;
        return true;
      }
    }
    return false;
  }

  uint16_t contactCount(MenuModel::Page page) const {
    ContactInfo ignored;
    uint16_t count = 0;
    while (contactAt(page, count, ignored)) count++;
    return count;
  }

  bool channelAt(uint16_t wanted, ChannelDetails& result, uint8_t& actual_index) const {
    uint16_t found = 0;
    ChannelDetails channel;
    for (uint16_t index = 0; index < MAX_GROUP_CHANNELS; index++) {
      if (!the_mesh.getChannel(index, channel) || channel.name[0] == 0) continue;
      if (found++ == wanted) {
        result = channel;
        actual_index = index;
        return true;
      }
    }
    return false;
  }

  uint16_t channelCount() const {
    uint16_t count = 0;
    ChannelDetails channel;
    uint8_t actual_index;
    while (channelAt(count, channel, actual_index)) count++;
    return count;
  }

  uint16_t itemCount() const {
    MenuModel::Page page = _menu.state().page;
    if (page == MenuModel::Page::CHANNELS) return channelCount();
    if (page == MenuModel::Page::PRIVATE_CONTACTS ||
        page == MenuModel::Page::CLIENTS || page == MenuModel::Page::ROOMS ||
        page == MenuModel::Page::REPEATERS || page == MenuModel::Page::MISC) {
      return contactCount(page);
    }
    return MenuModel::staticItemCount(page);
  }

  void loadDateTimeEditor() {
    DateTime now(_rtc->getCurrentTime());
    snprintf(_date_time_digits, sizeof(_date_time_digits),
             "%02u%02u%04u%02u%02u",
             now.day(), now.month(), now.year(), now.hour(), now.minute());
    _date_time_field = 0;
    _date_time_digit = 0;
  }

  static bool isLeapYear(uint16_t year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  }

  bool saveDateTime() {
    uint8_t day = (_date_time_digits[0] - '0') * 10 + (_date_time_digits[1] - '0');
    uint8_t month = (_date_time_digits[2] - '0') * 10 + (_date_time_digits[3] - '0');
    uint16_t year = (_date_time_digits[4] - '0') * 1000 +
                    (_date_time_digits[5] - '0') * 100 +
                    (_date_time_digits[6] - '0') * 10 +
                    (_date_time_digits[7] - '0');
    uint8_t hour = (_date_time_digits[8] - '0') * 10 + (_date_time_digits[9] - '0');
    uint8_t minute = (_date_time_digits[10] - '0') * 10 + (_date_time_digits[11] - '0');
    static const uint8_t days_in_month[] = {
      31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    if (year < 2000 || year > 2099 || month < 1 || month > 12 ||
        hour > 23 || minute > 59) return false;
    uint8_t max_day = days_in_month[month - 1];
    if (month == 2 && isLeapYear(year)) max_day = 29;
    if (day < 1 || day > max_day) return false;
    _rtc->setCurrentTime(DateTime(year, month, day, hour, minute, 0).unixtime());
    return true;
  }

  void drawDateTimeField(DisplayDriver& display, int center_x, int baseline_y,
                         const char* value, uint8_t field) const {
    int width = display.getTextWidth(value);
    bool selected = _date_time_field == field;
    display.setColor(selected ? DisplayDriver::LIGHT : DisplayDriver::GREEN);
    if (selected) {
      display.fillRect(center_x - width / 2 - 3, baseline_y - 14, width + 6, 19);
      display.setColor(DisplayDriver::DARK);
    }
    display.drawTextCentered(center_x, baseline_y, value);
  }

  uint16_t queuedCountForChannel(uint8_t channel_idx) const {
    uint16_t count = 0;
    QueuedMessageInfo message;
    uint16_t total = the_mesh.getQueuedMessageCount();
    for (uint16_t i = 0; i < total; i++) {
      if (the_mesh.getQueuedMessage(i, message) &&
          !message.is_outgoing && message.is_channel &&
          message.channel_idx == channel_idx) count++;
    }
    return count;
  }

  uint16_t queuedCountForContact(const ContactInfo& contact) const {
    uint16_t count = 0;
    QueuedMessageInfo message;
    uint16_t total = the_mesh.getQueuedMessageCount();
    for (uint16_t i = 0; i < total; i++) {
      if (the_mesh.getQueuedMessage(i, message) && !message.is_outgoing &&
          !message.is_channel &&
          memcmp(message.pubkey_prefix, contact.id.pub_key, 6) == 0) count++;
    }
    return count;
  }

  uint16_t queuedChannelCount() const {
    uint16_t count = 0;
    ChannelDetails channel;
    uint8_t actual_index;
    for (uint16_t visible = 0; channelAt(visible, channel, actual_index); visible++) {
      count += queuedCountForChannel(actual_index);
    }
    return count;
  }

  uint16_t queuedPrivateCount() const {
    uint16_t count = 0;
    ContactInfo contact;
    for (uint16_t visible = 0;
         contactAt(MenuModel::Page::PRIVATE_CONTACTS, visible, contact);
         visible++) {
      count += queuedCountForContact(contact);
    }
    return count;
  }

  uint16_t queuedCountForContactPage(MenuModel::Page page) const {
    uint16_t count = 0;
    ContactInfo contact;
    for (uint16_t visible = 0; contactAt(page, visible, contact); visible++) {
      count += queuedCountForContact(contact);
    }
    return count;
  }

  bool queuedMessageMatches(const QueuedMessageInfo& message,
                            MenuModel::Page page, uint8_t context) const {
    if (page == MenuModel::Page::CHANNEL) {
      ChannelDetails channel;
      uint8_t actual_index;
      return channelAt(context, channel, actual_index) &&
             message.is_channel && message.channel_idx == actual_index;
    }
    ContactInfo contact;
    return !message.is_channel &&
           contactAt(MenuModel::Page::PRIVATE_CONTACTS, context, contact) &&
           memcmp(message.pubkey_prefix, contact.id.pub_key, 6) == 0;
  }

  uint16_t conversationMessageCount(MenuModel::Page page, uint8_t context) const {
    uint16_t count = 0;
    QueuedMessageInfo message;
    uint16_t total = the_mesh.getQueuedMessageCount();
    for (uint16_t i = 0; i < total; i++) {
      if (the_mesh.getQueuedMessage(i, message) &&
          queuedMessageMatches(message, page, context)) count++;
    }
    return count;
  }

  bool conversationMessageAt(MenuModel::Page page, uint8_t context,
                             uint16_t newest_offset, QueuedMessageInfo& result,
                             uint16_t* queue_index = NULL) const {
    uint16_t matched = 0;
    uint16_t total = the_mesh.getQueuedMessageCount();
    for (uint16_t i = total; i > 0; i--) {
      if (!the_mesh.getQueuedMessage(i - 1, result) ||
          !queuedMessageMatches(result, page, context)) continue;
      if (matched++ == newest_offset) {
        if (queue_index) *queue_index = i - 1;
        return true;
      }
    }
    return false;
  }

  const char* messageBody(const QueuedMessageInfo& message) const {
    if (!message.is_channel) return message.text;
    const char* separator = strstr(message.text, ": ");
    return separator ? separator + 2 : message.text;
  }

  void messageSender(const QueuedMessageInfo& message, MenuModel::Page page,
                     uint8_t context, char* sender, size_t size) const {
    if (message.is_outgoing) {
      snprintf(sender, size, "Mine");
      return;
    }
    if (message.is_channel) {
      const char* separator = strstr(message.text, ": ");
      size_t length = separator ? (size_t)(separator - message.text) : 0;
      if (length >= size) length = size - 1;
      if (length) memcpy(sender, message.text, length);
      sender[length] = 0;
      if (!length) snprintf(sender, size, "Unknown");
      return;
    }
    ContactInfo contact;
    if (contactAt(MenuModel::Page::PRIVATE_CONTACTS, context, contact)) {
      snprintf(sender, size, "%s", contact.name);
    } else {
      snprintf(sender, size, "Unknown");
    }
  }

  uint8_t wrapChatText(DisplayDriver& display, const char* text,
                       char lines[][48], uint8_t max_lines) const {
    uint8_t line_count = 0;
    const char* cursor = text;
    const int max_width = display.width() - 14;
    while (*cursor && line_count < max_lines) {
      while (*cursor == ' ') cursor++;
      if (!*cursor) break;
      size_t remaining = strlen(cursor);
      size_t take = remaining < 47 ? remaining : 47;
      while (take > 1) {
        memcpy(lines[line_count], cursor, take);
        lines[line_count][take] = 0;
        if (display.getTextWidth(lines[line_count]) <= max_width) break;
        take--;
      }
      if (take < remaining) {
        size_t break_at = take;
        while (break_at > 1 && cursor[break_at] != ' ') break_at--;
        if (break_at > 1) take = break_at;
      }
      memcpy(lines[line_count], cursor, take);
      lines[line_count][take] = 0;
      while (take > 0 && lines[line_count][take - 1] == ' ') {
        lines[line_count][--take] = 0;
      }
      cursor += take;
      while (*cursor == ' ') cursor++;
      line_count++;
    }
    return line_count ? line_count : 1;
  }

  uint8_t chatEntryHeight(DisplayDriver& display,
                          const QueuedMessageInfo& message) const {
    char lines[10][48];
    uint8_t line_count = wrapChatText(display, messageBody(message), lines, 10);
    // FreeSans9 occupies about 16 logical pixels. Advancing by 17 leaves at
    // most one blank pixel between the header and every wrapped text line.
    return 3 + (line_count + 1) * 17;
  }

  void drawChatEntry(DisplayDriver& display, const QueuedMessageInfo& message,
                     MenuModel::Page page, uint8_t context, int top,
                     bool selected) const {
    char lines[10][48];
    uint8_t line_count = wrapChatText(display, messageBody(message), lines, 10);
    int height = 3 + (line_count + 1) * 17;
    display.setColor(selected ? DisplayDriver::LIGHT : DisplayDriver::GREEN);
    if (selected) {
      display.fillRect(1, top, display.width() - 5, height);
      display.setColor(DisplayDriver::DARK);
    }
    char sender[32];
    char header[48];
    DateTime timestamp(message.timestamp);
    messageSender(message, page, context, sender, sizeof(sender));
    if (message.is_outgoing) {
      snprintf(header, sizeof(header), ":%s (%02u:%02u)",
               sender, timestamp.hour(), timestamp.minute());
      display.drawTextRightAlign(display.width() - 7, top + 15, header);
    } else {
      snprintf(header, sizeof(header), "%s (%02u:%02u):",
               sender, timestamp.hour(), timestamp.minute());
      display.drawTextEllipsized(4, top + 15, display.width() - 14, header);
    }
    for (uint8_t line = 0; line < line_count; line++) {
      display.setCursor(4, top + 32 + line * 17);
      display.print(lines[line]);
    }
  }

  uint8_t detailLineCount(const QueuedMessageInfo& message) const {
    return 7 + message.hop_count;
  }

  void detailLine(const QueuedMessageInfo& message, MenuModel::Page page,
                  uint8_t context, uint8_t line, char* text, size_t size) const {
    char sender[32];
    DateTime timestamp(message.timestamp);
    switch (line) {
      case 0:
        snprintf(text, size, "Direction: %s", message.is_outgoing ? "outgoing" : "incoming");
        break;
      case 1:
        messageSender(message, page, context, sender, sizeof(sender));
        snprintf(text, size, "Sender: %s", sender);
        break;
      case 2:
        snprintf(text, size, "%02u/%02u/%04u %02u:%02u",
                 timestamp.day(), timestamp.month(), timestamp.year(),
                 timestamp.hour(), timestamp.minute());
        break;
      case 3:
        snprintf(text, size, "Route: %s", message.path_len == 0xFF ? "direct" : "flood");
        break;
      case 4:
        snprintf(text, size, "Hops: %u", message.hop_count);
        break;
      case 5:
        if (message.snr_quarter_db) {
          snprintf(text, size, "SNR: %.2f dB", message.snr_quarter_db / 4.0f);
        } else {
          snprintf(text, size, "SNR: unavailable");
        }
        break;
      case 6:
        snprintf(text, size, "Repeats: unavailable");
        break;
      default: {
        uint8_t hop = line - 7;
        uint8_t hash_size = message.path_hash_size;
        int used = snprintf(text, size, "Hop %u: ", hop + 1);
        for (uint8_t byte = 0; byte < hash_size && used + 2 < (int)size; byte++) {
          used += snprintf(text + used, size - used, "%02X",
                           message.path[hop * hash_size + byte]);
        }
        break;
      }
    }
  }

  void renderMessageDetail(DisplayDriver& display, MenuModel::Page page,
                           uint8_t context) const {
    QueuedMessageInfo message;
    if (!the_mesh.getQueuedMessage(_detail_queue_index, message)) {
      display.setColor(DisplayDriver::GREEN);
      display.drawTextCentered(display.width() / 2, 70, "Message unavailable");
      return;
    }
    uint8_t count = detailLineCount(message);
    uint8_t first = _detail_scroll;
    if (first + 5 > count && count > 5) first = count - 5;
    for (uint8_t row = 0; row < 5 && first + row < count; row++) {
      char text[48];
      detailLine(message, page, context, first + row, text, sizeof(text));
      drawListItem(display, row, text, first + row == _detail_scroll);
    }
    drawScrollbar(display, count, _detail_scroll);
  }

  void beginComposer() {
    _composing = true;
    _compose_text[0] = 0;
    _compose_length = 0;
    _compose_cursor = 0;
    _compose_last_key = 0;
    _compose_cycle = 0;
  }

  static const char* t9Characters(char key) {
    switch (key) {
      case '0': return " 0";
      case '1': return ".,!?1";
      case '2': return "abc2";
      case '3': return "def3";
      case '4': return "ghi4";
      case '5': return "jkl5";
      case '6': return "mno6";
      case '7': return "pqrs7";
      case '8': return "tuv8";
      case '9': return "wxyz9";
      default: return "";
    }
  }

  void handleT9Key(char key) {
    const char* choices = t9Characters(key);
    uint8_t choice_count = strlen(choices);
    uint32_t now = millis();
    bool cycle = key == _compose_last_key && _compose_cursor > 0 &&
                 now - _compose_last_press < 900;
    if (cycle) {
      _compose_cycle = (_compose_cycle + 1) % choice_count;
      _compose_text[_compose_cursor - 1] = choices[_compose_cycle];
    } else if (_compose_length + 1 < sizeof(_compose_text)) {
      memmove(&_compose_text[_compose_cursor + 1],
              &_compose_text[_compose_cursor],
              _compose_length - _compose_cursor + 1);
      _compose_cycle = 0;
      _compose_text[_compose_cursor++] = choices[0];
      _compose_length++;
    }
    _compose_last_key = key;
    _compose_last_press = now;
  }

  void renderComposer(DisplayDriver& display, MenuModel::Page page) const {
    drawHeader(display, page == MenuModel::Page::CHANNEL ? "New Message" : "Reply");
    display.setColor(DisplayDriver::GREEN);
    display.drawRect(1, 21, display.width() - 2, display.height() - 22);
    display.setTextSize(3);
    const int left = 5;
    const int right = display.width() - 10;
    const int line_height = 30;
    const uint8_t visible_lines = 3;
    bool cycling = _compose_last_key != 0 &&
                   millis() - _compose_last_press < 900;
    bool cursor_visible = cycling || (millis() / 400) % 2 == 0;

    auto glyphWidth = [&display](char value) {
      char glyph[2] = {value, 0};
      int width = display.getTextWidth(glyph);
      return value == ' ' ? width + 5 : width;
    };

    // Store half-open [start, end) ranges. When possible, the breaking space
    // stays at the end of the previous line and the next word starts cleanly.
    uint16_t line_starts[181];
    uint16_t line_ends[181];
    uint16_t line_count = 0;
    uint16_t start = 0;
    while (start < _compose_length && line_count < 181) {
      int width = 0;
      uint16_t pos = start;
      uint16_t last_space = UINT16_MAX;
      while (pos < _compose_length) {
        int advance = glyphWidth(_compose_text[pos]);
        if (width + advance > right - left) break;
        width += advance;
        if (_compose_text[pos] == ' ') last_space = pos;
        pos++;
      }
      if (pos == _compose_length) {
        line_starts[line_count] = start;
        line_ends[line_count++] = pos;
        start = pos;
      } else if (last_space != UINT16_MAX && last_space >= start) {
        line_starts[line_count] = start;
        line_ends[line_count++] = last_space + 1;
        start = last_space + 1;
      } else {
        // A word wider than the field still has to be split to make progress.
        if (pos == start) pos++;
        line_starts[line_count] = start;
        line_ends[line_count++] = pos;
        start = pos;
      }
    }
    if (line_count == 0 ||
        (_compose_length > 0 && _compose_text[_compose_length - 1] == ' ')) {
      line_starts[line_count] = _compose_length;
      line_ends[line_count++] = _compose_length;
    }

    uint16_t cursor_line = line_count - 1;
    for (uint16_t line = 0; line < line_count; line++) {
      if (_compose_cursor >= line_starts[line] &&
          (_compose_cursor < line_ends[line] || line + 1 == line_count)) {
        cursor_line = line;
        break;
      }
    }
    uint16_t first_line = cursor_line >= visible_lines
        ? cursor_line - visible_lines + 1 : 0;

    for (uint8_t visible = 0;
         visible < visible_lines && first_line + visible < line_count;
         visible++) {
      uint16_t line = first_line + visible;
      int x = left;
      int baseline = 48 + visible * line_height;
      for (uint16_t i = line_starts[line]; i < line_ends[line]; i++) {
        if (i == _compose_cursor && cursor_visible) {
          display.fillRect(x, baseline - 24, 2, 27);
        }
        char glyph[2] = {_compose_text[i], 0};
        display.setCursor(x, baseline);
        display.print(glyph);
        x += glyphWidth(_compose_text[i]);
      }
      if (_compose_cursor == line_ends[line] && cursor_visible) {
        display.fillRect(x, baseline - 24, 2, 27);
      }
    }

    if (line_count > visible_lines) {
      const int track_x = display.width() - 5;
      const int track_y = 24;
      const int track_height = display.height() - track_y - 3;
      int thumb_height = track_height * visible_lines / line_count;
      if (thumb_height < 6) thumb_height = 6;
      int thumb_y = track_y;
      if (line_count > visible_lines) {
        thumb_y += (track_height - thumb_height) * first_line /
                   (line_count - visible_lines);
      }
      display.setColor(DisplayDriver::GREEN);
      display.fillRect(track_x, track_y, 1, track_height);
      display.fillRect(track_x - 1, thumb_y, 3, thumb_height);
    }
  }

  bool sendComposedMessage(MenuModel::Page page, uint8_t context) {
    if (_compose_length == 0) {
      _task->showAlert("Message is empty", 1200);
      return false;
    }
    bool sent = false;
    if (page == MenuModel::Page::CHANNEL) {
      ChannelDetails channel;
      uint8_t actual_index;
      if (channelAt(context, channel, actual_index)) {
        sent = the_mesh.sendUiChannelMessage(actual_index, _compose_text);
      }
    } else {
      ContactInfo contact;
      if (contactAt(MenuModel::Page::PRIVATE_CONTACTS, context, contact)) {
        sent = the_mesh.sendUiContactMessage(contact, _compose_text);
      }
    }
    _task->showAlert(sent ? "Message sent" : "Send failed", 1200);
    if (sent) {
      _composing = false;
      _compose_selected = true;
      _history_offset = 0;
    }
    return sent;
  }

  void drawHeader(DisplayDriver& display, const char* title) const {
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.drawTextCentered(display.width() / 2, 12, title);
    display.drawRect(0, 18, display.width(), 1);
  }

  void drawListItem(DisplayDriver& display, uint8_t row, const char* label,
                    bool selected, const char* value = NULL) const {
    int y = 32 + row * 19;
    display.setTextSize(1);
    if (selected) {
      display.setColor(DisplayDriver::LIGHT);
      display.fillRect(1, y - 13, display.width() - 5, 18);
      display.setColor(DisplayDriver::DARK);
    } else {
      display.setColor(DisplayDriver::GREEN);
    }
    display.drawTextEllipsized(4, y, value ? 78 : display.width() - 12, label);
    if (value) display.drawTextRightAlign(display.width() - 7, y, value);
  }

  void drawScrollbar(DisplayDriver& display, uint16_t count,
                     uint16_t selected, uint8_t visible_rows = 5) const {
    if (count <= visible_rows) return;
    const int x = display.width() - 3;
    const int top = 19;
    const int height = display.height() - top;
    int thumb_height = height * visible_rows / count;
    if (thumb_height < 6) thumb_height = 6;
    if (thumb_height > height) thumb_height = height;
    int thumb_y = top;
    if (count > 1) {
      thumb_y += (height - thumb_height) * selected / (count - 1);
    }
    display.setColor(DisplayDriver::GREEN);
    display.fillRect(x, top, 1, height);
    display.fillRect(x + 1, thumb_y, 2, thumb_height);
  }

  template <typename LabelFn>
  void drawList(DisplayDriver& display, uint16_t count, LabelFn label) const {
    if (count == 0) {
      display.setColor(DisplayDriver::GREEN);
      display.drawTextCentered(display.width() / 2, 74, "No items");
      return;
    }
    uint16_t selected = _menu.state().selected;
    uint16_t first = selected > 3 ? selected - 3 : 0;
    if (first + 5 > count && count > 5) first = count - 5;
    for (uint8_t row = 0; row < 5 && first + row < count; row++) {
      char text[40];
      label(first + row, text, sizeof(text));
      drawListItem(display, row, text, first + row == selected);
    }
    drawScrollbar(display, count, selected);
  }

  void drawBatteryGauge(DisplayDriver& display, int percentage) const {
    const int x = display.width() - 28;
    const int y = 1;
    const int w = 24;
    const int h = 10;
    const int inner_x = x + 2;
    const int inner_y = y + 2;
    const int inner_w = w - 4;
    const int inner_h = h - 4;
    int fill_w = inner_w * percentage / 100;

    display.setColor(DisplayDriver::GREEN);
    display.drawRect(x, y, w, h);
    display.fillRect(x + w, y + 3, 3, h - 6);
    if (fill_w > 0) {
      // The requested gauge grows from the right edge toward the left.
      display.fillRect(inner_x + inner_w - fill_w, inner_y, fill_w, inner_h);
    }

  }

  void renderHome(DisplayDriver& display) const {
    static const uint8_t* icons[] = {
      menu_fa_message_icon, menu_fa_contacts_icon, menu_fa_radio_icon,
      menu_fa_advert_icon, menu_fa_sensor_icon, menu_fa_settings_icon
    };
    static const char* labels[] = {
      "Message", "Contacts", "Radio", "Advert", "Sensors", "Settings"
    };
    char text[24];
    DateTime now(_rtc->getCurrentTime());

    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    snprintf(text, sizeof(text), "%02u/%02u %02u:%02u",
             now.day(), now.month(), now.hour(), now.minute());
    display.setCursor(0, 10);
    display.print(text);

    int battery = ((_task->getBattMilliVolts() - BATT_MIN_MILLIVOLTS) * 100) /
                  (BATT_MAX_MILLIVOLTS - BATT_MIN_MILLIVOLTS);
    if (battery < 0) battery = 0;
    if (battery > 100) battery = 100;
    drawBatteryGauge(display, battery);

    display.setColor(DisplayDriver::GREEN);
    display.drawTextEllipsized(0, 27, display.width() - 42, _prefs->node_name);
    snprintf(text, sizeof(text), "MSG:%u", the_mesh.getQueuedIncomingMessageCount());
    display.drawTextRightAlign(display.width() - 1, 27, text);
    display.drawTextCentered(display.width() / 2, 43, labels[_menu.state().selected]);
    display.drawRect(0, 47, display.width(), 1);

    display.setTextSize(1);
    for (uint8_t i = 0; i < 6; i++) {
      uint8_t col = i % 3;
      uint8_t row = i / 3;
      int x = col * display.width() / 3;
      int y = 50 + row * 40;
      int w = display.width() / 3;
      int h = 38;
      bool selected = i == _menu.state().selected;
      display.setColor(selected ? DisplayDriver::LIGHT : DisplayDriver::GREEN);
      if (selected) {
        display.fillRect(x + 1, y, w - 2, h);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.drawRect(x + 1, y, w - 2, h);
      }
      display.drawXbm(x + (w - 32) / 2, y + (h - 32) / 2, icons[i], 32, 32);
    }
  }

public:
  TreeMenuScreen(UITask* task, SensorManager* sensors, NodePrefs* prefs, mesh::RTCClock* rtc)
    : _task(task), _sensors(sensors), _prefs(prefs), _rtc(rtc) {}

  int render(DisplayDriver& display) override {
    MenuModel::Page page = _menu.state().page;
    if (page == MenuModel::Page::HOME) {
      renderHome(display);
      return 30000;
    }
    if (_message_detail &&
        (page == MenuModel::Page::CHANNEL ||
         page == MenuModel::Page::PRIVATE_CONVERSATION)) {
      drawHeader(display, "Message details");
      renderMessageDetail(display, page, _menu.state().context);
      return 30000;
    }
    if (_composing &&
        (page == MenuModel::Page::CHANNEL ||
         page == MenuModel::Page::PRIVATE_CONVERSATION)) {
      renderComposer(display, page);
      return 200;
    }
    if (page == MenuModel::Page::DONATE) {
      // A QR code needs a light quiet zone at least four modules wide. Three
      // logical pixels per module leaves six modules around this 29x29 code.
      const int module_size = 3;
      int qr_size = module_size * 29;
      int left = (display.width() - qr_size) / 2;
      int top = (display.height() - qr_size) / 2;
      // GxEPD maps the semantic DARK colour to white and LIGHT to black.
      display.setColor(DisplayDriver::DARK);
      display.fillRect(0, 0, display.width(), display.height());
      display.setColor(DisplayDriver::LIGHT);
      for (uint8_t row = 0; row < 29; row++) {
        for (uint8_t col = 0; col < 29; col++) {
          if (donate_qr_rows[row] & (1UL << col)) {
            display.fillRect(left + col * module_size, top + row * module_size,
                             module_size, module_size);
          }
        }
      }
      return 30000;
    }
    drawHeader(display, pageTitle(page));

    if (page == MenuModel::Page::MESSAGE) {
      char count[12];
      snprintf(count, sizeof(count), "%u/%u", queuedChannelCount(), channelCount());
      drawListItem(display, 0, "Channels", _menu.state().selected == 0, count);
      snprintf(count, sizeof(count), "%u/%u", queuedPrivateCount(),
               contactCount(MenuModel::Page::PRIVATE_CONTACTS));
      drawListItem(display, 1, "Private", _menu.state().selected == 1, count);
    } else if (page == MenuModel::Page::CONTACTS) {
      static const char* labels[] = { "Clients", "Rooms", "Repeaters", "Misc" };
      MenuModel::Page pages[] = { MenuModel::Page::CLIENTS, MenuModel::Page::ROOMS,
                                  MenuModel::Page::REPEATERS, MenuModel::Page::MISC };
      for (uint8_t i = 0; i < 4; i++) {
        char count[12];
        snprintf(count, sizeof(count), "%u", contactCount(pages[i]));
        drawListItem(display, i, labels[i], _menu.state().selected == i, count);
      }
    } else if (page == MenuModel::Page::CHANNELS) {
      drawList(display, channelCount(), [this](uint16_t index, char* text, size_t size) {
        ChannelDetails channel;
        uint8_t actual_index;
        if (channelAt(index, channel, actual_index)) {
          uint16_t count = queuedCountForChannel(actual_index);
          if (count) snprintf(text, size, "%s (%u)", channel.name, count);
          else snprintf(text, size, "%s", channel.name);
        }
      });
    } else if (page == MenuModel::Page::PRIVATE_CONTACTS ||
               page == MenuModel::Page::CLIENTS || page == MenuModel::Page::ROOMS ||
               page == MenuModel::Page::REPEATERS || page == MenuModel::Page::MISC) {
      drawList(display, contactCount(page), [this, page](uint16_t index, char* text, size_t size) {
        ContactInfo contact;
        if (contactAt(page, index, contact)) {
          if (page == MenuModel::Page::PRIVATE_CONTACTS) {
            uint16_t count = queuedCountForContact(contact);
            if (count) snprintf(text, size, "%s (%u)", contact.name, count);
            else snprintf(text, size, "%s", contact.name);
          } else {
            snprintf(text, size, "%s", contact.name);
          }
        }
      });
    } else if (page == MenuModel::Page::CHANNEL ||
               page == MenuModel::Page::PRIVATE_CONVERSATION) {
      uint8_t context = _menu.state().context;
      if (_history_page != page || _history_context != context) {
        _history_page = page;
        _history_context = context;
        _history_offset = 0;
        _compose_selected = false;
        _composing = false;
      }
      uint16_t count = conversationMessageCount(page, context);
      if (count == 0) _compose_selected = true;
      display.setColor(DisplayDriver::GREEN);
      if (count == 0) {
        display.drawTextCentered(display.width() / 2, 61, "No messages");
      } else {
        if (_history_offset >= count) _history_offset = count - 1;
        // Keep chat entries entirely above the final New Message/Reply row.
        // That row starts filling at y=95; allowing history to extend below it
        // makes the two selection backgrounds overlap on the e-paper display.
        const int viewport_height = 74;
        uint16_t oldest_offset = _history_offset;
        int used_height = 0;
        QueuedMessageInfo selected_message;
        if (conversationMessageAt(page, context, _history_offset, selected_message)) {
          used_height = chatEntryHeight(display, selected_message);
        }
        while (oldest_offset + 1 < count) {
          QueuedMessageInfo older;
          if (!conversationMessageAt(page, context, oldest_offset + 1, older)) break;
          int height = chatEntryHeight(display, older);
          if (used_height + height > viewport_height) break;
          oldest_offset++;
          used_height += height;
        }
        uint16_t newest_offset = _history_offset;
        while (newest_offset > 0) {
          QueuedMessageInfo newer;
          if (!conversationMessageAt(page, context, newest_offset - 1, newer)) break;
          int height = chatEntryHeight(display, newer);
          if (used_height + height > viewport_height) break;
          newest_offset--;
          used_height += height;
        }
        int top = 20;
        for (uint16_t offset = oldest_offset;; offset--) {
          QueuedMessageInfo message;
          if (conversationMessageAt(page, context, offset, message)) {
            drawChatEntry(display, message, page, context, top,
                          !_compose_selected && offset == _history_offset);
            top += chatEntryHeight(display, message);
          }
          if (offset == newest_offset || offset == 0) break;
        }
        drawScrollbar(display, count, count - 1 - _history_offset,
                      oldest_offset - newest_offset + 1);
      }
      drawListItem(display, 4,
                   page == MenuModel::Page::CHANNEL ? "New Message" : "Reply",
                   _compose_selected);
    } else if (page == MenuModel::Page::CONTACT_DETAIL) {
      ContactInfo contact;
      display.setColor(DisplayDriver::GREEN);
      if (contactAt(_menu.parentPage(), _menu.state().context, contact)) {
        display.drawTextCentered(display.width() / 2, 50, contact.name);
        char text[32];
        snprintf(text, sizeof(text), "Type: %u", contact.type);
        display.drawTextCentered(display.width() / 2, 73, text);
        snprintf(text, sizeof(text), "Path hops: %u", contact.out_path_len);
        display.drawTextCentered(display.width() / 2, 96, text);
      } else {
        display.drawTextCentered(display.width() / 2, 74, "Contact unavailable");
      }
    } else if (page == MenuModel::Page::RADIO) {
      char text[40];
      display.setColor(DisplayDriver::GREEN);
      snprintf(text, sizeof(text), "FQ %.3f  SF %u", _prefs->freq, _prefs->sf);
      display.drawTextCentered(display.width() / 2, 50, text);
      snprintf(text, sizeof(text), "BW %.2f  CR %u", _prefs->bw, _prefs->cr);
      display.drawTextCentered(display.width() / 2, 73, text);
      snprintf(text, sizeof(text), "TX %d dBm", _prefs->tx_power_dbm);
      display.drawTextCentered(display.width() / 2, 96, text);
    } else if (page == MenuModel::Page::ADVERT) {
      display.setColor(DisplayDriver::GREEN);
      display.drawTextCentered(display.width() / 2, 62, "Center/Yes:");
      display.drawTextCentered(display.width() / 2, 84, "send advert");
    } else if (page == MenuModel::Page::SENSORS) {
      char text[32];
      snprintf(text, sizeof(text), "Battery: %.2f V", _task->getBattMilliVolts() / 1000.0f);
      display.setColor(DisplayDriver::GREEN);
      display.drawTextCentered(display.width() / 2, 62, text);
      display.drawTextCentered(display.width() / 2, 84, "Sensor details next");
    } else if (page == MenuModel::Page::SETTINGS) {
      static const char* labels[] = {
        "GPS", "Bluetooth", "Date / Time", "Notifications", "About", "Donate"
      };
      uint8_t first = _menu.state().selected > 3 ? 1 : 0;
      for (uint8_t row = 0; row < 5; row++) {
        uint8_t index = first + row;
        drawListItem(display, row, labels[index], _menu.state().selected == index);
      }
      drawScrollbar(display, 6, _menu.state().selected);
    } else if (page == MenuModel::Page::GPS) {
      display.setColor(DisplayDriver::GREEN);
      display.drawTextCentered(display.width() / 2, 62, _task->getGPSState() ? "GPS: on" : "GPS: off");
      display.drawTextCentered(display.width() / 2, 84, "Center/Yes toggles");
    } else if (page == MenuModel::Page::BLUETOOTH) {
      display.setColor(DisplayDriver::GREEN);
      display.drawTextCentered(display.width() / 2, 62, _task->isSerialEnabled() ? "Bluetooth: on" : "Bluetooth: off");
      display.drawTextCentered(display.width() / 2, 84, "Center/Yes toggles");
    } else if (page == MenuModel::Page::DATE_TIME) {
      if (_date_time_digits[0] == 0) loadDateTimeEditor();
      display.setColor(DisplayDriver::GREEN);
      char field[5];
      snprintf(field, sizeof(field), "%c%c", _date_time_digits[0], _date_time_digits[1]);
      drawDateTimeField(display, 18, 47, field, 0);
      display.setColor(DisplayDriver::GREEN);
      display.drawTextCentered(display.width() / 2 - 27, 47, "/");
      snprintf(field, sizeof(field), "%c%c", _date_time_digits[2], _date_time_digits[3]);
      drawDateTimeField(display, 52, 47, field, 1);
      display.setColor(DisplayDriver::GREEN);
      display.drawTextCentered(display.width() / 2 + 5, 47, "/");
      snprintf(field, sizeof(field), "%c%c%c%c",
               _date_time_digits[4], _date_time_digits[5],
               _date_time_digits[6], _date_time_digits[7]);
      drawDateTimeField(display, 98, 47, field, 2);

      snprintf(field, sizeof(field), "%c%c", _date_time_digits[8], _date_time_digits[9]);
      drawDateTimeField(display, 45, 75, field, 3);
      display.setColor(DisplayDriver::GREEN);
      display.drawTextCentered(display.width() / 2, 75, ":");
      snprintf(field, sizeof(field), "%c%c", _date_time_digits[10], _date_time_digits[11]);
      drawDateTimeField(display, 83, 75, field, 4);

      const char* save = "Save";
      int save_width = display.getTextWidth(save);
      bool save_selected = _date_time_field == 5;
      display.setColor(save_selected ? DisplayDriver::LIGHT : DisplayDriver::GREEN);
      if (save_selected) {
        display.fillRect(display.width() / 2 - save_width / 2 - 8, 91,
                         save_width + 16, 23);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.drawRect(display.width() / 2 - save_width / 2 - 8, 91,
                         save_width + 16, 23);
      }
      display.drawTextCentered(display.width() / 2, 108, save);
    } else if (page == MenuModel::Page::NOTIFICATIONS) {
      drawListItem(display, 0, "Blue LED", _menu.state().selected == 0);
      drawListItem(display, 1, "Green LED", _menu.state().selected == 1);
    } else if (page == MenuModel::Page::BLUE_LED || page == MenuModel::Page::GREEN_LED) {
      uint8_t led = page == MenuModel::Page::GREEN_LED ? 1 : 0;
      for (uint8_t row = 0; row < 5; row++) {
        uint8_t index = (_menu.state().selected > 3 ? _menu.state().selected - 3 : 0) + row;
        if (index >= 7) break;
        drawListItem(display, row, notificationName(index), index == _menu.state().selected,
                     behaviourName(_menu.ledBehaviour(led, index)));
      }
      drawScrollbar(display, 7, _menu.state().selected);
    } else if (page == MenuModel::Page::LED_BEHAVIOUR) {
      for (uint8_t i = 0; i < 5; i++) {
        drawListItem(display, i, behaviourName(static_cast<MenuModel::LedBehaviour>(i)),
                     _menu.state().selected == i);
      }
    } else if (page == MenuModel::Page::ABOUT) {
      display.setColor(DisplayDriver::GREEN);
      display.drawTextCentered(display.width() / 2, 42, "MeshCore");
      display.drawTextCentered(display.width() / 2, 63, FIRMWARE_VERSION);
      display.drawTextCentered(display.width() / 2, 84, FIRMWARE_BUILD_DATE);
      display.drawTextCentered(display.width() / 2, 105, "T-Echo Lite");
    }
    return 30000;
  }

  bool handleInput(char c) override {
    MenuModel::Page page = _menu.state().page;
    if (page == MenuModel::Page::DATE_TIME) {
      if (c >= '0' && c <= '9') {
        static const uint8_t starts[] = { 0, 2, 4, 8, 10 };
        static const uint8_t lengths[] = { 2, 2, 4, 2, 2 };
        if (_date_time_field >= 5) return true;
        uint8_t start = starts[_date_time_field];
        uint8_t length = lengths[_date_time_field];
        _date_time_digits[start + _date_time_digit] = c;
        _date_time_digit = (_date_time_digit + 1) % length;
        return true;
      }
      if (c == KEY_UP) {
        _date_time_field = _date_time_field == 0 ? 5 : _date_time_field - 1;
        _date_time_digit = 0;
        return true;
      }
      if (c == KEY_DOWN) {
        _date_time_field = (_date_time_field + 1) % 6;
        _date_time_digit = 0;
        return true;
      }
      if (c == KEY_ENTER || c == KEY_SELECT) {
        if (_date_time_field != 5) return true;
        if (saveDateTime()) {
          _task->showAlert("Date and time saved", 1400);
        } else {
          _task->showAlert("Invalid date or time", 1400);
        }
        return true;
      }
      if (c == KEY_CANCEL) {
        _date_time_digits[0] = 0;
      }
    }
    if (page == MenuModel::Page::CHANNEL ||
        page == MenuModel::Page::PRIVATE_CONVERSATION) {
      uint16_t count = conversationMessageCount(page, _menu.state().context);
      if (_composing) {
        if (c >= '0' && c <= '9') {
          handleT9Key(c);
          return true;
        }
        if (c == KEY_UP) {
          if (_compose_cursor > 0) _compose_cursor--;
          _compose_last_key = 0;
          return true;
        }
        if (c == KEY_DOWN) {
          if (_compose_cursor < _compose_length) _compose_cursor++;
          _compose_last_key = 0;
          return true;
        }
        if (c == KEY_BACKSPACE) {
          if (_compose_cursor > 0) {
            memmove(&_compose_text[_compose_cursor - 1],
                    &_compose_text[_compose_cursor],
                    _compose_length - _compose_cursor + 1);
            _compose_cursor--;
            _compose_length--;
          }
          _compose_last_key = 0;
          return true;
        }
        if (c == KEY_CONTEXT_MENU || c == KEY_ENTER || c == KEY_SELECT) {
          sendComposedMessage(page, _menu.state().context);
          return true;
        }
        if (c == KEY_CANCEL) {
          _composing = false;
          _compose_selected = true;
          return true;
        }
        return true;
      }
      if (_message_detail) {
        QueuedMessageInfo message;
        if (!the_mesh.getQueuedMessage(_detail_queue_index, message)) {
          _message_detail = false;
          return true;
        }
        uint8_t lines = detailLineCount(message);
        if (c == KEY_UP) {
          _detail_scroll = _detail_scroll == 0 ? lines - 1 : _detail_scroll - 1;
          return true;
        }
        if (c == KEY_DOWN) {
          _detail_scroll = (_detail_scroll + 1) % lines;
          return true;
        }
        if (c == KEY_CANCEL) {
          _message_detail = false;
          return true;
        }
        if (c == KEY_HOME) {
          _message_detail = false;
          _menu.handle(MenuModel::Action::HOME);
          return true;
        }
        if (c == KEY_CONTEXT_MENU) {
          if (the_mesh.deleteQueuedMessage(_detail_queue_index)) {
            _message_detail = false;
            if (_history_offset > 0) _history_offset--;
            _task->showAlert("Message deleted", 1200);
          } else {
            _task->showAlert("Delete failed", 1200);
          }
          return true;
        }
        return true;
      }
      if (c == KEY_UP && count > 0) {
        if (_compose_selected) {
          _compose_selected = false;
          _history_offset = 0;
        } else if (_history_offset + 1 < count) {
          _history_offset++;
        }
        return true;
      }
      if (c == KEY_DOWN) {
        if (count == 0 || (!_compose_selected && _history_offset == 0)) {
          _compose_selected = true;
        } else if (!_compose_selected) {
          _history_offset--;
        }
        return true;
      }
      if ((c == KEY_ENTER || c == KEY_SELECT) && _compose_selected) {
        beginComposer();
        return true;
      }
      if ((c == KEY_ENTER || c == KEY_SELECT) && count > 0) {
        QueuedMessageInfo message;
        uint16_t queue_index;
        if (conversationMessageAt(page, _menu.state().context, _history_offset,
                                  message, &queue_index)) {
          _detail_queue_index = queue_index;
          _detail_scroll = 0;
          _message_detail = true;
        }
        return true;
      }
    }
    if ((c == KEY_ENTER || c == KEY_SELECT) && page == MenuModel::Page::ADVERT) {
      _task->notify(UIEventType::ack);
      _task->showAlert(the_mesh.advert() ? "Advert sent!" : "Advert failed..", 1200);
      return true;
    }
    if ((c == KEY_ENTER || c == KEY_SELECT) && page == MenuModel::Page::GPS) {
      _task->toggleGPS();
      return true;
    }
    if ((c == KEY_ENTER || c == KEY_SELECT) && page == MenuModel::Page::BLUETOOTH) {
      _task->isSerialEnabled() ? _task->disableSerial() : _task->enableSerial();
      return true;
    }

    MenuModel::Action action;
    if (c == KEY_UP) action = MenuModel::Action::UP;
    else if (c == KEY_DOWN) action = MenuModel::Action::DOWN;
    else if (c == KEY_LEFT || c == KEY_PREV) action = MenuModel::Action::LEFT;
    else if (c == KEY_RIGHT || c == KEY_NEXT) action = MenuModel::Action::RIGHT;
    else if (c == KEY_ENTER || c == KEY_SELECT) action = MenuModel::Action::SELECT;
    else if (c == KEY_CANCEL) action = MenuModel::Action::BACK;
    else if (c == KEY_HOME) action = MenuModel::Action::HOME;
    else if (c == KEY_CONTEXT_MENU) {
      _menu.handle(MenuModel::Action::HOME);
      _menu.handle(MenuModel::Action::SELECT);
      return true;
    } else {
      return false;
    }
    if (action == MenuModel::Action::SELECT &&
        page == MenuModel::Page::LED_BEHAVIOUR) {
      uint8_t led = _menu.parentPage() == MenuModel::Page::GREEN_LED ? 1 : 0;
      _task->configureNotificationLed(
        led, _menu.state().context,
        static_cast<MenuModel::LedBehaviour>(_menu.state().selected));
    }
    _menu.handle(action, itemCount());
    return true;
  }
};
#endif

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
      sprintf(p->origin, "(D) %s:", from_name);
    } else {
      sprintf(p->origin, "(%d) %s:", (uint32_t) path_len, from_name);
    }
    StrHelper::strncpy(p->msg, msg, sizeof(p->msg));
  }

  int render(DisplayDriver& display) override {
    char tmp[16];
    display.setCursor(2, 12);
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    sprintf(tmp, "Unread: %d", num_unread);
    display.print(tmp);

    auto p = &unread[head];

    int secs = _rtc->getCurrentTime() - p->timestamp;
    if (secs < 60) {
      sprintf(tmp, "%ds", secs);
    } else if (secs < 60*60) {
      sprintf(tmp, "%dm", secs / 60);
    } else {
      sprintf(tmp, "%dh", secs / (60*60));
    }
    display.setCursor(display.width() - display.getTextWidth(tmp) - 2, 12);
    display.print(tmp);

    display.drawRect(0, 18, display.width(), 1);  // horiz line

    display.setCursor(2, 37);
    display.setColor(DisplayDriver::YELLOW);
    char filtered_origin[sizeof(p->origin)];
    display.translateUTF8ToBlocks(filtered_origin, p->origin, sizeof(filtered_origin));
    display.print(filtered_origin);

    display.setCursor(2, 58);
    display.setColor(DisplayDriver::LIGHT);
    char filtered_msg[sizeof(p->msg)];
    display.translateUTF8ToBlocks(filtered_msg, p->msg, sizeof(filtered_msg));
    display.printWordWrap(filtered_msg, display.width());

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

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _auto_off = millis() + AUTO_OFF_MILLIS;

#if defined(PIN_USER_BTN)
  user_btn.begin();
#endif
#if defined(PIN_USER_BTN_ANA)
  analog_btn.begin();
#endif

  _node_prefs = node_prefs;

#if defined(T_ECHO_LITE_KEYPAD)
  beginKeypadDiagnostic();
  for (uint8_t led = 0; led < 2; led++) {
    for (uint8_t event = 0; event < 7; event++) {
      _notification_led_settings[led][event] = MenuModel::LedBehaviour::OFF;
    }
    MenuModel::startLedPattern(
      _notification_led_patterns[led], MenuModel::LedBehaviour::OFF, millis());
    writeNotificationLed(led, false);
  }
#endif

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
#if defined(T_ECHO_LITE_KEYPAD)
  home = new TreeMenuScreen(this, sensors, node_prefs, &rtc_clock);
#else
  home = new HomeScreen(this, &rtc_clock, sensors, node_prefs);
#endif
  msg_preview = new MsgPreviewScreen(this, &rtc_clock);
#if defined(EINK_SLOW_FULL_REFRESH) && !defined(T_ECHO_LITE_KEYPAD)
  setCurrScreen(home);
#else
  setCurrScreen(splash);
#endif
}

#if defined(T_ECHO_LITE_KEYPAD)
bool UITask::keypadWrite(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(TCA8418_ADDRESS);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

uint8_t UITask::keypadRead(uint8_t reg) {
  Wire.beginTransmission(TCA8418_ADDRESS);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0;
  if (Wire.requestFrom(TCA8418_ADDRESS, 1) != 1) return 0;
  return Wire.read();
}

void UITask::beginKeypadDiagnostic() {
  // TCA8418 key codes are 1-based: (row * 10) + column + 1.
  // Enable rows 0..4 and columns 0..3 as a keypad matrix.
  _keypad_found = keypadWrite(TCA8418_KP_GPIO1, 0x1F) &&
                  keypadWrite(TCA8418_KP_GPIO2, 0x0F) &&
                  keypadWrite(TCA8418_CONFIG, 0x01);

  Serial.println(_keypad_found
    ? "T9 diagnostic: TCA8418 found at I2C 0x34"
    : "T9 diagnostic: TCA8418 NOT FOUND at I2C 0x34");

  pinMode(PIN_BUTTON1, INPUT_PULLUP);
  pinMode(PIN_BUTTON2, INPUT_PULLUP);
  _button1_state = digitalRead(PIN_BUTTON1);
  _button2_state = digitalRead(PIN_BUTTON2);
  Serial.println("Button diagnostic: watching P0.24 and P0.18 (active LOW)");
  _next_refresh = 0;
}

void UITask::pollKeypadDiagnostic() {
  if (!_keypad_found) return;

  uint8_t count = keypadRead(TCA8418_KEY_LCK_EC) & 0x0F;
  while (count--) {
    uint8_t event = keypadRead(TCA8418_KEY_EVENT_A);
    uint8_t code = event & 0x7F;
    if (code == 0) continue;

    uint8_t row = (code - 1) / 10;
    uint8_t col = (code - 1) % 10;
    bool pressed = (event & 0x80) != 0;

    Serial.print(pressed ? "PRESS" : "RELEASE");
    Serial.print(" event=0x");
    if (event < 0x10) Serial.print('0');
    Serial.print(event, HEX);
    Serial.print(" code=");
    Serial.print(code);
    Serial.print(" row=");
    Serial.print(row);
    Serial.print(" col=");
    Serial.print(col);
    Serial.print(" label=");
    Serial.println(keypadLabel(row, col));

    // Keep the press visible on e-paper; releases remain available on serial.
    if (pressed) {
      _keypad_has_event = true;
      _diagnostic_is_gpio = false;
      _keypad_event = event;
      _keypad_code = code;
      _keypad_row = row;
      _keypad_col = col;
      _next_refresh = 0;
      char key = 0;
      switch (code) {
        case 1:  key = KEY_ENTER; break;       // Yes
        case 11: key = KEY_BACKSPACE; break;   // No / X
        case 21: key = KEY_DOWN; break;
        case 31: key = KEY_ENTER; break;       // Center
        case 41: key = KEY_UP; break;
        case 42: key = KEY_CANCEL; break;      // Esc
        case 43: key = KEY_HOME; break;
        case 44: key = KEY_CONTEXT_MENU; break; // Mail
        case 3:  key = '0'; break;
        case 12: key = '7'; break;
        case 13: key = '8'; break;
        case 14: key = '9'; break;
        case 22: key = '4'; break;
        case 23: key = '5'; break;
        case 24: key = '6'; break;
        case 32: key = '1'; break;
        case 33: key = '2'; break;
        case 34: key = '3'; break;
        default: break;
      }
      // Every physical keypad key dismisses the splash. Outside the splash,
      // only keys with an explicit navigation mapping are forwarded.
      if (key != 0) queueKey(key);
      else if (curr == splash) queueKey(KEY_SELECT);
    }
  }
  keypadWrite(TCA8418_INT_STAT, 0x01);
}

void UITask::queueKey(char key) {
  if (key == 0) return;
  uint8_t next = (_key_queue_head + 1) % KEY_QUEUE_SIZE;
  if (next == _key_queue_tail) {
    // Preserve the newest physical presses if the consumer ever falls behind.
    _key_queue_tail = (_key_queue_tail + 1) % KEY_QUEUE_SIZE;
  }
  _key_queue[_key_queue_head] = key;
  _key_queue_head = next;
}

bool UITask::dequeueKey(char& key) {
  if (_key_queue_tail == _key_queue_head) return false;
  key = _key_queue[_key_queue_tail];
  _key_queue_tail = (_key_queue_tail + 1) % KEY_QUEUE_SIZE;
  return true;
}

void UITask::pollDiagnosticButtons() {
  bool button1 = digitalRead(PIN_BUTTON1);
  bool button2 = digitalRead(PIN_BUTTON2);

  if (button1 != _button1_state) {
    _button1_state = button1;
    Serial.println(button1 ? "RELEASE GPIO=P0.24 code=24" : "PRESS GPIO=P0.24 code=24");
    if (!button1) {
      _keypad_has_event = true;
      _diagnostic_is_gpio = true;
      _diagnostic_gpio = 24;
      queueKey(KEY_HOME);
      _next_refresh = 0;
    }
  }
  if (button2 != _button2_state) {
    _button2_state = button2;
    Serial.println(button2 ? "RELEASE GPIO=P0.18 code=18" : "PRESS GPIO=P0.18 code=18");
    if (!button2) {
      _keypad_has_event = true;
      _diagnostic_is_gpio = true;
      _diagnostic_gpio = 18;
      if (curr == splash) queueKey(KEY_SELECT);
      _next_refresh = 0;
    }
  }
}

void UITask::renderKeypadDiagnostic() {
  char line[32];
  _display->setColor(DisplayDriver::LIGHT);
  _display->setTextSize(2);
  _display->drawTextCentered(_display->width() / 2, 14, "T9 KEY TEST");
  _display->setTextSize(1);

  if (!_keypad_found && !_keypad_has_event) {
    _display->drawTextCentered(_display->width() / 2, 42, "TCA8418 not found");
    _display->drawTextCentered(_display->width() / 2, 57, "buttons still active");
    return;
  }
  if (!_keypad_has_event) {
    _display->drawTextCentered(_display->width() / 2, 44, "Press any key");
    _display->drawTextCentered(_display->width() / 2, 60, "I2C: 0x34");
    return;
  }

  if (_diagnostic_is_gpio) {
    _display->drawTextCentered(_display->width() / 2, 35, "Bottom button");
    snprintf(line, sizeof(line), "GPIO: P0.%02u", _diagnostic_gpio);
    _display->drawTextCentered(_display->width() / 2, 52, line);
    snprintf(line, sizeof(line), "Code: %u  0x%02X", _diagnostic_gpio, _diagnostic_gpio);
    _display->drawTextCentered(_display->width() / 2, 69, line);
    return;
  }

  snprintf(line, sizeof(line), "Key: %s", keypadLabel(_keypad_row, _keypad_col));
  _display->drawTextCentered(_display->width() / 2, 35, line);
  snprintf(line, sizeof(line), "Code: %u  0x%02X", _keypad_code, _keypad_code);
  _display->drawTextCentered(_display->width() / 2, 50, line);
  snprintf(line, sizeof(line), "Event: 0x%02X", _keypad_event);
  _display->drawTextCentered(_display->width() / 2, 65, line);
  snprintf(line, sizeof(line), "Matrix: R%u C%u", _keypad_row, _keypad_col);
  _display->drawTextCentered(_display->width() / 2, 80, line);
}
#endif

void UITask::showAlert(const char* text, int duration_millis) {
  strcpy(_alert, text);
  _alert_expiry = millis() + duration_millis;
}

void UITask::notify(UIEventType t) {
#if defined(T_ECHO_LITE_KEYPAD)
  switch (t) {
    case UIEventType::channelMessage:    triggerNotificationLeds(0); break;
    case UIEventType::contactMessage:    triggerNotificationLeds(1); break;
    case UIEventType::roomMessage:       triggerNotificationLeds(2); break;
    case UIEventType::newContactMessage: triggerNotificationLeds(4); break;
    default: break;
  }
#endif

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

#if defined(T_ECHO_LITE_KEYPAD)
void UITask::writeNotificationLed(uint8_t led, bool on) {
  int pin = led == 0 ? LED_BLUE : LED_GREEN;
  digitalWrite(pin, on ? LED_STATE_ON : !LED_STATE_ON);
}

void UITask::configureNotificationLed(
    uint8_t led, uint8_t event, MenuModel::LedBehaviour behaviour) {
  if (led >= 2 || event >= 7 || behaviour >= MenuModel::LedBehaviour::COUNT) return;
  _notification_led_settings[led][event] = behaviour;
  MenuModel::startLedPattern(_notification_led_patterns[led], behaviour, millis());
  writeNotificationLed(led, _notification_led_patterns[led].output_on);
}

void UITask::triggerNotificationLeds(uint8_t event) {
  if (event >= 7) return;
  uint32_t now = millis();
  for (uint8_t led = 0; led < 2; led++) {
    MenuModel::startLedPattern(
      _notification_led_patterns[led], _notification_led_settings[led][event], now);
    writeNotificationLed(led, _notification_led_patterns[led].output_on);
  }
}

void UITask::notificationLedHandler() {
  uint32_t now = millis();
  for (uint8_t led = 0; led < 2; led++) {
    if (MenuModel::updateLedPattern(_notification_led_patterns[led], now)) {
      writeNotificationLed(led, _notification_led_patterns[led].output_on);
    }
  }
}
#endif


void UITask::msgRead(int msgcount) {
  _msgcount = msgcount;
  _next_refresh = 0;
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) {
  _msgcount = msgcount;

  ((MsgPreviewScreen *) msg_preview)->addPreview(path_len, from_name, text);

  if (_display != NULL) {
    if (!_display->isOn() && !hasConnection()) {
      _display->turnOn();
    }
    if (_display->isOn()) {
      _auto_off = millis() + AUTO_OFF_MILLIS;
      _next_refresh = 100;
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
#if defined(T_ECHO_LITE_KEYPAD)
  pollKeypadDiagnostic();
  pollDiagnosticButtons();
  // Drain every buffered press before a potentially blocking e-paper update.
  // This keeps multi-tap T9 timing independent of display refresh latency.
  while (dequeueKey(c)) {
    if (curr) curr->handleInput(c);
    _auto_off = millis() + AUTO_OFF_MILLIS;
    _next_refresh = 100;
  }
#endif
#if !defined(T_ECHO_LITE_KEYPAD)
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
#endif

#if !defined(T_ECHO_LITE_KEYPAD)
  if (c != 0 && curr) {
    curr->handleInput(c);
    _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
    _next_refresh = 100;  // trigger refresh
  }
#endif

#if defined(T_ECHO_LITE_KEYPAD)
  notificationLedHandler();
#else
  userLedHandler();
#endif

#ifdef PIN_BUZZER
  if (buzzer.isPlaying())  buzzer.loop();
#endif

  if (curr) curr->poll();

  if (_display != NULL && _display->isOn()) {
    if (millis() >= _next_refresh && curr) {
      _display->startFrame();
#ifdef T9_KEYBOARD_DIAGNOSTIC
      renderKeypadDiagnostic();
      _next_refresh = millis() + 60000;
#else
      int delay_millis = curr->render(*_display);
      if (millis() < _alert_expiry) {  // render alert popup
        _display->setTextSize(1);
        int y = _display->height() / 3;
        int p = _display->height() / 32;
        _display->setColor(DisplayDriver::DARK);
        _display->fillRect(p, y, _display->width() - p*2, y);
        _display->setColor(DisplayDriver::LIGHT);  // draw box border
        _display->drawRect(p, y, _display->width() - p*2, y);
        _display->drawTextCentered(_display->width() / 2, y + p*3, _alert);
        _next_refresh = _alert_expiry;   // will need refresh when alert is dismissed
      } else {
        _next_refresh = millis() + delay_millis;
      }
#endif
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
          _display->startFrame();
          _display->setTextSize(2);
          _display->setColor(DisplayDriver::RED);
          _display->drawTextCentered(_display->width() / 2, 56, "Low Battery.");
          _display->drawTextCentered(_display->width() / 2, 82, "Shutting Down!");
          _display->endFrame();
          if (_display->isEink() == false) { delay(3000); }
        }
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
    _next_refresh = 0;  // trigger refresh
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
