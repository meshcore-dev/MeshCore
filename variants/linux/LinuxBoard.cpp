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
  if (config.lora_irq_pin != -1) {
    initGPIOPin(config.lora_irq_pin, "gpiochip0", config.lora_irq_pin);
  }
  if (config.lora_reset_pin != -1) {
    initGPIOPin(config.lora_reset_pin, "gpiochip0", config.lora_reset_pin);
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

    else if (strcmp(key, "lora_irq_pin") == 0)   lora_irq_pin = atoi(value);
    else if (strcmp(key, "lora_reset_pin") == 0) lora_reset_pin = atoi(value);

    else if (strcmp(key, "advert_name") == 0)    advert_name = safe_copy(value, 100);
    else if (strcmp(key, "admin_password") == 0) admin_password = safe_copy(value, 100);
    else if (strcmp(key, "lat") == 0)            lat = atof(value);
    else if (strcmp(key, "lon") == 0)            lon = atof(value);
  }
  fclose(f);
  return 0;
}
