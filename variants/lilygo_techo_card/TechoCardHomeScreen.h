// =============================================================================
// TechoCardHomeScreen -- 72x40 OLED home screen for LilyGo T-Echo Card
//
// Four-line layout using U8g2's 4x6 tom_thumb font (18 chars x 4 lines).
// U8g2's native SSD1306_72X40_ER support handles all GDDRAM offset mapping.
//
// Two-button navigation:  A (pin 42) = next page / long-press activate
//                         C (pin 24) = previous page
//
// Pages:  STATUS -> RADIO -> BLE -> ADVERT -> GPS -> COMPASS -> BATTERY -> HIBERNATE
// =============================================================================
#pragma once

#include <math.h>
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
    COMPASS,
    BATTERY,
    HIBERNATE,
    PAGE_COUNT
  };

  UITask* _task;
  mesh::RTCClock* _rtc;
  NodePrefs* _prefs;
  uint8_t _page;
  bool _shutdown_init;
  unsigned long _shutdown_at;

  // Compass state
  bool _compassInitDone;
  bool _compassOK;
  float _lastHeading;
  int16_t _lastMx, _lastMy, _lastMz;

  // Compass calibration state
  bool _calMode;
  unsigned long _calStart;
  uint16_t _calCount;
  int16_t _calMinX, _calMaxX;
  int16_t _calMinY, _calMaxY;
  int16_t _calMinZ, _calMaxZ;

  // Diagnostic counters (temporary)
  uint16_t _magOk;
  uint16_t _magFail;

  // Four lines at 9px spacing within 40px display.
  // U8g2 handles panel offset natively -- y=0 is the true visible top.
  static const int Y0 = 2;
  static const int Y1 = 11;
  static const int Y2 = 20;
  static const int Y3 = 29;

  int battPercent() {
    uint16_t mv = _task->getBattMilliVolts();
    if (mv == 0) return 0;
    int pct = ((int)mv - 3000) * 100 / 1160;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
  }

  const char* cardinal(float deg) {
    if (deg >= 337.5f || deg < 22.5f) return "N";
    if (deg < 67.5f) return "NE";
    if (deg < 112.5f) return "E";
    if (deg < 157.5f) return "SE";
    if (deg < 202.5f) return "S";
    if (deg < 247.5f) return "SW";
    if (deg < 292.5f) return "W";
    return "NW";
  }

public:
  TechoCardHomeScreen(UITask* task, mesh::RTCClock* rtc, NodePrefs* prefs)
    : _task(task), _rtc(rtc), _prefs(prefs),
      _page(STATUS), _shutdown_init(false), _shutdown_at(0),
      _compassInitDone(false), _compassOK(false),
      _lastHeading(0), _lastMx(0), _lastMy(0), _lastMz(0),
      _calMode(false), _calStart(0), _calCount(0),
      _calMinX(0), _calMaxX(0),
      _calMinY(0), _calMaxY(0),
      _calMinZ(0), _calMaxZ(0),
      _magOk(0), _magFail(0) {}

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

      display.setColor(DisplayDriver::YELLOW);
      display.setCursor(0, Y1);
      snprintf(tmp, sizeof(tmp), "MSG: %d", _task->getMsgCount());
      display.print(tmp);

      snprintf(tmp, sizeof(tmp), "%d%%", battPercent());
      display.drawTextRightAlign(display.width() - 1, Y1, tmp);

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
          display.setColor(DisplayDriver::LIGHT);
          display.setCursor(0, Y2);
          snprintf(tmp, sizeof(tmp), "%.4f",
                   loc->getLatitude() / 1000000.0);
          display.print(tmp);

          display.setCursor(0, Y3);
          snprintf(tmp, sizeof(tmp), "%.4f",
                   loc->getLongitude() / 1000000.0);
          display.print(tmp);
        } else {
          // No fix yet -- show NMEA sentence rate to confirm the chip is talking.
          // If this stays at 0, GPS is silent (baud rate wrong, RF off, etc).
          display.setCursor(0, Y2);
          snprintf(tmp, sizeof(tmp), "NMEA: %u/s",
                   (unsigned)gpsStream.getSentencesPerSec());
          display.print(tmp);

          display.setColor(DisplayDriver::LIGHT);
          display.setCursor(0, Y3);
          display.print("Hold A: toggle");
        }
      }
      break;
    }
#endif

    // ----- COMPASS -----
    case COMPASS: {
      if (!_compassInitDone) {
        _compassOK = board.initCompass();
        board.loadCalibration();
        _compassInitDone = true;
      }

      // --- Calibration mode ---
      if (_calMode) {
        int16_t mx, my, mz;
        if (_compassOK && board.readMag(mx, my, mz)) {
          if (_calCount == 0) {
            _calMinX = _calMaxX = mx;
            _calMinY = _calMaxY = my;
            _calMinZ = _calMaxZ = mz;
          } else {
            if (mx < _calMinX) _calMinX = mx;
            if (mx > _calMaxX) _calMaxX = mx;
            if (my < _calMinY) _calMinY = my;
            if (my > _calMaxY) _calMaxY = my;
            if (mz < _calMinZ) _calMinZ = mz;
            if (mz > _calMaxZ) _calMaxZ = mz;
          }
          _calCount++;
        }

        int spreadX = _calMaxX - _calMinX;
        int spreadY = _calMaxY - _calMinY;
        int spreadZ = _calMaxZ - _calMinZ;
        unsigned long elapsed = millis() - _calStart;
        bool adequate = (spreadX >= 100 && spreadY >= 100 && _calCount >= 150);
        bool timeout = (elapsed >= 30000);

        if (adequate || (timeout && spreadX >= 50 && spreadY >= 50)) {
          // Compute and save calibration
          CompassCalibration cal;
          cal.off_x = (_calMinX + _calMaxX) / 2;
          cal.off_y = (_calMinY + _calMaxY) / 2;
          cal.off_z = (_calMinZ + _calMaxZ) / 2;
          float avgRange = ((float)spreadX + (float)spreadY) / 2.0f;
          cal.scale_x = (spreadX > 0) ? avgRange / (float)spreadX : 1.0f;
          cal.scale_y = (spreadY > 0) ? avgRange / (float)spreadY : 1.0f;
          cal.scale_z = (spreadZ > 30) ? avgRange / (float)spreadZ : 1.0f;
          cal.magic = COMPASS_CAL_MAGIC;
          board.saveCalibration(cal);
          _calMode = false;
          _task->showAlert("Cal saved!", 800);
          return 500;
        }

        if (timeout) {
          _calMode = false;
          _task->showAlert("Try again", 800);
          return 500;
        }

        // Calibration progress display
        display.setColor(DisplayDriver::GREEN);
        display.setCursor(0, Y0);
        display.print("Calibrate");

        display.setColor(DisplayDriver::YELLOW);
        display.setCursor(0, Y1);
        display.print("Rotate slowly...");

        display.setColor(DisplayDriver::LIGHT);
        display.setCursor(0, Y2);
        snprintf(tmp, sizeof(tmp), "Samples: %u", _calCount);
        display.print(tmp);

        display.setCursor(0, Y3);
        snprintf(tmp, sizeof(tmp), "X:%d Y:%d", spreadX, spreadY);
        display.print(tmp);

        return 100; // fast sample collection
      }

      // --- Normal compass display ---
      display.setColor(DisplayDriver::GREEN);
      display.setCursor(0, Y0);
      display.print("Compass");
      if (board.isCalibrated()) {
        display.drawTextRightAlign(display.width() - 1, Y0, "CAL");
      }

      if (!_compassOK) {
        display.setColor(DisplayDriver::RED);
        display.setCursor(0, Y2);
        display.print("IMU not found");
        break;
      }

      int16_t mx, my, mz;
      if (board.readMag(mx, my, mz)) {
        _magOk++;
        // Exponential moving average: 7/8 old + 1/8 new (settles in ~2s)
        if (_magOk == 1) {
          _lastMx = mx; _lastMy = my; _lastMz = mz;
        } else {
          _lastMx = (_lastMx * 7 + mx + 4) >> 3;
          _lastMy = (_lastMy * 7 + my + 4) >> 3;
          _lastMz = (_lastMz * 7 + mz + 4) >> 3;
        }
        float cx = (float)_lastMx;
        float cy = (float)_lastMy;
        if (board.isCalibrated()) {
          const CompassCalibration& cal = board.getCalibration();
          cx = ((float)_lastMx - cal.off_x) * cal.scale_x;
          cy = ((float)_lastMy - cal.off_y) * cal.scale_y;
        }
        // Y axis is inverted relative to compass convention on this PCB
        _lastHeading = atan2f(-cy, cx) * 180.0f / (float)M_PI;
        if (_lastHeading < 0) _lastHeading += 360.0f;
      } else {
        _magFail++;
      }

      display.setColor(DisplayDriver::YELLOW);
      display.setCursor(0, Y1);
      snprintf(tmp, sizeof(tmp), "%.0f %s",
               _lastHeading, cardinal(_lastHeading));
      display.print(tmp);

      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, Y2);
      snprintf(tmp, sizeof(tmp), "X:%d Y:%d", _lastMx, _lastMy);
      display.print(tmp);

      display.setCursor(0, Y3);
      snprintf(tmp, sizeof(tmp), "Z:%d", _lastMz);
      display.print(tmp);

      return 250; // smooth readable refresh
    }

    // ----- BATTERY -----
    case BATTERY: {
      display.setColor(DisplayDriver::GREEN);
      display.setCursor(0, Y0);
      display.print("Battery");

      uint16_t mv = _task->getBattMilliVolts();
      snprintf(tmp, sizeof(tmp), "%d%%", battPercent());
      display.drawTextRightAlign(display.width() - 1, Y0, tmp);

      display.setColor(DisplayDriver::YELLOW);
      display.setCursor(0, Y1);
      snprintf(tmp, sizeof(tmp), "%d.%02dV", mv / 1000, (mv % 1000) / 10);
      display.print(tmp);

      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, Y2);
      {
        float dieTemp = board.getMCUTemperature();
        snprintf(tmp, sizeof(tmp), "Temp: %.0fC", dieTemp);
        display.print(tmp);
      }
      break;
    }

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

    // Any input during calibration cancels it
    if (_calMode) {
      _calMode = false;
      _task->showAlert("Cancelled", 500);
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

      case COMPASS:
        if (!_compassOK) return false;
        _calMode = true;
        _calStart = millis();
        _calCount = 0;
        return true;

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