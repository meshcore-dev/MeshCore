#pragma once
#include <Arduino.h>

/*
 * ThinkNode M1 GPS 新旧硬件自动兼容
 *
 * ===== 依据：V1.5 / V2.0 两份原理图逐脚核对 =====
 *
 * V1.5（旧，Quectel L76K，U5）
 *   K5(MSK12C02 滑动开关) 直接切换 GPS 模块电源 VCC_GPS：
 *       P$2 = VCC_GPS（接 U5.VCC / D4），P$1 = VCC_GPS_1（供电），P$3..P$7 = GND
 *   ⇒ 旧板上「打开 GPS」是**机械开关直接给模块供电**，没有任何 MCU 引脚参与；
 *   P1.01 ← R47 = 2MΩ ← VCC_GPS   ⇒ 只作开关位置**检测**（高=开关打开），
 *       2MΩ 源阻抗极高，必须用「普通输入」读，任何内部上/下拉都会把电平拉死；
 *   P1.02 → R53 = 100Ω → U5.WAKE_UP ⇒ 高=唤醒/保持工作，低=睡眠；
 *   P1.05 → R54 = 100Ω → U5.RESET_N ⇒ 低=复位，必须保持高才是运行；
 *   P1.08 → R10 = 100Ω → U5.RXD（MCU 发）、P1.09 → R9 = 100Ω → U5.TXD（MCU 收），9600；
 *   P1.00 → R20 = 0Ω → U5.1PPS；
 *   **P0.08 → R43 = 0Ω → BAT_ADC** ⇒ 旧板的 P0.08 硬连到电池采样节点，绝不可上拉/驱动！
 *
 * V2.0（新，Airoha AG3352QN/A，U18）
 *   P1.01 → R47 = 0Ω → VGPS_0V8_EN（R78 100K 下拉）⇒ **高=打开 GPS 供电**（U22 0.8V / U24 1.8V）；
 *   P0.08 → R43 = 0Ω → GPS_SW ← K5（另一端 GND / N$38）⇒ **高=开启、低=关闭**（按实机要求与 V1.5 方向一致）；
 *   P1.02 → R20 = 100K 下拉，R18 = NC（未连芯片）⇒ 固定为低的方案识别脚；
 *   P1.05 → R23 = 1K → Q16 → GPS_RST ⇒ **高=复位（经三极管拉低 RESETB），低=运行**（与旧板相反）；
 *   P1.08/P1.09 UART 与旧板相同；P0.19/P0.25 是 SX1262 的 SCK/RESET，不是 GPS 信号。
 *
 * 结论：两套方案「打开 GPS」的引脚完全不同：旧板靠机械开关（固件只需 P1.02 唤醒），
 *       新板靠 P1.01 供电使能。板型通过 P0.04/P0.08 是否硬连识别并缓存，
 *       再读取对应开关脚；两版均以高电平表示用户要求的开启方向。
 */
#ifdef THINKNODE_M1

#define M1_GPS_SW_OLD_PIN     33   // P1.01 旧板：2MΩ 检测开关位置（高=开）；新板：供电使能
#define M1_GPS_SW_NEW_PIN      8   // P0.08 新板：GPS_SW（高=开）；旧板：接 BAT_ADC，勿动
#define M1_GPS_EN_OLD_PIN     34   // P1.02 旧板：L76K WAKE_UP（高=工作）；新板：方案识别脚
#define M1_GPS_PWR_NEW_PIN    33   // P1.01 新板：VGPS_0V8_EN（高=供电）
#define M1_GPS_DETECT_PIN     34   // P1.02 新板：GPS_HIGHT（100K 下拉）
#define M1_GPS_RESET_PIN      37   // P1.05 旧板：低=复位；新板：高=复位（经 Q16）

enum M1GpsScheme : uint8_t {
  M1_GPS_UNKNOWN = 0,
  M1_GPS_L76K    = 1,   // 旧：Quectel L76K（V1.5）
  M1_GPS_AG3352  = 2    // 新：Airoha AG3352QN/A（V2.0）
};

void     m1_gps_init();                 // 相关脚按输入初始化（不驱动、不加拉）
bool     m1_gps_ready();                // 是否已经懒初始化过（引脚 + UART）
void     serial_end_safe_public();      // 关 UARTE（供 stop_gps 复用）
bool     m1_gps_switch_on();            // GPS 开关是否处于「开」；会顺带确定板型
M1GpsScheme m1_gps_scheme();
const char* m1_gps_scheme_name();
uint32_t m1_gps_baud();
void     m1_gps_power_on();             // 按板型上电/唤醒 + 复位 + 匹配波特率
void     m1_gps_power_off();            // 按板型睡眠/断电
void     m1_gps_rails_off();            // 关机/休眠用：安全关闭 GPS 供电
void     m1_gps_apply_sleep_pins();     // final GPIO states after UART/buses have stopped
/* 进 SYSTEMOFF 之前调用：按 M1 新 GPS 的 readme（先断 0.8V 再发睡眠命令）把模块停到低功耗。
   variant 用来**轮换候选睡眠命令**：每次长按休眠换一种，哪种能让电流掉下来就是正确命令。
   现场事实：GPS 开关开 480µA / 关 300µA（差 180µA 就是模组没睡着）。 */
void     m1_gps_sleep_for_hibernate(uint8_t variant);
const char* m1_gps_sleep_variant_name(uint8_t variant);   // 给屏幕/串口显示当前测试的是哪种
void     m1_gps_dbg_line(char* out, size_t n);   // 诊断1：板型/状态/接收计数（大字）
void     m1_gps_dbg_line2(char* out, size_t n);  // 诊断2：最后字节 + UART 线空闲电平（大字）
void     m1_gps_dbg_line3(char* out, size_t n);  // 诊断3：关键引脚电平/波特率（小字）

/* 串口接收活动统计：用于判断"完全没数据"还是"有数据但解析不了" */
void     m1_gps_note_rx();              // 在主循环里、解析之前调用
uint32_t m1_gps_rx_count();             // 有数据可读的次数
uint8_t  m1_gps_last_byte();            // 最近看到的第一个字节（'$'=0x24 说明是 NMEA）

/* 串口旁路统计：把 GPS 串口的数据“tee”一份出来，给屏幕显示用 */
Stream&  m1_gps_stream();               // 传给 NMEA 解析器的 Stream（内部转发到 Serial1）
uint32_t m1_gps_total_bytes();          // 累计收到的字节数
uint32_t m1_gps_dollar_count();         // 累计收到的 '$' 个数（NMEA 句子的标志）
uint32_t m1_gps_gga_count();            // 累计收到的 GGA 句数（MicroNMEA 只解析 GGA/RMC）
void     m1_gps_kick_output();          // 主动发命令让模块输出 GGA/RMC
void     m1_gps_tick();                 // 主循环里调用：超过 5 秒没有 GGA 就尝试打开输出
void     m1_gps_last_bytes(char* out, size_t n);   // 最近 4 个字节的十六进制

#endif  // THINKNODE_M1
