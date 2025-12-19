#include "t1000e_sensors.h"

#include <Arduino.h>

#define HEATER_NTC_BX 4250   // thermistor coefficient B
#define HEATER_NTC_RP 8250   // ohm, series resistance to thermistor
#define HEATER_NTC_KA 273.15 // 25 Celsius at Kelvin
#define NTC_REF_VCC   3300   // mV, max voltage of 3V3 sensor rail
#define LIGHT_REF_VCC 2400   //

static unsigned int ntc_res2[136] = {
  113347, 107565, 102116, 96978, 92132, 87559, 83242, 79166, 75316, 71677, 68237, 64991, 61919, 59011,
  56258,  53650,  51178,  48835, 46613, 44506, 42506, 40600, 38791, 37073, 35442, 33892, 32420, 31020,
  29689,  28423,  27219,  26076, 24988, 23951, 22963, 22021, 21123, 20267, 19450, 18670, 17926, 17214,
  16534,  15886,  15266,  14674, 14108, 13566, 13049, 12554, 12081, 11628, 11195, 10780, 10382, 10000,
  9634,   9284,   8947,   8624,  8315,  8018,  7734,  7461,  7199,  6948,  6707,  6475,  6253,  6039,
  5834,   5636,   5445,   5262,  5086,  4917,  4754,  4597,  4446,  4301,  4161,  4026,  3896,  3771,
  3651,   3535,   3423,   3315,  3211,  3111,  3014,  2922,  2834,  2748,  2666,  2586,  2509,  2435,
  2364,   2294,   2228,   2163,  2100,  2040,  1981,  1925,  1870,  1817,  1766,  1716,  1669,  1622,
  1578,   1535,   1493,   1452,  1413,  1375,  1338,  1303,  1268,  1234,  1202,  1170,  1139,  1110,
  1081,   1053,   1026,   999,   974,   949,   925,   902,   880,   858,
};

static char ntc_temp2[136] = {
  -30, -29, -28, -27, -26, -25, -24, -23, -22, -21, -20, -19, -18, -17, -16, -15, -14, -13, -12, -11,
  -10, -9,  -8,  -7,  -6,  -5,  -4,  -3,  -2,  -1,  0,   1,   2,   3,   4,   5,   6,   7,   8,   9,
  10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,
  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,  48,  49,
  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,  63,  64,  65,  66,  67,  68,  69,
  70,  71,  72,  73,  74,  75,  76,  77,  78,  79,  80,  81,  82,  83,  84,  85,  86,  87,  88,  89,
  90,  91,  92,  93,  94,  95,  96,  97,  98,  99,  100, 101, 102, 103, 104, 105,
};

static float get_heater_temperature(unsigned int vcc_volt, unsigned int ntc_volt) {
  int i = 0;
  float Vout = 0, Rt = 0, temp = 0;
  Vout = ntc_volt;

  Rt = (HEATER_NTC_RP * vcc_volt) / Vout - HEATER_NTC_RP;

  for (i = 0; i < 136; i++) {
    if (Rt >= ntc_res2[i]) {
      break;
    }
  }

  temp = ntc_temp2[i - 1] + 1 * (ntc_res2[i - 1] - Rt) / (float)(ntc_res2[i - 1] - ntc_res2[i]);

  temp = (temp * 100 + 5) / 100;
  return temp;
}

static int get_light_lv(unsigned int light_volt) {
  float Vout = 0, Vin = 0, Rt = 0, temp = 0;
  unsigned int light_level = 0;

  // Seeed's firmware maps the photocell reading to a 0-100 % range rather than lux.
  if (light_volt <= 80) {
    light_level = 0;
    return light_level;
  } else if (light_volt >= 2480) {
    light_level = 100;
    return light_level;
  }
  Vout = light_volt;
  light_level = 100 * (Vout - 80) / LIGHT_REF_VCC;

  return light_level;
}

float t1000e_get_temperature(void) {
  unsigned int ntc_v, vcc_v;

  digitalWrite(PIN_3V3_EN, HIGH);
  digitalWrite(SENSOR_EN, HIGH);
  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  delay(10);
  unsigned int rail_v = (1000.0 * (analogRead(BATTERY_PIN) * ADC_MULTIPLIER * AREF_VOLTAGE)) / 4096;
  vcc_v = (rail_v > NTC_REF_VCC) ? NTC_REF_VCC : rail_v;
  ntc_v = (1000.0 * AREF_VOLTAGE * analogRead(TEMP_SENSOR)) / 4096;
  digitalWrite(PIN_3V3_EN, LOW);
  digitalWrite(SENSOR_EN, LOW);

  return get_heater_temperature(vcc_v, ntc_v);
}

uint32_t t1000e_get_light(void) {
  int lux = 0;
  unsigned int lux_v = 0;

  digitalWrite(PIN_3V3_EN, HIGH);
  digitalWrite(SENSOR_EN, HIGH);
  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  delay(10);
  lux_v = 1000 * analogRead(LUX_SENSOR) * AREF_VOLTAGE / 4096;
  lux = get_light_lv(lux_v);
  digitalWrite(SENSOR_EN, LOW);
  digitalWrite(PIN_3V3_EN, LOW);

  return lux;
}
