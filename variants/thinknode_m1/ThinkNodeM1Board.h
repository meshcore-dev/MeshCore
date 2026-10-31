#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

// built-ins
// 2026-10 原理图（ThinkNode-M1_V2.0）核对：电池检测链路为
//   BAT+ → Q9(P-MOS 开关，受 PERIPHERAL_EN/P0.12 控制) → R34 100K → BAT_ADC → R35 100K → GND → U1.P0.04/AIN2
//   即分压比 1/2，ADC 满量程 6.0V；参考源用内部 3.0V、12bit ⇒ 0.7324 mV/LSB（除以 2 后再补回）。
#define VBAT_MV_PER_LSB   (0.73242188F)   // 3.0V ADC range and 12-bit ADC resolution = 3000mV/4096

#define VBAT_DIVIDER      (0.5F)          // R34 100K / R35 100K voltage divider on VBAT
#define VBAT_DIVIDER_COMP (2.0F)          // Compensation factor for the VBAT divider
#define M1_VBAT_DIVIDER_NUM  2            // 整数版分压补偿（= 1 / VBAT_DIVIDER）

#define PIN_VBAT_READ     (4)
#define REAL_VBAT_MV_PER_LSB (VBAT_DIVIDER_COMP * VBAT_MV_PER_LSB)

/* 按键（V2.0 原理图核实）：
   功能键 = 触摸键 K3 = 网络 BUTTON_TOUCH = U1.P1.07 = Arduino 39（R71 100K 上拉 + C71 1nF）
   翻页键 = K4 = BUTTON1 = U1.P1.10 = Arduino 42（= variant.h 的 PIN_USER_BTN）
   注意 variant.h 的 PIN_BUTTON2 = 11 实际是 GPS_INT，不能当按键。 */
#ifndef PIN_FUNC_BTN
  #define PIN_FUNC_BTN          39
#endif
#ifndef FUNC_BTN_LONG_MILLIS
  #define FUNC_BTN_LONG_MILLIS  3000
#endif

/* 固件版本标识仅用于休眠页，启动和首页隐藏。 */
#ifndef M1_FW_TAG
  #ifdef M1_HIB_GPS_RESET_LOW
    #define M1_FW_TAG             "m1-r55b"
  #else
    #define M1_FW_TAG             "m1-r55"
  #endif
#endif

/* ---------------- 电池 ADC 采样参数（2026-10 优化） ----------------
   分压源阻抗 = 100K∥100K = 50K，而 nRF52 SAADC 默认采样时间仅 3us，
   采样电容建立不充分 → 系统性偏低。这里改为 40us 采样时间 + 16x 硬件过采样
   + 上电做一次 SAADC 偏移校准，并做多次平均，消除这项固定误差。
   若实测仍有固定偏差，用 M1_VBAT_CAL_PERMILLE 校准（固件读数/万用表读数×1000）。 */
#ifndef M1_VBAT_ADC_SAMPLES
  #define M1_VBAT_ADC_SAMPLES     8      // 平均次数（另外丢弃第 1 次）
#endif
#ifndef M1_VBAT_ADC_TACQ_US
  #define M1_VBAT_ADC_TACQ_US     20     // 采样保持时间(us)：50K 源阻抗下 20us 约 160 个 RC 时间常数
#endif
#ifndef M1_VBAT_ADC_OVERSAMPLE
  #define M1_VBAT_ADC_OVERSAMPLE  1      // SAADC 硬件过采样倍数（1=关闭；开启会增加不确定性）
#endif
#ifndef M1_VBAT_CAL_PERMILLE
  #define M1_VBAT_CAL_PERMILLE    1000   // 校准系数（1000 = 不校准）
#endif

class ThinkNodeM1Board : public NRF52Board {
public:
  ThinkNodeM1Board() : NRF52Board("THINKNODE_M1_OTA") {}
  void begin();
  uint16_t getBattMilliVolts() override;

  /* 2026-08-21 评审 序号27：关机/深度休眠前把 M1 外设电源域关掉，降低漏电流 */
  void shutdownPeripherals() override;
  void powerOff() override;   // USB absent: SYSTEMOFF; inserting USB starts the device.

  /* 2026-08-21 评审 序号10：可唤醒的深度休眠
     - 先释放全部 GPIO 再切断 VDD 外设轨（否则高电平会经 ESD 二极管倒灌，电流反而升高）
     - 把功能键/翻页键配成 SENSE_LOW 唤醒源（NOPULL，靠外部 100K 上拉），再进 SYSTEMOFF
     - 按键按下 → 复位重启（等同一次正常开机），因此"单击唤醒"成立
     - 返回 false 表示按键一直没松开、已放弃休眠（避免"按住就睡→立刻被自己唤醒"） */
  bool hibernate();

  /* 评审 序号33：充电状态感知（USB VBUS 在位；充满状态由充电 IC 硬件指示，固件无法区分） */
  bool isCharging() { return isExternalPowered(); }

  /* 上次复位原因：用来确认"休眠是否真的进了 SYSTEMOFF"（OFF 位） */
  uint32_t reset_reas = 0;
  uint8_t  prev_sleep = 0;      // 0xA5=断轨休眠唤醒, 0xA6=安全模式唤醒, 0xA7=无引脚操作唤醒, 0x5A=休眠页关机, 0=无
  uint8_t  prev_trace = 0;      // 上次断电过程的细粒度进度（GPREGRET，1..23）
  uint8_t  keyLevels() const;   // 实时按键电平：bit0=K4/P1.10, bit1=K3/P1.07（1=高=未按）
  uint8_t  gpsSchemeCode() const;   // 1=旧 L76K, 2=新 AG3352, 0=未识别（主页显示 G:O / G:N）
  bool bootedFromSystemOff() const { return (reset_reas & POWER_RESETREAS_OFF_Msk) != 0; }
  bool wokeFromHibernate() const {
    return bootedFromSystemOff() || prev_sleep == 0xA5 || prev_sleep == 0xA6 || prev_sleep == 0xA7 ||
           (prev_sleep >= 0xE0 && prev_sleep <= 0xE7);
  }

  #if defined(P_LORA_TX_LED)
  // Blue LED belongs to RX activity / BLE pairing. Transmitting (including
  // relays and ACKs) must neither light it nor interrupt an existing RX pulse.
  void onBeforeTransmit() override { }
  void onAfterTransmit() override { }
  #endif

  const char* getManufacturerName() const override {
    return "Elecrow ThinkNode-M1";
  }
};
