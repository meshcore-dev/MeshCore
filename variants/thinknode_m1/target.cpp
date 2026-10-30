#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>
#include <helpers/sensors/MicroNMEALocationProvider.h>

ThinkNodeM1Board board;

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);

WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
MicroNMEALocationProvider nmea = MicroNMEALocationProvider(m1_gps_stream(), &rtc_clock, -1, -1);  // 串口走 m1_gps 的 tee，便于统计；GPS 引脚也由 m1_gps 管理
ThinkNodeM1SensorManager sensors = ThinkNodeM1SensorManager(nmea);

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
  #ifdef PIN_FUNC_BTN
    // 参数：pin, 长按阈值(ms), reverse(按下为低), pulldownup(启用电平上拉)
    // 板上 R71 为 100K 上拉到 VDD_NRF + C71 1nF，再叠加 MCU 内部上拉，避免浮空误判
    MomentaryButton func_btn(PIN_FUNC_BTN, FUNC_BTN_LONG_MILLIS, true, true);
  #endif
#endif

bool radio_init() {
  rtc_clock.begin(Wire);
  return radio.std_init(&SPI);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}

void ThinkNodeM1SensorManager::start_gps() {
  if (!gps_active) {
    if (!m1_gps_ready()) {    // 懒初始化：第一次真正用 GPS 时才配引脚 + 开 UART
      m1_gps_init();
      Serial1.begin(m1_gps_baud());
    }
    gps_active = true;
    m1_gps_power_on();        // 按识别出的方案上电 + 复位 + 匹配波特率
    _location->begin();
    // 串口打印识别结果，现场可直接确认用的是哪种 GPS
    Serial.printf("GPS: %s @ %lu bps\n", m1_gps_scheme_name(), (unsigned long) m1_gps_baud());
  }
}

void ThinkNodeM1SensorManager::stop_gps() {
  if (gps_active) {
    gps_active = false;
    _location->stop();
    m1_gps_power_off();       // 按方案关电/进待机（新方案：先发待机命令再断 0.8V）
    serial_end_safe_public(); // 关 UARTE，让停用后的状态与"从未用过 GPS"尽量一致
  }
}

bool ThinkNodeM1SensorManager::begin() {
  /* ★ 2026-10：GPS 改成**懒初始化**。
     现场对照：GPS 开关关着时，加了 GPS 兼容的固件休眠仍有 300 µA，而没有 GPS 兼容的
     第 5 版只要 30 µA。差别就在"开机就把 GPS 相关引脚/UART 都初始化一遍"。
     所以这里只读开关电平，开关真的打开时才做 m1_gps_init() + Serial1.begin()。
     这样开关关着时固件的行为与第 5 版完全一致。 */
  last_gps_switch_state = m1_gps_switch_on();
  // 首次启动推迟到第一个 loop()，避免新 GPS 的波特率扫描拖慢开机画面
  gps_start_pending = last_gps_switch_state;

  return true;
}

bool ThinkNodeM1SensorManager::querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) {
  if (requester_permissions & TELEM_PERM_LOCATION) {   // does requester have permission?
    telemetry.addGPS(TELEM_CHANNEL_SELF, node_lat, node_lon, node_altitude);
  }
  return true;
}

void ThinkNodeM1SensorManager::loop() {
  static long next_gps_update = 0;
  static long last_switch_check = 0;

  // 开机后第一次进入主循环时再启动 GPS（开关本来就是开的情况）
  if (gps_start_pending) {
    gps_start_pending = false;
    start_gps();
  }

  // Check GPS switch state every second
  if (millis() - last_switch_check > 1000) {
    bool current_switch_state = m1_gps_switch_on();   // 新旧硬件的开关脚/极性不同

    // Detect switch state change
    if (current_switch_state != last_gps_switch_state) {
      last_gps_switch_state = current_switch_state;

      if (current_switch_state) {  // Switch is ON
        MESH_DEBUG_PRINTLN("GPS switch ON");
        start_gps();
      } else {  // Switch is OFF
        MESH_DEBUG_PRINTLN("GPS switch OFF");
        stop_gps();
      }
    }

    last_switch_check = millis();
  }

  if (!gps_active) {
    return;  // GPS is not active, skip further processing
  }

  m1_gps_note_rx();       // 统计串口接收活动（屏幕上能看到 rx/lb，判断是否有数据）
  m1_gps_tick();          // 若 5 秒内没有 GGA，主动发命令让模块输出定位句
  _location->loop();

  if (millis() > next_gps_update) {
    if (_location->isValid()) {
      node_lat = ((double)_location->getLatitude())/1000000.;
      node_lon = ((double)_location->getLongitude())/1000000.;
      node_altitude = ((double)_location->getAltitude()) / 1000.0;
      MESH_DEBUG_PRINTLN("lat %f lon %f", node_lat, node_lon);
    }
    next_gps_update = millis() + 1000;
  }
}

int ThinkNodeM1SensorManager::getNumSettings() const {
  return 1;  // always show GPS setting
}

const char* ThinkNodeM1SensorManager::getSettingName(int i) const {
  return (i == 0) ? "gps" : NULL;
}

const char* ThinkNodeM1SensorManager::getSettingValue(int i) const {
  if (i == 0) {
    return gps_active ? "1" : "0";
  }
  return NULL;
}

bool ThinkNodeM1SensorManager::setSettingValue(const char* name, const char* value) {
  if (strcmp(name, "gps") == 0) {
    if (strcmp(value, "0") == 0) {
      stop_gps();
    } else {
      start_gps();
    }
    return true;
  }
  return false;  // not supported
}

