#include "UITask.h"
#include "target.h"
#include <Arduino.h>
#include <helpers/CommonCLI.h>
#include "MyMesh.h"

#ifndef USER_BTN_PRESSED
#define USER_BTN_PRESSED LOW
#endif

#ifndef AUTO_OFF_MILLIS
#define AUTO_OFF_MILLIS      20000  // 20 seconds (0 = never auto-off, e.g. mains/solar repeaters)
#endif
#define BOOT_SCREEN_MILLIS   4000   // 4 seconds

#define POWEROFF_DELAY 3000

#define EINK_REFRESH_MILLIS  30000  // e-ink: slow + wears with use, and CRC-gated in the driver
#define NUM_EINK_PAGES       3      // Status / Radio / Traffic

// battery voltage -> percent range (override per-variant if needed)
#ifndef BATT_MIN_MILLIVOLTS
#define BATT_MIN_MILLIVOLTS  3000
#endif
#ifndef BATT_MAX_MILLIVOLTS
#define BATT_MAX_MILLIVOLTS  4200
#endif

// 'meshcore', 128x13px
static const uint8_t meshcore_logo [] PROGMEM = {
    0x3c, 0x01, 0xe3, 0xff, 0xc7, 0xff, 0x8f, 0x03, 0x87, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe,
    0x3c, 0x03, 0xe3, 0xff, 0xc7, 0xff, 0x8e, 0x03, 0x8f, 0xfe, 0x3f, 0xfe, 0x1f, 0xff, 0x1f, 0xfe,
    0x3e, 0x03, 0xc3, 0xff, 0x8f, 0xff, 0x0e, 0x07, 0x8f, 0xfe, 0x7f, 0xfe, 0x1f, 0xff, 0x1f, 0xfc,
    0x3e, 0x07, 0xc7, 0x80, 0x0e, 0x00, 0x0e, 0x07, 0x9e, 0x00, 0x78, 0x0e, 0x3c, 0x0f, 0x1c, 0x00,
    0x3e, 0x0f, 0xc7, 0x80, 0x1e, 0x00, 0x0e, 0x07, 0x1e, 0x00, 0x70, 0x0e, 0x38, 0x0f, 0x3c, 0x00,
    0x7f, 0x0f, 0xc7, 0xfe, 0x1f, 0xfc, 0x1f, 0xff, 0x1c, 0x00, 0x70, 0x0e, 0x38, 0x0e, 0x3f, 0xf8,
    0x7f, 0x1f, 0xc7, 0xfe, 0x0f, 0xff, 0x1f, 0xff, 0x1c, 0x00, 0xf0, 0x0e, 0x38, 0x0e, 0x3f, 0xf8,
    0x7f, 0x3f, 0xc7, 0xfe, 0x0f, 0xff, 0x1f, 0xff, 0x1c, 0x00, 0xf0, 0x1e, 0x3f, 0xfe, 0x3f, 0xf0,
    0x77, 0x3b, 0x87, 0x00, 0x00, 0x07, 0x1c, 0x0f, 0x3c, 0x00, 0xe0, 0x1c, 0x7f, 0xfc, 0x38, 0x00,
    0x77, 0xfb, 0x8f, 0x00, 0x00, 0x07, 0x1c, 0x0f, 0x3c, 0x00, 0xe0, 0x1c, 0x7f, 0xf8, 0x38, 0x00,
    0x73, 0xf3, 0x8f, 0xff, 0x0f, 0xff, 0x1c, 0x0e, 0x3f, 0xf8, 0xff, 0xfc, 0x70, 0x78, 0x7f, 0xf8,
    0xe3, 0xe3, 0x8f, 0xff, 0x1f, 0xfe, 0x3c, 0x0e, 0x3f, 0xf8, 0xff, 0xfc, 0x70, 0x3c, 0x7f, 0xf8,
    0xe3, 0xe3, 0x8f, 0xff, 0x1f, 0xfc, 0x3c, 0x0e, 0x1f, 0xf8, 0xff, 0xf8, 0x70, 0x3c, 0x7f, 0xf8,
};

static int battPercent(uint16_t mv) {
  int pct = ((int)mv - BATT_MIN_MILLIVOLTS) * 100 / (BATT_MAX_MILLIVOLTS - BATT_MIN_MILLIVOLTS);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

// human-readable uptime at minute granularity (keeps e-ink physical updates to ~1/min)
static void fmtUptime(char* out, size_t n, uint32_t secs) {
  uint32_t d = secs / 86400; secs %= 86400;
  uint32_t h = secs / 3600;  secs %= 3600;
  uint32_t m = secs / 60;
  if (d)      snprintf(out, n, "%lud %luh %lum", (unsigned long)d, (unsigned long)h, (unsigned long)m);
  else if (h) snprintf(out, n, "%luh %lum", (unsigned long)h, (unsigned long)m);
  else        snprintf(out, n, "%lum", (unsigned long)m);
}

void UITask::begin(MyMesh* mesh, NodePrefs* node_prefs, const char* build_date, const char* firmware_version) {
  _mesh = mesh;
  _prevBtnState = HIGH;
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _started_at = millis();
  _node_prefs = node_prefs;
  _is_eink = _display->isEink();
  _page = 0;
  _display->turnOn();

#if defined(PIN_USER_BTN) && defined(DISPLAY_CLASS)
  user_btn.begin();
#endif

  // strip off dash and commit hash by changing dash to null terminator
  // e.g: v1.2.3-abcdef -> v1.2.3
  char *version = strdup(firmware_version);
  char *dash = strchr(version, '-');
  if(dash){
    *dash = 0;
  }

  // v1.2.3 (1 Jan 2025)
  snprintf(_version_info, sizeof(_version_info), "%s (%s)", version, build_date);
  free(version);
}

// Rich, multi-page dashboard for large e-ink panels (e.g. E290 / E213).
void UITask::renderRichScreen() {
  static const char* PAGE_NAMES[NUM_EINK_PAGES] = { "Status", "Radio", "Traffic" };
  char line[64];
  RepeaterStats st;
  _mesh->getStats(st);

  const int w = _display->width();
  const int dy = 13;
  int y;

  int batt_pct = battPercent(st.batt_milli_volts);

  // header: node name (large) + battery percent (top-right)
  _display->setColor(UIColor::primary_txt);
  _display->setTextSize(2);
  _display->drawTextEllipsized(2, 0, w - 52, _node_prefs->node_name);

  _display->setTextSize(1);
  snprintf(line, sizeof(line), "%d%%", batt_pct);
  _display->drawTextRightAlign(w - 2, 2, line);

  // subtitle: role + page indicator
  _display->setColor(UIColor::secondary_txt);
  snprintf(line, sizeof(line), "Repeater   [%d/%d] %s", _page + 1, (int)NUM_EINK_PAGES, PAGE_NAMES[_page]);
  _display->drawTextLeftAlign(2, 20, line);

  // divider
  _display->fillRect(0, 31, w, 1);

  _display->setColor(UIColor::primary_txt);
  y = 38;
  if (_page == 0) {          // ---------- Status ----------
    snprintf(line, sizeof(line), "FW: %s", _version_info);
    _display->drawTextLeftAlign(4, y, line); y += dy;

    char up[24];
    fmtUptime(up, sizeof(up), st.total_up_time_secs);
    snprintf(line, sizeof(line), "Uptime: %s", up);
    _display->drawTextLeftAlign(4, y, line); y += dy;

    snprintf(line, sizeof(line), "Battery: %u mV (%d%%)", st.batt_milli_volts, batt_pct);
    _display->drawTextLeftAlign(4, y, line); y += dy;

    snprintf(line, sizeof(line), "Freq: %.3f MHz  SF%u", _node_prefs->freq, _node_prefs->sf);
    _display->drawTextLeftAlign(4, y, line); y += dy;

    snprintf(line, sizeof(line), "BW: %.2f kHz  CR: 4/%u", _node_prefs->bw, _node_prefs->cr);
    _display->drawTextLeftAlign(4, y, line); y += dy;

    snprintf(line, sizeof(line), "TX: %d dBm  Neighbours: %lu",
             (int)_node_prefs->tx_power_dbm, (unsigned long)_mesh->getNumNeighbours());
    _display->drawTextLeftAlign(4, y, line); y += dy;

  } else if (_page == 1) {   // ---------- Radio ----------
    snprintf(line, sizeof(line), "Last RSSI: %d dBm", st.last_rssi);
    _display->drawTextLeftAlign(4, y, line); y += dy;

    snprintf(line, sizeof(line), "Last SNR: %.2f dB", st.last_snr / 4.0f);
    _display->drawTextLeftAlign(4, y, line); y += dy;

    snprintf(line, sizeof(line), "Noise floor: %d dBm", st.noise_floor);
    _display->drawTextLeftAlign(4, y, line); y += dy;

    snprintf(line, sizeof(line), "TX queue: %u", st.curr_tx_queue_len);
    _display->drawTextLeftAlign(4, y, line); y += dy;

    snprintf(line, sizeof(line), "Recv errors: %lu", (unsigned long)st.n_recv_errors);
    _display->drawTextLeftAlign(4, y, line); y += dy;

  } else {                   // ---------- Traffic ----------
    snprintf(line, sizeof(line), "Rx: %lu  (F:%lu D:%lu)",
             (unsigned long)st.n_packets_recv,
             (unsigned long)st.n_recv_flood, (unsigned long)st.n_recv_direct);
    _display->drawTextLeftAlign(4, y, line); y += dy;

    snprintf(line, sizeof(line), "Tx: %lu  (F:%lu D:%lu)",
             (unsigned long)st.n_packets_sent,
             (unsigned long)st.n_sent_flood, (unsigned long)st.n_sent_direct);
    _display->drawTextLeftAlign(4, y, line); y += dy;

    snprintf(line, sizeof(line), "Airtime  tx:%lus  rx:%lus",
             (unsigned long)st.total_air_time_secs, (unsigned long)st.total_rx_air_time_secs);
    _display->drawTextLeftAlign(4, y, line); y += dy;

    snprintf(line, sizeof(line), "Dups F:%u D:%u  Err:%u",
             st.n_flood_dups, st.n_direct_dups, st.err_events);
    _display->drawTextLeftAlign(4, y, line); y += dy;
  }
}

void UITask::renderCurrScreen() {
  char tmp[80];
  if (millis() < _started_at + BOOT_SCREEN_MILLIS) { // boot screen
    // meshcore logo
    _display->setColor(UIColor::corp_blue);
    int logoWidth = 128;
    _display->drawXbm((_display->width() - logoWidth) / 2, 3, meshcore_logo, logoWidth, 13);

    // meshcore website
    const char* website = "https://meshcore.io";
    _display->setColor(UIColor::primary_txt);
    _display->setTextSize(1);
    _display->drawTextCentered(_display->width() / 2, 22, website);

    // version info
    _display->setTextSize(1);
    _display->drawTextCentered(_display->width() / 2, 35, _version_info);

    // node type
    const char* node_type = "< Repeater >";
    _display->drawTextCentered(_display->width() / 2, 48, node_type);
  } else if (_powering_off_at > 0) {
    // meshcore logo
    _display->setColor(UIColor::corp_blue);
    int logoWidth = 128;
    _display->drawXbm((_display->width() - logoWidth) / 2, 3, meshcore_logo, logoWidth, 13);

    // meshcore website
    const char* website = "https://meshcore.io";
    _display->setColor(UIColor::primary_txt);
    _display->setTextSize(1);
    _display->drawTextCentered(_display->width()/ 2, 22, website);

    // Powering off
    const char* poweroff_string = "Turning OFF";
    uint16_t poffWidth = _display->getTextWidth(poweroff_string);
    _display->setCursor((_display->width() - poffWidth) / 2, 48);
    _display->drawTextCentered(_display->width()/2, 48, poweroff_string);
  } else if (_is_eink) {   // large e-ink: rich, multi-page dashboard
    renderRichScreen();
  } else {                 // small OLED: compact legacy layout
    _display->setCursor(0, 0);
    _display->setTextSize(1);
    _display->setColor(UIColor::primary_txt);
    _display->print(_node_prefs->node_name);

    // freq / sf
    _display->setCursor(0, 20);
    sprintf(tmp, "FREQ: %06.3f SF%d", _node_prefs->freq, _node_prefs->sf);
    _display->print(tmp);

    // bw / cr
    _display->setCursor(0, 30);
    sprintf(tmp, "BW: %03.2f CR: %d", _node_prefs->bw, _node_prefs->cr);
    _display->print(tmp);
  }
}

void UITask::loop() {
#if defined(PIN_USER_BTN) && defined(DISPLAY_CLASS)
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    if (!_display->isOn()) {
      _display->turnOn();                       // wake a sleeping display
    } else if (_is_eink) {
      _page = (_page + 1) % NUM_EINK_PAGES;      // cycle info pages (Status/Radio/Traffic)
      _next_refresh = 0;                         // redraw immediately
    }
    _auto_off = millis() + AUTO_OFF_MILLIS;      // extend auto-off timer
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
      _display->turnOn();
      Serial.println("Powering Off");
      _powering_off_at = millis() + POWEROFF_DELAY;
  }
#endif

  if (_display->isOn()) {
    if (millis() >= _next_refresh) {
      _display->startFrame();
      renderCurrScreen();
      _display->endFrame();

      // e-ink content changes slowly (minute granularity) and the driver CRC-gates
      // physical updates, so a gentle cadence avoids needless flashing/wear...
      unsigned long interval = _is_eink ? EINK_REFRESH_MILLIS : 1000;
      // ...but keep the boot splash / power-off countdown responsive.
      if (millis() < _started_at + BOOT_SCREEN_MILLIS || _powering_off_at > 0) interval = 500;
      _next_refresh = millis() + interval;
    }
    if (AUTO_OFF_MILLIS > 0 && millis() > _auto_off) {
      _display->turnOff();
    }
  }

  if (_powering_off_at > 0) { // power off timer armed
#ifdef LED_PIN
    digitalWrite(LED_PIN, LED_STATE_ON); // switch on the led until poweroff
#endif
    if (millis() > _powering_off_at) {
      _board->powerOff();  // should not return
    }
  }
}
