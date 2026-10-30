#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <nrf.h>
#include <nrf_gpio.h>
#ifdef QSPIFLASH
#include <CustomLFS_QSPIFlash.h>
#endif

#include "ThinkNodeM1Board.h"
#include "m1_leds.h"
#include "m1_gps.h"

#ifdef THINKNODE_M1

/* 必须在 SystemInit() 之前抓 RESETREAS —— 核心启动代码会把它清掉。
   优先级 101 早于 SystemInit(102) 与其它 C++ 静态构造。 */
static uint32_t s_m1_reset_reas = 0;
static uint8_t  s_m1_prev_sleep = 0;     // 上次进入 SYSTEMOFF 前的标记（0=无, 0xA5=休眠唤醒, 0x5A=关机）
static uint8_t  s_m1_prev_trace = 0;     // GPREGRET（第二个保留寄存器）里的细粒度进度，1..23
static void __attribute__((constructor(101))) m1_early_reset_capture() {
  s_m1_reset_reas = NRF_POWER->RESETREAS;
  NRF_POWER->RESETREAS = s_m1_reset_reas;
  // GPREGRET2 由我们在进 SYSTEMOFF 前写入，引导程序不会清它 —— 比 RESETREAS 可靠
  s_m1_prev_sleep = (uint8_t) NRF_POWER->GPREGRET2;
  NRF_POWER->GPREGRET2 = 0;
  // GPREGRET 是另一个掉电不丢的寄存器，用它记"细到每条引脚操作"的进度
  s_m1_prev_trace = (uint8_t) NRF_POWER->GPREGRET;
  NRF_POWER->GPREGRET = 0;
}

/* 休眠/关机"进度标记"（写 GPREGRET2，复位后不丢，开机画面会显示 S:xx）。
   现场判读（从上往下，看到哪个就说明流程最远只走到哪一步）：
     0x00     = 正常上电/复位，或中途主动放弃（屏上出现 Sleep aborted）
     0xB1     = 按键已确认安静，准备断电
     0xC1     = 即将释放全部 GPIO（P0.12 会悬空 → R75 下拉 → VDD 掉电）
     0xC2     = GPIO 已全部释放
     0xC3     = 即将把 P0.12 驱低（正式断 VDD 外设轨）
     0xC4     = 外设轨已断
     0xC5     = 引脚/外设轨全部处理完
     0xD1     = 安全模式：即将做最小的外设静默（不释放任何 GPIO）
     0xD2     = 安全模式：外设静默完成
     0xB2     = 同上（断轨模式走完）
     0xB7     = 同上（安全模式：外设轨仍带电）
     0xB3     = 唤醒源已装且断轨后仍确认为高
     0xA5     = 已进入 SYSTEMOFF（断轨模式）      ← 真休眠
     0xA6     = 已进入 SYSTEMOFF（安全模式）      ← 真休眠，但外设轨带电
     0xEE     = 断轨后唤醒脚被拖低，已恢复供电并受控复位
     0x51/52/5A = 休眠页"关机"流程的各步 */
/* ★ 关键：GPREGRET / GPREGRET2 在 SoftDevice 启用后是**写保护寄存器**。
   直接写 NRF_POWER->GPREGRET2 会被 SoftDevice 拦下 → 触发 fault handler
   → NVIC_SystemReset()（现场表现：开机画面 R:00000004，且"写完标记的下一条
   指令凭空消失"）。必须走 sd_power_gpregret_clr/set 这套 SVC 接口。
   MeshCore 自己的 NRF52Board::enterSystemOff() 也是这么写的。 */
static void m1_gpre_set(uint8_t id, uint8_t value) {   // id: 0=GPREGRET, 1=GPREGRET2
  uint8_t sd_enabled = 0;
  sd_softdevice_is_enabled(&sd_enabled);
  if (sd_enabled) {
    (void) sd_power_gpregret_clr(id, 0xFF);
    if (value != 0) (void) sd_power_gpregret_set(id, value);
  } else {
    if (id == 1) NRF_POWER->GPREGRET2 = value;
    else         NRF_POWER->GPREGRET  = value;
  }
}

static inline void m1_marker(uint8_t v) { m1_gpre_set(1, v); }   // GPREGRET2

/* 细粒度进度：写到 GPREGRET（另一个保留寄存器），开机画面显示 T:xx。
   1..9  = hibernate() 主要阶段
   11..17 = 安全模式断电块里的每一条引脚操作（值 = 即将执行的那一条）
   20..23 = 装唤醒源/断电后确认/即将进 OFF */
static inline void m1_trace(uint8_t v) { m1_gpre_set(0, v); }    // GPREGRET

// Check the actual flash WIP bit before removing its supply. Stop QSPI
// before GPIO cleanup: a peripheral can otherwise still own those pins.
// Called while the application and display can still recover on failure.
static bool m1_quiesce_flash() {
#ifdef QSPIFLASH
  if (QSPIFlash._qspi_initialized) {
    const uint32_t start = millis();
    bool ready = false;
    while (millis() - start < 5000) {
      if (QSPIFlash.isReady()) {
        uint8_t status = 0xFF;
        nrf_qspi_cinstr_conf_t cfg = {};
        cfg.opcode = 0x05; // MX25R1635F Read Status Register
        cfg.length = NRF_QSPI_CINSTR_LEN_2B;
        cfg.io2_level = true;
        cfg.io3_level = true;
        if (nrfx_qspi_cinstr_xfer(&cfg, nullptr, &status) == NRFX_SUCCESS &&
            (status & 0x01) == 0) {
          ready = true;
          break;
        }
      }
      delay(1);
    }
    if (!ready) {
      Serial.println("M1 sleep aborted: QSPI busy/error");
      return false;
    }
    QSPIFlash.end();
    nrfx_qspi_uninit();
    QSPIFlash._qspi_initialized = false;
  }
#endif
  return true;
}

void ThinkNodeM1Board::begin() {
  NRF52Board::begin();

  reset_reas = s_m1_reset_reas;
  prev_sleep = s_m1_prev_sleep;
  prev_trace = s_m1_prev_trace;
  Serial.printf("M1 reset_reas=0x%08X prev_sleep=0x%02X prev_trace=%u\n",
                (unsigned) reset_reas, (unsigned) prev_sleep, (unsigned) prev_trace);

  Wire.begin();

  m1_leds_begin();   // 评审 序号5/7/8：LED 指示

#ifdef P_LORA_TX_LED
  pinMode(P_LORA_TX_LED, OUTPUT);
  digitalWrite(P_LORA_TX_LED, LOW);
#endif

  // P0.21 controls the RF switch; P1.05 (37) belongs to GPS reset.
  pinMode(PIN_TXCO, OUTPUT);
  digitalWrite(PIN_TXCO, HIGH);
  delay(10);
}

// Stop peripherals while their supplies and buses are still available.
void ThinkNodeM1Board::shutdownPeripherals() {
  m1_leds_shutdown();
  NRF52Board::shutdownPeripherals();   // 关屏 + 电台复位（基类）

#ifdef P_LORA_TX_LED
  pinMode(P_LORA_TX_LED, OUTPUT);
  digitalWrite(P_LORA_TX_LED, LOW);    // 蓝色指示灯灭
#endif

#ifdef LED_RED
  digitalWrite(LED_RED, LOW);          // Q4 off: red LED is active high
  pinMode(LED_RED, INPUT);             // 释放，交还充电 IC 驱动
#endif

  // GPS sequencing belongs to m1_gps_sleep_for_hibernate().
  // In particular P1.02 is only a board-ID input on V2.0.

#ifdef DISP_BACKLIGHT
  pinMode(DISP_BACKLIGHT, OUTPUT);
  digitalWrite(DISP_BACKLIGHT, LOW);   // 背光/电子纸供电关
#endif

#ifdef PIN_TXCO
  pinMode(PIN_TXCO, OUTPUT);
  digitalWrite(PIN_TXCO, LOW);         // MCU RF-switch control, not radio DIO3
#endif

#ifdef PIN_PWR_EN
  // 注意：这里不切断外设电源轨，断轨放在低功耗序列末尾（必须先释放引脚再断轨）
#endif

#ifdef PIN_BUZZER
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);       // 蜂鸣器静音
#endif
}

/* ---------- 低功耗断电序列（评审序号 10 休眠 / 序号 27 关机漏电流） ----------

   V2.0 原理图的供电结构：
     - MCU    : VCC3V3 → R31(0R) → VDD_NRF，永远带电；
     - 外设轨 : VCC3V3 → Q2(P-MOS) → VDD，Q2 由 PERIPHERAL_EN(P0.12) 经 Q3 驱动，
                可关断（R29 = NC 无旁路）；QSPI flash、电子纸、电池分压都在这一路；
     - 电台轨 : VCC3V3 → R73(0R) → VDD_RADIO，硬连，软件无法断电，通过 SetSleep 进入睡眠。

   关键：先释放所有 GPIO（输入-断开）再断外设轨。否则 MCU 输出的高电平会经外设 ESD
   二极管倒灌进已断电的轨，实测把休眠电流从 µA 级抬到约 16mA。 */

static inline void m1_pin_drive_low(uint32_t pin) {
  nrf_gpio_pin_write(pin, 0);
  nrf_gpio_cfg_output(pin);
}

static inline void m1_pin_drive_high(uint32_t pin) {
  nrf_gpio_pin_write(pin, 1);
  nrf_gpio_cfg_output(pin);
}

/* 进入 SYSTEMOFF 后不返回应用。
   注意：**不要再加 NVIC_SystemReset() 兜底** —— 那会把设备重启，现场看起来就是
   “一休眠就立刻醒过来”，反而掩盖了 SYSTEMOFF 失败的真实原因。 */
static void m1_check_usb_before_off() {
  uint8_t enabled = 0;
  sd_softdevice_is_enabled(&enabled);
  uint32_t status = 0;
  if (enabled) {
    // Preserve USB attach reporting under SoftDevice; never touch its POWER
    // interrupt registers directly. VBUS sensing does not require enumeration.
    (void) sd_power_usbdetected_enable(1);
    if (sd_power_usbregstatus_get(&status) != NRF_SUCCESS) return;
  } else {
    status = NRF_POWER->USBREGSTATUS;
  }
  // Peripherals are already shut down here. Restart if the charger arrived
  // during shutdown rather than enter OFF with VBUS already present.
  if (status & POWER_USBREGSTATUS_VBUSDETECT_Msk) NVIC_SystemReset();
}

// Once peripherals are shut down, never return to the application or reset
// merely because SYSTEMOFF failed. SoftDevice owns POWER until disabled.
[[noreturn]] static void m1_enter_system_off(uint8_t sleep_marker) {
  m1_check_usb_before_off();
  m1_marker(sleep_marker);
  uint8_t enabled = 0;
  sd_softdevice_is_enabled(&enabled);
  if (enabled) {
    for (unsigned i = 0; i < 40; ++i) {
      m1_check_usb_before_off();
      (void) sd_power_system_off();
      delay(25);
    }
    // Do not call another SVC or use RTOS delays after disabling SoftDevice:
    // the scheduler's clock may already have stopped.
    for (;;) {
      const uint32_t result = sd_softdevice_disable();
      if (result == NRF_SUCCESS || result == NRF_ERROR_SOFTDEVICE_NOT_ENABLED) break;
      (void) sd_power_system_off();
      delay(25);
    }
  }
  __disable_irq();
  for (;;) {
    NRF_POWER->SYSTEMOFF = POWER_SYSTEMOFF_SYSTEMOFF_Enter;
    __DSB();
    __WFE(); // also remain parked when a debugger emulates SYSTEMOFF
  }
}

// Clear inherited GPIO SENSE/LATCH and comparator wake before selecting the
// explicit wake policy. Peripheral interrupts alone cannot wake SYSTEMOFF.
static void m1_clear_wake_sources() {
  for (uint32_t pin = 0; pin < 48; ++pin) {
    nrf_gpio_cfg_sense_set(pin, NRF_GPIO_PIN_NOSENSE);
  }
  NRF_P0->LATCH = 0xFFFFFFFF;
  NRF_P1->LATCH = 0xFFFFFFFF;
  NRF_LPCOMP->TASKS_STOP = 1;
  NRF_LPCOMP->INTENCLR = 0xFFFFFFFF;
  NRF_LPCOMP->ENABLE = 0;
}

// Both keys have pull-ups to the always-on MCU supply. Either low wakes SYSTEMOFF.
static const uint32_t M1_WAKE_PINS[] = {PIN_USER_BTN, PIN_FUNC_BTN};

static bool m1_wake_key_held() {
  for (uint32_t pin : M1_WAKE_PINS) {
    if (nrf_gpio_pin_read(pin) == 0) return true;
  }
  return false;
}

/* 两个按键的实时电平（bit1 = K4/P1.10，bit0 = K3/P1.07，1 = 高电平=未按）。
   开机画面会显示，用于现场判断"固件到底读到了什么"。 */
uint8_t ThinkNodeM1Board::keyLevels() const {
  uint8_t v = 0;
  if (nrf_gpio_pin_read(PIN_USER_BTN) != 0) v |= 0x01;   // K4 / 翻页键 (P1.10)
#ifdef PIN_FUNC_BTN
  if (nrf_gpio_pin_read(PIN_FUNC_BTN) != 0) v |= 0x02;   // K3 / 功能键 (P1.07)
#endif
  return v;
}

uint8_t ThinkNodeM1Board::gpsSchemeCode() const {
  return (uint8_t) m1_gps_scheme();   // 1=L76K(旧) 2=AG3352(新) 0=未识别
}

/* 上次休眠是否"断轨即复位"（残留标记落在 C 段），或者是安全模式入睡(0xA6)。
   现场已多次确认：在这块板子上"断 VDD 外设轨"这一步会把芯片复位，
   所以目前**默认不断轨**（安全模式），先把"能睡 + 单击唤醒"跑通。
   要回去试断轨省电：platformio.ini 里打开 -D M1_HIB_CUT_RAIL=1。 */
static bool m1_use_rail_cut() {
#ifdef M1_HIB_CUT_RAIL
  // 只有"断轨过程本身"留下的标记（C1..C5）才退回不断轨；其它残留值不作为依据，
  // 否则一次 0xA6 就会让它永远不再断轨。
  return !(s_m1_prev_sleep >= 0xC1 && s_m1_prev_sleep <= 0xC6);
#else
  return false;   // 未启用断轨：安全模式（外设轨保持供电）
#endif
}

// r43: release unused pins without output pulses. Final radio and GPS
// states are applied after bus shutdown; no bulk release follows them.
static void m1_hibernate_pin_table() {
  for (uint32_t pin = 2; pin < 48; pin++) {
    if (pin == 18) continue;                                       // P0.18 NRESET，保持原样
    if (pin == P_LORA_BUSY || pin == 19 || pin == 20 || pin == 22 || pin == 23 ||
        pin == P_LORA_NSS || pin == P_LORA_RESET) continue;         // 电台：下面显式驱动
    if (pin == 9 || pin == 10) continue;                            // NFC1/NFC2
    if (pin == PIN_USER_BTN || pin == PIN_FUNC_BTN) continue;       // 按键（唤醒源）
    if (pin == 11 || pin == 32 || pin == 40 || pin == 41 ||
        pin == M1_GPS_SW_OLD_PIN || pin == M1_GPS_SW_NEW_PIN ||
        pin == M1_GPS_EN_OLD_PIN || pin == M1_GPS_DETECT_PIN ||
        pin == M1_GPS_RESET_PIN) continue;                          // GPS：下面显式驱动
    if (pin == DISP_BACKLIGHT || pin == PIN_TXCO || pin == P_LORA_TX_LED ||
        pin == LED_RED || pin == PIN_PWR_EN) continue;              // 下面显式驱动
    nrf_gpio_cfg_default(pin);
  }
}

/* 关键脚的最终电平（必须在 m1_hibernate_pin_table() 之后调用，且此后不再释放引脚） */
static void m1_hibernate_pins(bool cut_rail) {
  m1_hibernate_pin_table();

  // Radio supply is always on through R73. Preserve the SetSleep state.
  // NRF52Board already sent SetSleep. Holding NRESET low afterwards
  // destroys that state; absence of an external pull-up proves nothing
  // about the SX1262 internal reset circuitry. Keep reset deasserted.
  m1_pin_drive_high(P_LORA_RESET);
  m1_pin_drive_high(P_LORA_NSS);         // CS 高（与 Meshtastic shutdownPeripherals 一致）
  m1_pin_drive_low(PIN_TXCO);            // P0.21 → SX1262 DIO3（TCXO 供电）

  // ---- 电子纸 / 背光：U11 的输入是 VCC3V3 常供电，EN 悬空可能自己打开升压 ----
  m1_pin_drive_low(DISP_BACKLIGHT);      // P1.11 EINK_EN → U11.EN / U7.DIM

  // ---- 指示灯 ----
  m1_pin_drive_low(P_LORA_TX_LED);       // P0.13 蓝灯（Q5，NPN，低=灭）
  m1_pin_drive_low(LED_RED);             // P1.06 红灯（Q4，NPN，低=灭）

  // ---- GPS（旧板由机械开关 K5 供电，固件只能让模组自己睡）----
  m1_gps_apply_sleep_pins();           // board-specific final states; preserve reset diagnostic

#ifdef PIN_PWR_EN
  if (cut_rail) {
    digitalWrite(PIN_PWR_EN, LOW);       // 断 VDD 外设轨（QSPI flash / 电子纸 / 电池分压）
    pinMode(PIN_PWR_EN, INPUT);          // 板上 R75=20K 下拉 → 轨保持关闭
  } else {
    m1_pin_drive_high(PIN_PWR_EN);       // 安全模式：外设轨保持供电
  }
#else
  (void) cut_rail;
#endif
}

/* Explicit power-off uses the same peripheral shutdown. Charger insertion
   remains a wake source; while USB is already present keep the device on. */
void ThinkNodeM1Board::powerOff() {
  if (isExternalPowered()) return;
  if (!m1_quiesce_flash()) return;
  m1_marker(0x51);
  shutdownPeripherals();
  m1_gps_sleep_for_hibernate(0);
  Wire.end();
  SPI.end();
#if defined(DISPLAY_CLASS) && SPI_INTERFACES_COUNT > 1
  SPI1.end();
#endif
  m1_marker(0x52);
  m1_hibernate_pins(m1_use_rail_cut());
  // Critical/off mode: only the mechanical power switch (or charger attach)
  // can start the device. Neither page nor function key is a wake source.
  m1_clear_wake_sources();
  m1_enter_system_off(0x5A);
}

// r43: quiesce flash, sleep devices, stop buses, apply final pins, enter OFF.
bool ThinkNodeM1Board::hibernate() {
  // Do not turn off peripherals if the wake key is already held or flash
  // still has an unfinished operation. The UI can retry normally.
  if (isExternalPowered() || m1_wake_key_held() || !m1_quiesce_flash()) return false;
  m1_trace(1);
  m1_marker(0xB1);

  shutdownPeripherals();          // 关屏(EINK_EN 低) + 电台 sleep/NRESET + GPS stop + 串口 flush
  m1_trace(2);

  /* ★ final42：GPS 必须**先**处理完 —— 它要发串口命令，必须在释放/改引脚之前做。
     （旧板 V1.5 的待机命令 + WAKE_UP 低电平都在 m1_gps_sleep_for_hibernate() 里） */
  m1_gps_sleep_for_hibernate(0);
  m1_trace(3);
  // Finish all bus transactions before releasing pins and cutting VDD.
  // This is also the ordering used by Meshtastic 2.7.26 cpuDeepSleep().
  Wire.end();
  SPI.end();
#if defined(DISPLAY_CLASS) && SPI_INTERFACES_COUNT > 1
  SPI1.end();
#endif

  // Release unused pins, apply radio/GPS final levels, then cut VDD.
  m1_hibernate_pins(m1_use_rail_cut());
  m1_trace(4);
  m1_trace(5);

  m1_trace(20);
  for (uint32_t pin : M1_WAKE_PINS) {
    nrf_gpio_cfg_input(pin, NRF_GPIO_PIN_PULLUP);
  }
  // A held/bouncing key must not force a reboot after shutdown. Wait for
  // both keys to remain released before arming their level-sensitive wake.
  uint32_t quiet_since = millis();
  while (millis() - quiet_since < 500) {
    m1_check_usb_before_off();
    if (m1_wake_key_held()) quiet_since = millis();
    delay(5);
  }
  m1_clear_wake_sources();
  for (uint32_t pin : M1_WAKE_PINS) {
    nrf_gpio_cfg_sense_set(pin, NRF_GPIO_PIN_SENSE_LOW);
  }
  m1_trace(21);
  const uint8_t sleep_marker = m1_use_rail_cut() ? 0xA5 : 0xA6;
  m1_marker(sleep_marker);

  m1_trace(22);
  m1_enter_system_off(sleep_marker);
}

/* 电池电压采样（2026-10 按原理图优化）
   分压：R34 100K / R35 100K（1:2），分压节点 BTA_ADC → P0.04/AIN2；
   分压使能：PERIPHERAL_EN(P0.12) → R74 → Q7 → R76 → Q9(P-MOS)。
   精度处理：40us 采样保持（源阻抗 50K）+ 16x 硬件过采样 + 偏移校准 + 多次平均。 */
static void m1_batt_adc_init() {
  analogReadResolution(12);        // 0..4095
  analogReference(AR_INTERNAL_3_0);  // 满量程 3.0V（默认是 3.6V，会偏小 20%）
  analogSampleTime(M1_VBAT_ADC_TACQ_US);
  analogOversampling(M1_VBAT_ADC_OVERSAMPLE);
  analogCalibrateOffset();
}

uint16_t ThinkNodeM1Board::getBattMilliVolts() {
  static bool adc_ready = false;
  if (!adc_ready) {
    m1_batt_adc_init();
    adc_ready = true;
  }

#ifdef PIN_PWR_EN
  // 分压电路由外设总闸经 Q7/Q9 控制，测量前确保导通
  pinMode(PIN_PWR_EN, OUTPUT);
  digitalWrite(PIN_PWR_EN, HIGH);
#endif

  (void) analogRead(PIN_VBAT_READ);   // 丢弃首个采样（采样保持电容建立）

  uint32_t sum = 0;
  for (int i = 0; i < M1_VBAT_ADC_SAMPLES; i++) {
    sum += analogRead(PIN_VBAT_READ);
  }
  uint32_t raw = (sum + (M1_VBAT_ADC_SAMPLES / 2)) / M1_VBAT_ADC_SAMPLES;   // 0..4095

  // 引脚电压(mV) = raw * 3000 / 4096，再乘分压补偿得到电池电压
  uint32_t pin_mv  = (raw * 3000UL + 2048UL) / 4096UL;
  uint32_t batt_mv = pin_mv * (uint32_t) M1_VBAT_DIVIDER_NUM;
  batt_mv = (batt_mv * (uint32_t) M1_VBAT_CAL_PERMILLE + 500UL) / 1000UL;

  if (batt_mv > 65000UL) batt_mv = 65000UL;
  return (uint16_t) batt_mv;
}
#endif
