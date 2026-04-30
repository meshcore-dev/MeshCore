// =============================================================================
// TechoCardHomeScreen — 72×40 OLED home screen for LilyGo T-Echo Card
//
// Four-line layout using U8g2's 4×6 tom_thumb font (18 chars × 4 lines).
// U8g2's native SSD1306_72X40_ER support handles all GDDRAM offset mapping.
//
// Two-button navigation:  A (pin 42) = next page / long-press activate
//                         C (pin 24) = previous page
//
// Pages:  STATUS → RADIO → BLE → ADVERT → GPS → HIBERNATE
// =============================================================================
#pragma once

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/sensors/LocationProvider.h>
#include <target.h>
#include "MyMesh.h"
#include "UITask.h"

class TechoCardHomeScreen : public UIScreen {
  enum Page {
    STATUS,
    RADIO,
#ifdef BLE_PIN_CODE
    BLE,
#endif
    ADVERT,
#if ENV_INCLUDE_GPS == 1
    GPS,
#endif
    HIBERNATE,
    PAGE_COUNT
  };

  UITask* _task;
  mesh::RTCClock* _rtc;
  NodePrefs* _prefs;
  uint8_t _page;
  bool _shutdown_init;
  unsigned long _shutdown_at;

  // Four lines at 9px spacing within 40px display.
  // U8g2 handles panel offset natively — y=0 is the true visible top.
  static const int Y0 = 2;
  static const int Y1 = 11;
  static const int Y2 = 20;
  static const int Y3 = 29;

  int battPercent() {
    uint16_t mv = _task->getBattMilliVolts();
    if (mv == 0) return 0;
    int pct = ((int)mv - 3000) * 100 / 1200;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
  }

public:
  TechoCardHomeScreen(UITask* task, mesh::RTCClock* rtc, NodePrefs* prefs)
    : _task(task), _rtc(rtc), _prefs(prefs),
      _page(STATUS), _shutdown_init(false), _shutdown_at(0) {}

  void cancelEditing() { _shutdown_init = false; }

  int render(DisplayDriver& display) override {
    char tmp[32];
    display.setTextSize(1);

    switch (_page) {

    // ----- STATUS -----
    case STATUS: {
      display.setColor(DisplayDriver::GREEN);
      display.setCursor(0, Y0);
      char filtered_name[sizeof(_prefs->node_name)];
      display.translateUTF8ToBlocks(filtered_name, _prefs->node_name,
                                    sizeof(filtered_name));
      display.print(filtered_name);

      snprintf(tmp, sizeof(tmp), "%d%%", battPercent());
      display.drawTextRightAlign(display.width() - 1, Y0, tmp);

      display.setColor(DisplayDriver::YELLOW);
      display.setCursor(0, Y1);
      snprintf(tmp, sizeof(tmp), "MSG: %d", _task->getMsgCount());
      display.print(tmp);

      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, Y2);
      if (_task->hasConnection()) {
        display.print("Connected");
      } else if (_task->isSerialEnabled()) {
        display.print("BLE: On");
      } else {
        display.print("BLE: Off");
      }
      break;
    }

    // ----- RADIO -----
    case RADIO: {
      display.setColor(DisplayDriver::YELLOW);
      display.setCursor(0, Y0);
      snprintf(tmp, sizeof(tmp), "%.1f MHz  SF%d",
               _prefs->freq, _prefs->sf);
      display.print(tmp);

      display.setCursor(0, Y1);
      snprintf(tmp, sizeof(tmp), "BW %.0f  CR %d",
               _prefs->bw, _prefs->cr);
      display.print(tmp);

      display.setCursor(0, Y2);
      snprintf(tmp, sizeof(tmp), "TX: %d dBm",
               _prefs->tx_power_dbm);
      display.print(tmp);

      display.setCursor(0, Y3);
      snprintf(tmp, sizeof(tmp), "NF: %d",
               radio_driver.getNoiseFloor());
      display.print(tmp);
      break;
    }

#ifdef BLE_PIN_CODE
    // ----- BLE -----
    case BLE: {
      display.setColor(DisplayDriver::GREEN);
      display.setCursor(0, Y0);
      display.print(_task->isSerialEnabled() ? "BLE: ON" : "BLE: OFF");

      display.setColor(DisplayDriver::YELLOW);
      display.setCursor(0, Y1);
      snprintf(tmp, sizeof(tmp), "PIN: %lu",
               (unsigned long)the_mesh.getBLEPin());
      display.print(tmp);

      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, Y3);
      display.print("Hold A: toggle");
      break;
    }
#endif

    // ----- ADVERT -----
    case ADVERT: {
      display.setColor(DisplayDriver::GREEN);
      display.setCursor(0, Y0);
      display.print("Advert");

      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, Y2);
      display.print("Hold A: send");
      break;
    }

#if ENV_INCLUDE_GPS == 1
    // ----- GPS -----
    case GPS: {
      LocationProvider* loc = sensors.getLocationProvider();

      display.setColor(DisplayDriver::GREEN);
      display.setCursor(0, Y0);
      if (!_prefs->gps_enabled) {
        display.print("GPS: OFF");
        display.setColor(DisplayDriver::LIGHT);
        display.setCursor(0, Y2);
        display.print("Hold A: toggle");
        break;
      }

      display.print("GPS: ON");
      if (loc) {
        snprintf(tmp, sizeof(tmp), "S: %d",
                 loc->satellitesCount());
        display.drawTextRightAlign(display.width() - 1, Y0, tmp);

        display.setColor(DisplayDriver::YELLOW);
        display.setCursor(0, Y1);
        display.print(loc->isValid() ? "Fix: 3D" : "No fix");

        if (loc->isValid()) {
          display.setCursor(0, Y2);
          snprintf(tmp, sizeof(tmp), "%.4f, %.4f",
                   loc->getLatitude() / 1000000.0,
                   loc->getLongitude() / 1000000.0);
          display.print(tmp);
        }
      }

      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, Y3);
      display.print("Hold A: toggle");
      break;
    }
#endif

    // ----- HIBERNATE -----
    case HIBERNATE: {
      if (_shutdown_init) {
        display.setColor(DisplayDriver::RED);
        display.setCursor(0, Y1);
        display.print("Shutting down...");
        return 200;
      }

      display.setColor(DisplayDriver::YELLOW);
      display.setCursor(0, Y0);
      display.print("Hibernate");

      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, Y2);
      display.print("Hold A: sleep");
      break;
    }
    } // switch

    return 5000;
  }

  bool handleInput(char c) override {
    if (_shutdown_init) {
      _shutdown_init = false;
      return true;
    }

    if (c == KEY_NEXT || c == 'd') {
      _page = (_page + 1) % PAGE_COUNT;
      return true;
    }
    if (c == KEY_PREV || c == 'a') {
      _page = (_page + PAGE_COUNT - 1) % PAGE_COUNT;
      return true;
    }

    if (c == KEY_ENTER) {
      switch (_page) {
#ifdef BLE_PIN_CODE
      case BLE:
        if (_task->isSerialEnabled()) {
          _task->disableSerial();
          _task->showAlert("BLE Off", 800);
        } else {
          _task->enableSerial();
          _task->showAlert("BLE On", 800);
        }
        return true;
#endif

      case ADVERT:
        _task->notify(UIEventType::ack);
        if (the_mesh.advert()) {
          _task->showAlert("Sent!", 800);
        } else {
          _task->showAlert("Failed", 800);
        }
        return true;

#if ENV_INCLUDE_GPS == 1
      case GPS:
        _task->toggleGPS();
        return true;
#endif

      case HIBERNATE:
        _shutdown_init = true;
        _shutdown_at = millis() + 500;
        return true;

      default:
        return false;
      }
    }

    return false;
  }

  void poll() override {
    if (_shutdown_init && millis() >= _shutdown_at) {
      if (!_task->isButtonPressed()) {
        _task->shutdown();
      }
    }
  }
};