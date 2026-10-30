#pragma once
#include <Arduino.h>

/*
 * ThinkNode M1 LED 指示管理
 *
 * 依据：固件更新目录.xlsx 中 2026-8-21 会议评审结论
 *   序号 5  蓝牙配对：配对中蓝灯 1Hz 闪 30s；配对成功蓝灯常亮 30s 后熄灭
 *   序号 7  LoRa 接收：收到数据指示灯闪烁
 *   序号 8  低电量：低电时指示灯闪烁
 *
 * 硬件依据：V1.5/V2.0 原理图 netlist。
 *   - 蓝色通知/LoRa 指示灯 = P_LORA_TX_LED(P0.13 / pin13)，高电平点亮，固件可控；
 *   - P1.06(pin38) 经 R21/Q4 驱动 D11 红灯；U9.CHGLED 也经 R11 接 D11 阴极。
 *     P1.04 连的是 D6.A2，并非与 P1.06 并联。充电芯片也可能点亮 D11，
 *     MCU 无法强制熄灭由充电芯片点亮的红灯。
 *   - 充电状态只有 USB VBUS 可感知，没有"充满"回读信号。
 */
#ifdef THINKNODE_M1

// 低电提示灯：默认用红灯(pin38)；若硬件确认红灯不可用，注释掉本宏即退回蓝灯
#define M1_LOW_BATT_ON_RED

#ifndef M1_STATUS_LED_PIN
  #define M1_STATUS_LED_PIN   P_LORA_TX_LED   // 13, HIGH = 亮
#endif

#ifdef PIN_STATUS_LED_POLARITY_LOW
  #define M1_STATUS_LED_ON    LOW
#else
  #define M1_STATUS_LED_ON    HIGH
#endif

#ifdef M1_LOW_BATT_ON_RED
  #ifndef M1_LOW_BATT_LED_PIN
    #define M1_LOW_BATT_LED_PIN LED_RED       // 38 (P1.06)
  #endif
  #ifdef M1_RED_ACTIVE_LOW
    #define M1_LOW_BATT_LED_ON  LOW
  #else
    #define M1_LOW_BATT_LED_ON  HIGH          // 与 Meshtastic 现行 LED_STATE_ON 一致
  #endif
#else
  #define M1_LOW_BATT_LED_PIN M1_STATUS_LED_PIN
  #define M1_LOW_BATT_LED_ON  M1_STATUS_LED_ON
#endif

void m1_leds_begin();
void m1_leds_loop();
void m1_leds_shutdown();

// 蓝牙：连接建立 -> 配对窗口开始（闪灯）；完成绑定 -> 常亮 30s；断开 -> 取消
void m1_leds_ble_pairing_started();
void m1_leds_ble_pairing_secured();
void m1_leds_ble_pairing_cancelled();

// LoRa 收到一包数据
void m1_leds_rx_activity();

// 低电量指示开关
void m1_leds_low_battery(bool active);
bool m1_leds_is_low_battery();

#endif  // THINKNODE_M1
