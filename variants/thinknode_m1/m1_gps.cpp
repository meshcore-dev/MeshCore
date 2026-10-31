#include "m1_gps.h"

#ifdef THINKNODE_M1

#include <HardwareSerial.h>   // nRF52: Serial1
#include <nrf.h>
#include <nrf_gpio.h>

#ifndef M1_GPS_BAUD_OLD
  #define M1_GPS_BAUD_OLD     9600     // L76K 默认
#endif
#ifndef M1_GPS_BAUD_NEW
  #define M1_GPS_BAUD_NEW     115200   // AG3352 默认（未确认时自动扫描）
#endif
#ifndef M1_GPS_SNIFF_MS
  #define M1_GPS_SNIFF_MS     1200
#endif
#ifndef M1_GPS_NEW_SNIFF_MS
  #define M1_GPS_NEW_SNIFF_MS 800      // 新板矩阵探测用（组合多，窗口短一些）
#endif

#define M1_BAT_ADC_PIN        4          // P0.04/AIN2，旧板上与 P0.08 经 R43=0Ω 直连
#define M1_GPS_INT_PIN       11          // P0.11：V2.0 网络 GPS_INT（0Ω 到 U18.GPIO11）
                                         // 新gps 说明要求“发命令前把它拉低”

static M1GpsScheme s_scheme = M1_GPS_UNKNOWN;
static uint32_t    s_baud   = M1_GPS_BAUD_OLD;
static bool        s_board_checked = false;
static bool        s_is_new_board  = false;   // true = V2.0（AG3352）
static int8_t      s_uart_swap     = -1;      // 0=正常(RX41/TX40) 1=交换(-1=未定)
static int8_t      s_pwr_pol       = -1;      // 0=P1.01 高有效 1=低有效(-1=未定)
static uint32_t    s_rx_count      = 0;       // 串口有数据的次数
static uint8_t     s_last_byte     = 0;       // 最近看到的第一个字节
static bool        s_alive         = false;   // GPS 对命令有回包（链路 OK，只是没吐 NMEA）
static uint32_t    s_alive_baud    = 0;
static int8_t      s_alive_swap    = -1;
static int8_t      s_alive_pol     = -1;
static char        s_line109       = '?';     // P1.09(GPS_RX) 空闲电平：H 驱动高 / L 驱动低 / F 悬空
static char        s_line108       = '?';     // P1.08(GPS_TX) 同上
static uint32_t    s_start_ms      = 0;       // GPS 启动时刻（用于“没 GGA 就踢一脚”）
static int8_t      s_cycle_idx     = -1;      // 新板配置轮换索引
static uint32_t    s_next_cycle    = 0;
static bool        s_new_output_pending = false;
static uint8_t     s_new_extra_kicks = 0;

static void gps_send_cmd(const char* body);   // 前置声明
static void apply_uart(int8_t ord, uint32_t baud);   // 前置声明

/* 采样一根线的“空闲状态”：真正的 UART 空闲是**被驱动为高**。
   内部下拉仍读高 ⇒ 外部把它拉高（H）；内部上拉仍读低 ⇒ 外部拉低（L）；跟随上下拉 ⇒ 悬空（F）。 */
static char sample_line_state(int pin) {
  pinMode(pin, INPUT_PULLDOWN);
  delay(2);
  bool pd = digitalRead(pin);
  pinMode(pin, INPUT_PULLUP);
  delay(2);
  bool pu = digitalRead(pin);
  pinMode(pin, INPUT);
  if (pd && pu) return 'H';
  if (!pd && !pu) return 'L';
  return 'F';
}

void m1_gps_note_rx() {
  if (Serial1.available() > 0) {
    s_rx_count++;
    int c = Serial1.peek();
    if (c >= 0) s_last_byte = (uint8_t) c;
  }
}

uint32_t m1_gps_rx_count() { return s_rx_count; }
uint8_t  m1_gps_last_byte() { return s_last_byte; }

/* ---------------- 串口旁路统计（tee） ----------------
   把 GPS 串口包一层，NMEA 解析器照常读，同时统计总字节数、'$' 个数和最近几个字节。
   这样屏幕上就能直接判断：是不是根本没数据 / 数据是不是 NMEA / 波特率对不对。 */
static uint32_t s_total_bytes = 0;
static uint32_t s_dollars     = 0;
static uint8_t  s_recent[4]   = {0, 0, 0, 0};
static uint8_t  s_recent_idx  = 0;
static char     s_last_sentence[40] = {0};   // 最近一条完整 NMEA 句子（截断）
static char     s_last_gga[44]      = {0};   // 最近一条 GGA（MicroNMEA 只看 GGA/RMC）
static char     s_last_rmc[44]      = {0};
static uint32_t s_gga_count = 0, s_rmc_count = 0;
static uint16_t s_line_len    = 0;
static uint32_t s_sentences   = 0;

class M1TeeStream : public Stream {
public:
  int available() override { return Serial1.available(); }
  int peek() override { return Serial1.peek(); }
  int read() override {
    int c = Serial1.read();
    if (c >= 0) {
      s_total_bytes++;
      if (c == '$') {
        s_dollars++;
        s_line_len = 0;                       // 新句子开始
        s_last_sentence[0] = '$';
        s_last_sentence[1] = 0;
        s_line_len = 1;
      } else if (c == '\n') {
        if (s_line_len > 8) {
          s_sentences++;
          // MicroNMEA 只解析 GGA / RMC，其它句子（GSV/GSA/ZDA…）会被忽略
          const char* id = &s_last_sentence[3];
          if (s_line_len >= 6 && !strncmp(id, "GGA", 3)) {
            strncpy(s_last_gga, s_last_sentence, sizeof(s_last_gga) - 1);
            s_last_gga[sizeof(s_last_gga) - 1] = 0;
            s_gga_count++;
          } else if (s_line_len >= 6 && !strncmp(id, "RMC", 3)) {
            strncpy(s_last_rmc, s_last_sentence, sizeof(s_last_rmc) - 1);
            s_last_rmc[sizeof(s_last_rmc) - 1] = 0;
            s_rmc_count++;
          }
        }
        s_line_len = 0;
      } else if (c != '\r' && s_line_len > 0 && s_line_len < sizeof(s_last_sentence) - 1) {
        s_last_sentence[s_line_len++] = (char) c;
        s_last_sentence[s_line_len] = 0;
      }
      s_recent[s_recent_idx & 3] = (uint8_t) c;
      s_recent_idx++;
    }
    return c;
  }
  void flush() override { Serial1.flush(); }
  size_t write(uint8_t b) override { return Serial1.write(b); }
  size_t write(const uint8_t* buf, size_t len) override { return Serial1.write(buf, len); }
};

static M1TeeStream s_tee;
Stream& m1_gps_stream() { return s_tee; }

uint32_t m1_gps_total_bytes() { return s_total_bytes; }
uint32_t m1_gps_dollar_count() { return s_dollars; }
uint32_t m1_gps_gga_count() { return s_gga_count; }

/* 主动让模块输出 GGA/RMC（MicroNMEA 只解析这两种句子）。
   模块若默认只发 GSV/ZDA，这里就会把定位句打开。 */
void m1_gps_kick_output() {
  gps_send_cmd("PAIR062,0,1");     // GGA 1Hz
  delay(30);
  gps_send_cmd("PAIR062,4,1");     // RMC 1Hz
  delay(30);
  gps_send_cmd("PMTK314,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0");
  delay(30);
}

void m1_gps_tick() {
  if (!s_start_ms) return;
  uint32_t now = millis();

  // 旧板（L76K）已实测稳定：只在前 5 秒没 GGA 时补发一次唤醒命令
  if (s_scheme != M1_GPS_AG3352) {
    static bool old_kicked = false;
    if (!old_kicked && now - s_start_ms > 5000 && s_gga_count == 0) {
      old_kicked = true;
      m1_gps_kick_output();
    }
    return;
  }

  // Send output setup after boot without blocking the UI on NMEA reception.
  if (s_new_output_pending && now - s_start_ms >= 1000) {
    s_new_output_pending = false;
    m1_gps_kick_output();
  }

  // 新板：拿到 GGA 就锁定配置；否则在后台轮换 UART 配置。
  // 这样不再依赖开机时那一次 800ms 的一次性探测（NMEA 1Hz，相位很容易错过）。
  if (s_gga_count > 0) {
    if (s_new_extra_kicks < 2 && now - s_start_ms > 6000 + s_new_extra_kicks * 6000) {
      s_new_extra_kicks++;
      m1_gps_kick_output();               // 再补两次，确保定位句稳定输出
    }
    return;
  }

  if (now - s_start_ms < 3000) return;    // 给模块启动时间
  if (now < s_next_cycle) return;
  s_next_cycle = now + 3000;

  static const uint32_t bauds[] = { M1_GPS_BAUD_NEW, 9600, 38400 };
  s_cycle_idx = (s_cycle_idx + 1) % 6;
  int8_t ord = (s_cycle_idx >= 3) ? 1 : 0;
  uint32_t baud = bauds[s_cycle_idx % 3];

  pinMode(M1_GPS_PWR_NEW_PIN, OUTPUT);
  digitalWrite(M1_GPS_PWR_NEW_PIN, HIGH);        // 0.8V 使能（高有效）
  pinMode(M1_GPS_DETECT_PIN, INPUT);
  pinMode(M1_GPS_INT_PIN, INPUT_PULLUP);
  apply_uart(ord, baud);
  s_baud = baud;
  s_uart_swap = ord;
  s_pwr_pol = 0;
  delay(30);
  m1_gps_kick_output();
}

void m1_gps_last_bytes(char* out, size_t n) {
  uint8_t idx = s_recent_idx;
  uint8_t a = s_recent[(idx + 0) & 3], b = s_recent[(idx + 1) & 3];
  uint8_t c = s_recent[(idx + 2) & 3], d = s_recent[(idx + 3) & 3];
  snprintf(out, n, "%02X %02X %02X %02X", a, b, c, d);
}

/* ---------------- 板型识别（硬件签名，不靠猜） ----------------
   旧板(V1.5)：P0.08 经 R43 = 0Ω 直连 BAT_ADC(P0.04)
   新板(V2.0)：P0.08 只挂 K5 与 R10=100K 上拉到 VCC3V3
   ⇒ 把 P0.04 输出高、P0.08 用内部下拉读：
        旧板会被 0Ω 拉高 → 读高；新板被 13K 下拉压到约 0.4V → 读低。 */
static bool detect_new_board() {
  if (s_board_checked) return s_is_new_board;

  pinMode(M1_BAT_ADC_PIN, OUTPUT);
  digitalWrite(M1_BAT_ADC_PIN, HIGH);
  pinMode(M1_GPS_SW_NEW_PIN, INPUT_PULLDOWN);
  bool tied = digitalRead(M1_GPS_SW_NEW_PIN) == HIGH;
  pinMode(M1_BAT_ADC_PIN, INPUT);           // 还原 ADC 输入
  pinMode(M1_GPS_SW_NEW_PIN, INPUT);

  s_is_new_board = !tied;
  s_board_checked = true;
  return s_is_new_board;
}

/* ---------------- 开关状态 ----------------
   旧板：P1.01 经 2MΩ 接 VCC_GPS（机械开关合上才有电）→ 普通输入，高=开
   新板：P0.08 高=开，低=关；按实机要求与 V1.5 的滑动方向一致。
   厂商 readme 的低有效定义与所需机械方向相反，以实机反馈为准。 */
static bool switch_old_on() {
  pinMode(M1_GPS_SW_OLD_PIN, INPUT);          // 源阻抗 2MΩ，绝不能加上下拉
  return digitalRead(M1_GPS_SW_OLD_PIN) == HIGH;
}

static bool switch_new_on() {
  pinMode(M1_GPS_SW_NEW_PIN, INPUT_PULLUP);
  bool on = digitalRead(M1_GPS_SW_NEW_PIN) == HIGH;   // same switch direction as V1.5
  pinMode(M1_GPS_SW_NEW_PIN, INPUT);
  return on;
}

bool m1_gps_switch_on() {
#ifdef M1_GPS_DISABLED
  /* 诊断用：把 GPS 整套编译期关掉（等价于 Meshtastic 的 // #define HAS_GPS 1 被注释）。
     这样固件从不初始化 GPS 引脚、从不打开 Serial1、从不和模组通信 —— 用于验证
     "300 µA 到底是不是 GPS 子系统造成的"。 */
  return false;
#else
  if (detect_new_board()) {
    return switch_new_on();
  }
  return switch_old_on();
#endif  // M1_GPS_DISABLED
}

static bool s_ready = false;    // m1_gps_init() 是否执行过（懒初始化）

void m1_gps_init() {
#ifdef M1_GPS_DISABLED
  return;                        // 诊断用：完全不初始化 GPS 引脚
#else
  pinMode(M1_GPS_SW_OLD_PIN, INPUT);
  pinMode(M1_GPS_SW_NEW_PIN, INPUT);
  pinMode(M1_GPS_DETECT_PIN, INPUT);
  pinMode(M1_GPS_RESET_PIN, INPUT);

  detect_new_board();                     // 只在开机做一次，结果缓存
  if (s_is_new_board) {
    s_scheme = M1_GPS_AG3352;
    s_baud = M1_GPS_BAUD_NEW;
  } else {
    s_scheme = M1_GPS_L76K;
    s_baud = M1_GPS_BAUD_OLD;
  }
  s_ready = true;

  char line[64];
  m1_gps_dbg_line(line, sizeof(line));
  Serial.printf("GPS pins: %s\n", line);
#endif  // M1_GPS_DISABLED
}

bool m1_gps_ready() { return s_ready; }

M1GpsScheme m1_gps_scheme() { return detect_new_board() ? M1_GPS_AG3352 : M1_GPS_L76K; }

const char* m1_gps_scheme_name() {
  return detect_new_board() ? "AG3352(new)" : "L76K(old)";
}

uint32_t m1_gps_baud() { return s_baud; }

void m1_gps_dbg_line(char* out, size_t n) {
  // NODATA=完全没数据 / ALIVE=能收到回包但不吐 NMEA / DATA=有字节进来
  const char* state = (s_total_bytes == 0 && s_rx_count == 0 && !s_alive) ? "NODATA"
                    : (s_dollars == 0) ? "NO-NMEA" : "NMEA";
  snprintf(out, n, "%s %s", detect_new_board() ? "NEW" : "OLD", state);
}

void m1_gps_dbg_line2(char* out, size_t n) {
  // G/R = 收到的 GGA / RMC 句数（MicroNMEA 只解析这两种）
  snprintf(out, n, "%luB G%lu R%lu", (unsigned long) s_total_bytes,
           (unsigned long) s_gga_count, (unsigned long) s_rmc_count);
}

void m1_gps_dbg_line3(char* out, size_t n) {
  // 优先显示 GGA（含定位质量/卫星数），没有再退回最近一条其它句子
  if (s_last_gga[0]) snprintf(out, n, "%s", s_last_gga);
  else if (s_last_rmc[0]) snprintf(out, n, "%s", s_last_rmc);
  else snprintf(out, n, "%s", s_last_sentence);
}

/* ---------------- NMEA 探测 ---------------- */
static bool nmea_sniff(uint32_t duration_ms) {
  int dollars = 0;
  bool pending = false;
  uint32_t start = millis();
  while (millis() - start < duration_ms) {
    while (Serial1.available()) {
      int c = Serial1.read();
      if (c == '$') { dollars++; pending = true; }
      else if (pending) {
        if (c >= 'A' && c <= 'Z' && dollars >= 2) return true;
        pending = false;
      }
    }
    delay(1);
  }
  return false;
}

/* UART 是否真的开着（自己记账，不依赖 Serial1 的 operator bool）。
   ★ 这就是"开 GPS 休眠 30mA"的根因之一：stop_gps()/m1_gps_power_off() 会先
   serial_end_safe() 把 UART 关掉，之后 m1_gps_power_off() 里的 gps_send_cmd() 因为
   UART 已关而直接 return —— L76K **从来没收到过待机命令**，一直连续定位（≈28mA）。 */
static bool s_uart_open = false;

/* 关闭 UARTE 必须判空：adfruit 核的 Uart::end() 会等 TXSTOPPED/RXTO 事件，
   对已经关闭的 UART 再调一次可能死等。 */
static void serial_end_safe() {
  if (Serial1) Serial1.end();
  s_uart_open = false;
}

/* ord: 0 = RX=P1.09(41)/TX=P1.08(40)（与旧板一致）；1 = 交换 */
static void apply_uart(int8_t ord, uint32_t baud) {
  serial_end_safe();
  if (ord == 0) Serial1.setPins(41, 40);
  else          Serial1.setPins(40, 41);
  Serial1.begin(baud);
  s_uart_open = true;
}

static bool sniff_at(uint32_t baud, uint32_t ms) {
  serial_end_safe();
  Serial1.begin(baud);
  s_uart_open = true;
  if (nmea_sniff(ms)) { s_baud = baud; return true; }
  return false;
}

/* 休眠路径专用：确保 UART 真的开着（必要时按当前板型/波特率重新打开），
   这样 gps_send_cmd() 才真的能把待机命令发给模组。
   ★ 之所以必须重新打开：休眠流程里 shutdownPeripherals() → stop_gps() 已经把 UARTE
   关掉了，而 UART 关着时 gps_send_cmd() 是直接 return 的 —— 现场"开 GPS 休眠 30mA"
   就是 L76K 从来没收到过待机命令、一直连续定位造成的。 */
static void uart_ensure_open() {
  if (s_uart_open) return;
  int8_t  ord  = (s_uart_swap < 0) ? 0 : s_uart_swap;
  if (s_baud == 0) s_baud = detect_new_board() ? M1_GPS_BAUD_NEW : M1_GPS_BAUD_OLD;
  apply_uart(ord, s_baud);
  delay(30);
}

static void reset_pulse_old() {
  pinMode(M1_GPS_RESET_PIN, OUTPUT);
  digitalWrite(M1_GPS_RESET_PIN, LOW);    // 旧板：低=复位
  delay(100);
  digitalWrite(M1_GPS_RESET_PIN, HIGH);   // 释放
  delay(80);
}

static void reset_pulse_new() {
  pinMode(M1_GPS_RESET_PIN, OUTPUT);
  digitalWrite(M1_GPS_RESET_PIN, HIGH);   // 新板：高=复位（经 Q16 拉低 RESETB）
  delay(100);
  digitalWrite(M1_GPS_RESET_PIN, LOW);    // 释放
  delay(80);
}

/* 计算 NMEA 语句校验和并发送（自动补 *HH\r\n） */
static void gps_send_cmd(const char* body) {   // body 形如 "PMTK000" / "PAIR062,0,1"
  if (!s_uart_open) return;                    // UART 已关时别写（会卡在已释放的信号量上）
  uint8_t cs = 0;
  for (const char* p = body; *p; p++) cs ^= (uint8_t) *p;
  char buf[64];
  snprintf(buf, sizeof(buf), "$%s*%02X\r\n", body, cs);
  Serial1.print(buf);
  Serial1.flush();
}

/* 探活：发一条“测试”命令，只要能收到任意回包，就说明 UART 方向/波特率/供电都对了 */
static bool uart_ping(uint32_t baud, int8_t ord) {
  apply_uart(ord, baud);
  delay(20);
  gps_send_cmd("PMTK000");          // MTK/L76K 系测试命令
  delay(80);
  gps_send_cmd("PAIR000");          // Airoha(AG3352) 系常见握手
  uint32_t t0 = millis();
  while (millis() - t0 < 400) {
    if (Serial1.available() > 0) {
      int c = Serial1.peek();
      if (c >= 0) s_last_byte = (uint8_t) c;
      s_rx_count++;
      return true;
    }
    delay(1);
  }
  return false;
}

/* 让模块开始按 1Hz 输出常用 NMEA（MTK 与 Airoha 两套命令都发一遍，不支持的会被忽略） */
static void enable_nmea_output() {
  gps_send_cmd("PMTK314,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0");
  delay(30);
  gps_send_cmd("PAIR062,0,1");
  delay(30);
  gps_send_cmd("PAIR062,4,1");
  delay(30);
  gps_send_cmd("PAIR050,1000");
  delay(50);
}

/* 新板矩阵探测：供电极性 × UART 顺序 × 波特率，找到能出 NMEA 的组合 */
static bool probe_new_board() {
  static const uint32_t pass1[] = { M1_GPS_BAUD_NEW, M1_GPS_BAUD_OLD, 38400 };
  static const uint32_t pass2[] = { 57600, 460800, 4800 };   // 少见但有可能

  // P0.11(GPS_INT)：正常应保持高（说明里只在“发命令前”才拉低），这里默认上拉
  pinMode(M1_GPS_INT_PIN, INPUT_PULLUP);

  for (int8_t pol = 0; pol < 2; pol++) {
    pinMode(M1_GPS_PWR_NEW_PIN, OUTPUT);
    digitalWrite(M1_GPS_PWR_NEW_PIN, pol == 0 ? HIGH : LOW);
    pinMode(M1_GPS_DETECT_PIN, INPUT);
    delay(60);
    reset_pulse_new();
    delay(500);                                   // AG3352 启动比 L76K 慢

    for (int8_t ord = 0; ord < 2; ord++) {
      // 先探活（能收到回包说明链路 OK），再听 NMEA
      bool alive = false;
      for (uint32_t b : pass1) {
        if (uart_ping(b, ord)) {
          alive = true;
          s_alive = true; s_alive_baud = b; s_alive_swap = ord; s_alive_pol = pol;
          s_baud = b;
          break;
        }
      }
      for (uint32_t b : pass1) {
        apply_uart(ord, b);
        if (nmea_sniff(M1_GPS_NEW_SNIFF_MS)) {
          s_uart_swap = ord; s_pwr_pol = pol; s_baud = b;
          Serial.printf("GPS probe OK: pol=%d uart_swap=%d baud=%lu alive=%d\n",
                        (int) pol, (int) ord, (unsigned long) b, (int) alive);
          return true;
        }
        Serial.printf("GPS probe: pol=%d swap=%d baud=%lu -> none (alive=%d)\n",
                      (int) pol, (int) ord, (unsigned long) b, (int) alive);
      }
    }
  }

  // 第二遍：默认供电极性 + 试试“复位极性相反”（万一 Q16 那路与图不同）
  for (int8_t rst_hi = 0; rst_hi < 2; rst_hi++) {
    pinMode(M1_GPS_PWR_NEW_PIN, OUTPUT);
    digitalWrite(M1_GPS_PWR_NEW_PIN, HIGH);
    pinMode(M1_GPS_INT_PIN, INPUT_PULLUP);
    pinMode(M1_GPS_RESET_PIN, OUTPUT);
    digitalWrite(M1_GPS_RESET_PIN, rst_hi ? HIGH : LOW);
    delay(400);
    for (int8_t ord = 0; ord < 2; ord++) {
      for (uint32_t b : pass2) {
        apply_uart(ord, b);
        if (nmea_sniff(M1_GPS_NEW_SNIFF_MS)) {
          s_uart_swap = ord; s_pwr_pol = 0; s_baud = b;
          Serial.printf("GPS probe OK(2): rst_hi=%d swap=%d baud=%lu\n",
                        (int) rst_hi, (int) ord, (unsigned long) b);
          return true;
        }
      }
    }
  }

  // 第三遍：如果模块“活着但不吐 NMEA”，主动打开输出再听一次
  if (s_alive && s_alive_swap >= 0) {
    pinMode(M1_GPS_PWR_NEW_PIN, OUTPUT);
    digitalWrite(M1_GPS_PWR_NEW_PIN, s_alive_pol == 0 ? HIGH : LOW);
    delay(50);
    apply_uart(s_alive_swap, s_alive_baud);
    enable_nmea_output();
    if (nmea_sniff(2500)) {
      s_uart_swap = s_alive_swap; s_pwr_pol = s_alive_pol; s_baud = s_alive_baud;
      Serial.printf("GPS probe OK(3): after enable-nmea\n");
      return true;
    }
  }

  return false;
}

/* ---------------- 两套启动流程 ----------------
   要求：一个固件必须新旧 GPS 都能用，所以不依赖"先判板型"——
   先按判定结果启动，1.2 秒内没数据就换另一套，谁先出数据就用谁。 */

static bool start_old_path(bool wait_for_data) {
  apply_uart(0, M1_GPS_BAUD_OLD);
  pinMode(M1_GPS_SW_OLD_PIN, INPUT);          // 2MΩ 检测脚保持高阻
  pinMode(M1_GPS_EN_OLD_PIN, OUTPUT);
  digitalWrite(M1_GPS_EN_OLD_PIN, HIGH);      // WAKE_UP = 高（工作）
  pinMode(M1_GPS_RESET_PIN, OUTPUT);
  digitalWrite(M1_GPS_RESET_PIN, HIGH);       // RESET_N = 高（运行）
  s_baud = M1_GPS_BAUD_OLD;
  reset_pulse_old();
  return wait_for_data ? nmea_sniff(1200) : true;
}

static bool start_new_path(bool full_probe) {
  pinMode(M1_GPS_INT_PIN, INPUT_PULLUP);      // GPS_INT 常态高
  if (full_probe) {
    return probe_new_board();
  }
  // V2.0 power enable is known to be active high. Reuse the last working
  // UART settings; discover missing NMEA in tick(), never in the UI gesture.
  s_pwr_pol = 0;
  if (s_uart_swap < 0) s_uart_swap = 0;
  if (s_baud == 0) s_baud = M1_GPS_BAUD_NEW;
  pinMode(M1_GPS_PWR_NEW_PIN, OUTPUT);
  digitalWrite(M1_GPS_PWR_NEW_PIN, s_pwr_pol == 0 ? HIGH : LOW);
  pinMode(M1_GPS_DETECT_PIN, INPUT);
  delay(60);
  reset_pulse_new();
  apply_uart(s_uart_swap, s_baud);
  static const uint32_t bauds[] = { M1_GPS_BAUD_NEW, 9600, 38400 };
  s_cycle_idx = s_uart_swap ? 3 : 0;
  for (uint8_t i = 0; i < 3; ++i) {
    if (s_baud == bauds[i]) { s_cycle_idx += i; break; }
  }
  s_next_cycle = millis() + 6000; // Allow startup and multiple NMEA periods.
  s_new_output_pending = true;
  s_new_extra_kicks = 0;
  return true; // Enabled, not a claim that a fix/NMEA has already arrived.
}

void m1_gps_power_on() {
  bool detected_new = detect_new_board();
  s_rx_count = 0;
  s_last_byte = 0;
  s_total_bytes = 0;
  s_dollars = 0;
  s_recent_idx = 0;
  memset(s_recent, 0, sizeof(s_recent));
  s_last_sentence[0] = 0;
  s_last_gga[0] = 0;
  s_last_rmc[0] = 0;
  s_gga_count = 0;
  s_rmc_count = 0;
  s_line_len = 0;
  s_sentences = 0;
  s_start_ms = millis();

  // 先把两根 UART 线的空闲电平记下来（UART 打开后再读就不准了）
  s_line109 = sample_line_state(41);   // P1.09 / GPS_RX
  s_line108 = sample_line_state(40);   // P1.08 / GPS_TX

  bool ok_old = false, ok_new = false;
  if (detected_new) {
    ok_new = start_new_path(false);           // V2.0: bounded startup, background UART discovery
  } else {
    ok_old = start_old_path(true);            // 判为旧板：先走已验证的旧路径
    if (!ok_old) ok_new = start_new_path(true);
  }

  if (ok_new) {
    s_scheme = M1_GPS_AG3352;
    Serial.printf("GPS on: AG3352(new) @ %lu (pol=%d swap=%d)\n",
                  (unsigned long) s_baud, (int) s_pwr_pol, (int) s_uart_swap);
  } else if (ok_old) {
    s_scheme = M1_GPS_L76K;
    s_baud = M1_GPS_BAUD_OLD;
    apply_uart(0, s_baud);
    Serial.printf("GPS on: L76K(old) @ 9600\n");
  } else {
    // 两套都没在 1.2 秒内出声：回到判定板型的配置继续跑（模块可能只是启动慢）
    if (detected_new) {
      s_scheme = M1_GPS_AG3352;
      if (s_pwr_pol < 0) s_pwr_pol = 0;
      if (s_uart_swap < 0) s_uart_swap = 0;
      if (s_baud == 0) s_baud = M1_GPS_BAUD_NEW;
      pinMode(M1_GPS_PWR_NEW_PIN, OUTPUT);
      digitalWrite(M1_GPS_PWR_NEW_PIN, s_pwr_pol == 0 ? HIGH : LOW);
      pinMode(M1_GPS_DETECT_PIN, INPUT);
      pinMode(M1_GPS_RESET_PIN, OUTPUT);
      digitalWrite(M1_GPS_RESET_PIN, LOW);
      apply_uart(s_uart_swap, s_baud);
    } else {
      s_scheme = M1_GPS_L76K;
      start_old_path(false);
    }
    Serial.printf("GPS on: no data at boot, kept %s @ %lu\n",
                  m1_gps_scheme_name(), (unsigned long) s_baud);
  }
}

static void gps_send_standby() {
  if (!Serial1) return;
  Serial1.print("$PMTK161,0*28\r\n");
  Serial1.flush();
  delay(20);
}

void m1_gps_power_off() {
  if (detect_new_board()) {
    gps_send_standby();                        // 先让模块进待机（readme 第 2 条）
    pinMode(M1_GPS_PWR_NEW_PIN, OUTPUT);
    digitalWrite(M1_GPS_PWR_NEW_PIN, LOW);     // 断 0.8V
  } else {
    pinMode(M1_GPS_EN_OLD_PIN, OUTPUT);
    digitalWrite(M1_GPS_EN_OLD_PIN, LOW);      // WAKE_UP 低 → L76K 睡眠
    pinMode(M1_GPS_SW_OLD_PIN, INPUT);
  }
  // 复位脚一律“释放”为高阻。新板上把它拉高会经 Q16 把 GPS_RST 拉低，
  // R85(10K 到 GPS_1V8) 变成持续漏电通路 —— 实测 30uA → 310uA 就是它。
  pinMode(M1_GPS_RESET_PIN, INPUT);
  serial_end_safe();                           // 关 UARTE（不关的话 PHY 约剩 900uA）
}

/* 关机/深度休眠：断电序列，所有 GPS 相关脚都回到不漏电的状态 */
void m1_gps_rails_off() {
  if (detect_new_board()) {
    pinMode(M1_GPS_PWR_NEW_PIN, OUTPUT);
    digitalWrite(M1_GPS_PWR_NEW_PIN, LOW);     // 断 0.8V 供电
    pinMode(M1_GPS_INT_PIN, INPUT);            // GPS_INT 释放
    pinMode(M1_GPS_DETECT_PIN, INPUT);
  } else {
    pinMode(M1_GPS_EN_OLD_PIN, OUTPUT);
    digitalWrite(M1_GPS_EN_OLD_PIN, LOW);      // WAKE_UP 低
    pinMode(M1_GPS_SW_OLD_PIN, INPUT);         // 2MΩ 检测脚保持高阻
  }
  // 复位脚释放（既不拉高也不拉低）：任何推挽驱动都可能经基极电阻或上拉漏电
  pinMode(M1_GPS_RESET_PIN, INPUT);
  serial_end_safe();                           // 关 UARTE，省约 900uA
}

/* 进 SYSTEMOFF 之前把 GPS 停到最低功耗。
   现场事实：GPS 开关开 480 µA / 关 300 µA（差 180 µA = 模组没真正睡着）。
   新板 GPS_1V8 (U23) 是硬连常供电，模组不收到睡眠命令就不会真正收住；
   而且 GPS_RST 上有 R85=10K 上拉到 GPS_1V8，模组半断电时这条通路约漏 120µA。
   readme（新gps/M1_New GPS_Readme.txt 第 2 条）：
     "The 0.8V power supply for GPS on P1.01 is cut off. Then, the NRF52840 sends a
      sleep command to the GPS module, causing the GPS to enter a low-power mode."
   但没写具体是哪一条命令，所以这里做成**候选轮换**：每次长按休眠换一种命令，
   哪种能让休眠电流掉下来就是正确的那条（变体号随休眠标记保存，下次开机自动 +1）。
   NMEA 句子发错只会被忽略；唤醒时新板路径本来就会复位模组，不会留副作用。 */

static const char* const kSleepCmd[8][2] = {
  { NULL,          NULL        },   // 0: 什么都不发（基线）
  { "PMTK161,0",   NULL        },   // 1: MediaTek 待机
  { "PAIR007,0",   NULL        },   // 2: Airoha 候选
  { "PAIR008,0",   NULL        },   // 3: Airoha 候选
  { "PAIR006,0",   NULL        },   // 4: Airoha 候选
  { "PAIR712,0",   NULL        },   // 5: Airoha 候选
  { "PAIR062,0,0", "PAIR050,0" },   // 6: 先关 NMEA 输出再降搜索
  { NULL,          NULL        },   // 7: 保留
};

const char* m1_gps_sleep_variant_name(uint8_t variant) {
  static const char* const names[8] = {
    "v0 none", "v1 PMTK161", "v2 PAIR007", "v3 PAIR008",
    "v4 PAIR006", "v5 PAIR712", "v6 PAIR062+050", "v7 -"
  };
  return names[variant & 7];
}

void serial_end_safe_public() { serial_end_safe(); }

void m1_gps_apply_sleep_pins() {
  const bool is_new = detect_new_board(); // cached before the peripheral rail is cut
  static const uint32_t signal_pins[] = {11u, 32u, 40u, 41u};
  for (uint32_t pin : signal_pins) {
    nrf_gpio_cfg_default(pin);           // UART, PPS and INT: no pull or output contention
  }
  nrf_gpio_cfg_default(M1_GPS_SW_NEW_PIN); // V1.5: BAT_ADC; V2.0: K5 input
  if (is_new) {
    nrf_gpio_pin_clear(M1_GPS_PWR_NEW_PIN);
    nrf_gpio_cfg_output(M1_GPS_PWR_NEW_PIN); // U22 0.8V off
    nrf_gpio_cfg_default(M1_GPS_DETECT_PIN); // R20 pull-down, not L76K WAKE_UP
    nrf_gpio_pin_clear(M1_GPS_RESET_PIN);
    nrf_gpio_cfg_output(M1_GPS_RESET_PIN);  // Q16 off, release AG3352 RESETB
  } else {
    nrf_gpio_cfg_default(M1_GPS_SW_OLD_PIN); // 2M switch sense, no pull
    nrf_gpio_pin_clear(M1_GPS_EN_OLD_PIN);
    nrf_gpio_cfg_output(M1_GPS_EN_OLD_PIN);  // L76K WAKE_UP stays low
#ifdef M1_HIB_GPS_RESET_LOW
    nrf_gpio_pin_clear(M1_GPS_RESET_PIN);
    nrf_gpio_cfg_output(M1_GPS_RESET_PIN);
#else
    nrf_gpio_cfg_default(M1_GPS_RESET_PIN);
#endif
  }
}

void m1_gps_sleep_for_hibernate(uint8_t variant) {
#ifdef M1_GPS_DISABLED
  (void) variant;
  return;                        // 诊断用：GPS 整套关掉时，休眠路径也不碰 GPS
#else
  uint8_t v = variant & 7;
  bool is_new = detect_new_board();

  if (!is_new) {
    /* ---- 旧板 V1.5（L76K）：GPS 由机械开关 K5 直接供电，固件断不了电 ----
       现场数据（final41）：GPS 开关打开时休眠 30mA，关闭时 1.88mA ⇒ 差值 ≈28mA
       正好是 L76K 连续定位电流 ⇒ 模组根本没睡着。两个原因一起修：
         ① UART 可能已被 stop_gps() 关掉，待机命令发不出去 → 先重新打开再发；
         ② 待机后 WAKE_UP(P1.02) 必须**保持驱动为低**，不能被后面的引脚表放开。 */
    uart_ensure_open();
    gps_send_cmd("PMTK161,0");                  // MediaTek/Quectel 待机命令
    delay(150);
    pinMode(M1_GPS_EN_OLD_PIN, OUTPUT);
    digitalWrite(M1_GPS_EN_OLD_PIN, LOW);       // WAKE_UP 低 = 模组睡眠（保持驱动）
#ifdef M1_HIB_GPS_RESET_LOW
    pinMode(M1_GPS_RESET_PIN, OUTPUT);
    digitalWrite(M1_GPS_RESET_PIN, LOW);        // 试验变体：RESET_N 低 = 复位
#else
    pinMode(M1_GPS_RESET_PIN, INPUT);           // 默认：释放 RESET_N（模组内部上拉 → 正常）
#endif
    pinMode(M1_GPS_SW_OLD_PIN, INPUT);           // 2MΩ 开关检测脚高阻
    serial_end_safe();
    delay(50);
    return;
  }

  // ① 断电前先发一遍（先关 NMEA 输出能让模组更快静下来）
  //    注意：UART 可能已被 stop_gps() 关掉，这里先确保它开着，否则命令根本发不出去
  uart_ensure_open();
  for (uint8_t i = 0; i < 2; i++) {
    if (kSleepCmd[v][i]) { gps_send_cmd(kSleepCmd[v][i]); delay(30); }
  }
  // ② 断 0.8V 供电（P1.01 → U22.EN 低）
  pinMode(M1_GPS_PWR_NEW_PIN, OUTPUT);
  digitalWrite(M1_GPS_PWR_NEW_PIN, LOW);
  pinMode(M1_GPS_DETECT_PIN, INPUT);
  delay(80);
  // ③ readme 顺序：断电后再发一遍（模组靠 VIO 还活着，收到命令才真正收住）
  for (uint8_t i = 0; i < 2; i++) {
    if (kSleepCmd[v][i]) { gps_send_cmd(kSleepCmd[v][i]); delay(30); }
  }
  // ④ 复位脚必须保持高阻（P1.05→R23 1K→Q16 基极，拉高就是 2.6mA）
  pinMode(M1_GPS_RESET_PIN, INPUT);
  serial_end_safe();
  delay(150);
#endif  // M1_GPS_DISABLED
}

#endif  // THINKNODE_M1
