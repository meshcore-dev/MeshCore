#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "linux/gpio/LinuxGPIOPin.h"
#include "LinuxBoard.h"

int initGPIOPin(uint8_t pinNum, const std::string gpioChipName, uint8_t line)
{
#ifdef PORTDUINO_LINUX_HARDWARE
  char gpio_name[32];
  snprintf(gpio_name, sizeof(gpio_name), "GPIO%d", pinNum);

  try {
    GPIOPin *csPin;
    csPin = new LinuxGPIOPin(pinNum, gpioChipName.c_str(), line, gpio_name);
    csPin->setSilent();
    gpioBind(csPin);
    return 0;
  } catch (...) {
    MESH_DEBUG_PRINTLN("Warning, cannot claim pin %d", pinNum);
    return 1;
  }
#else
  return 0;
#endif
}

void portduinoSetup() {
}

void LinuxBoard::begin() {
  config.load("/etc/meshcored/meshcored.ini");

  Serial.printf("SPI begin %s\n", config.spidev);
  SPI.begin(config.spidev);

  Serial.printf("LoRa pins NSS=%d BUSY=%d IRQ=%d RESET=%d TX=%d RX=%d\n",
                (int)config.lora_nss_pin,
                (int)config.lora_busy_pin,
                (int)config.lora_irq_pin,
                (int)config.lora_reset_pin,
                (int)config.lora_rxen_pin,
                (int)config.lora_txen_pin);

  if (config.lora_nss_pin != RADIOLIB_NC) {
    initGPIOPin(config.lora_nss_pin, "gpiochip0", config.lora_nss_pin);
  }
  if (config.lora_busy_pin != RADIOLIB_NC) {
    initGPIOPin(config.lora_busy_pin, "gpiochip0", config.lora_busy_pin);
  }
  if (config.lora_irq_pin != RADIOLIB_NC) {
    initGPIOPin(config.lora_irq_pin, "gpiochip0", config.lora_irq_pin);
  }
  if (config.lora_reset_pin != RADIOLIB_NC) {
    initGPIOPin(config.lora_reset_pin, "gpiochip0", config.lora_reset_pin);
  }
  if (config.lora_rxen_pin != RADIOLIB_NC) {
    initGPIOPin(config.lora_rxen_pin, "gpiochip0", config.lora_rxen_pin);
  }
  if (config.lora_txen_pin != RADIOLIB_NC) {
    initGPIOPin(config.lora_txen_pin, "gpiochip0", config.lora_txen_pin);
  }
}

void trim(char *str) {
  char *end;
  while (isspace((unsigned char)*str)) str++;
  if (*str == 0) { *str = 0; return; }
  end = str + strlen(str) - 1;
  while (end > str && isspace((unsigned char)*end)) end--;
  end[1] = '\0';
}

char *safe_copy(char *value, size_t maxlen) {
  char *retval;
  size_t length = strlen(value) + 1;
  if (length > maxlen) length = maxlen;

  retval = (char *)malloc(length);
  strncpy(retval, value, length - 1);
  retval[length - 1] = '\0';
  return retval;
}

int LinuxConfig::load(const char *filename) {
  FILE *f = fopen(filename, "r");
  if (!f) return -1;

  char line[512];
  while (fgets(line, sizeof(line), f)) {
    char *p = line;
    // skip whitespace
    while (isspace(*p)) p++;
    // skip empty lines and comments
    if (*p == '\0' || *p == '#' || *p == ';') continue;

    char *key = p;
    while (*p && !isspace(*p) && *p != '=') p++;
    if (*p == '\0') continue;
    *p++ = '\0';

    while (*p && (isspace(*p) || *p == '=')) p++;
    char *value = p;
    p = value;
    while (*p && *p != '\n' && *p != '\r' && *p != '#' && *p != ';') p++;
    *p = '\0';

    trim(key);
    trim(value);

    if (strcmp(key, "spidev") == 0)         spidev = safe_copy(value, 32);
    else if (strcmp(key, "lora_freq") == 0) lora_freq = atof(value);
    else if (strcmp(key, "lora_bw") == 0)   lora_bw = atof(value);
    else if (strcmp(key, "lora_sf") == 0)   lora_sf = (uint8_t)atoi(value);
    else if (strcmp(key, "lora_cr") == 0)   lora_cr = (uint8_t)atoi(value);
    else if (strcmp(key, "lora_tcxo") == 0) lora_tcxo = atof(value);
    else if (strcmp(key, "lora_tx_power") == 0)   lora_tx_power = atoi(value);
    else if (strcmp(key, "current_limit") == 0)  current_limit = atof(value);
    else if (strcmp(key, "dio2_as_rf_switch") == 0)  dio2_as_rf_switch = value != 0;
    else if (strcmp(key, "rx_boosted_gain") == 0)  rx_boosted_gain = value != 0;

    else if (strcmp(key, "lora_irq_pin") == 0)   lora_irq_pin = atoi(value);
    else if (strcmp(key, "lora_reset_pin") == 0) lora_reset_pin = atoi(value);
    else if (strcmp(key, "lora_nss_pin") == 0)   lora_nss_pin = atoi(value);
    else if (strcmp(key, "lora_busy_pin") == 0)  lora_busy_pin = atoi(value);
    else if (strcmp(key, "lora_rxen_pin") == 0)  lora_rxen_pin = atoi(value);
    else if (strcmp(key, "lora_txen_pin") == 0)  lora_txen_pin = atoi(value);

    else if (strcmp(key, "advert_name") == 0)    advert_name = safe_copy(value, 100);
    else if (strcmp(key, "admin_password") == 0) admin_password = safe_copy(value, 100);
    else if (strcmp(key, "lat") == 0)            lat = atof(value);
    else if (strcmp(key, "lon") == 0)            lon = atof(value);
  }
  fclose(f);
  return 0;
}
