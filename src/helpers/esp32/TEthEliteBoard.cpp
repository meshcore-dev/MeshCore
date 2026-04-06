#if defined(T_ETH_ELITE_SX1262) || defined(T_ETH_ELITE_SX1276)

#include <Arduino.h>
#include <SPI.h>
#include "driver/spi_common.h"
#include "driver/gpio.h"
#include <ETHClass2.h>
#include "TEthEliteBoard.h"
#include "target.h"
#include "helpers/ui/MomentaryButton.h"
#include <esp_task_wdt.h>

extern MomentaryButton user_btn;

uint32_t deviceOnline = 0x00;
static SPIClass spi_eth(FSPI);
static ETHClass2 ETH;
String eth_local_ip;  // global, accessible from ESP32Board.cpp via extern

void TEthEliteBoard::begin() {
    ESP32Board::begin();
    user_btn.begin();
    Wire1.begin(PIN_BOARD_SDA1, PIN_BOARD_SCL1);
    #if ENV_INCLUDE_GPS
        Serial1.begin(9600, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
    #endif

#ifdef USE_ETHERNET
    WiFi.mode(WIFI_OFF);
#endif
    startNetwork();

    pinMode(P_LORA_TX_LED, OUTPUT);
    digitalWrite(P_LORA_TX_LED, LOW);

    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_DEEPSLEEP) {
#if defined(T_ETH_ELITE_SX1262)
      long wakeup_source = esp_sleep_get_ext1_wakeup_status();
      if (wakeup_source & (1 << P_LORA_DIO_1)) {
        startup_reason = BD_STARTUP_RX_PACKET;
      }
      rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
      rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1);
#elif defined(T_ETH_ELITE_SX1276)
      long wakeup_source = esp_sleep_get_ext1_wakeup_status();
      if (wakeup_source & (1 << P_LORA_DIO_0)) {
        startup_reason = BD_STARTUP_RX_PACKET;
      }
      rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
      rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_0);
#endif
    }
}

#ifdef MESH_DEBUG
void TEthEliteBoard::scanDevices(TwoWire *w)
{
    uint8_t err, addr;
    int nDevices = 0;

    Serial.println("Scanning I2C for Devices");
    for (addr = 1; addr < 127; addr++) {
        w->beginTransmission(addr); delay(2);
        err = w->endTransmission();
        if (err == 0) {
            nDevices++;
            switch (addr) {
            case 0x77:
            case 0x76:
                Serial.println("\tFound BME280 Sensor");
                deviceOnline |= BME280_ONLINE;
                break;
            case 0x34:
                Serial.println("\tFound AXP192/AXP2101 PMU");
                deviceOnline |= POWERMANAGE_ONLINE;
                break;
            case 0x3C:
                Serial.println("\tFound SSD1306/SH1106 display");
                deviceOnline |= DISPLAY_ONLINE;
                break;
            case 0x51:
                Serial.println("\tFound PCF8563 RTC");
                deviceOnline |= PCF8563_ONLINE;
                break;
            case 0x1C:
                Serial.println("\tFound QMC6310 MAG Sensor");
                deviceOnline |= QMC6310_ONLINE;
                break;
            default:
                Serial.printf("\tI2C device found at address 0x%02X\n", addr);
                break;
            }
        } else if (err == 4) {
            Serial.printf("Unknown error at address 0x%02X\n", addr);
        }
    }
    if (nDevices == 0)
        Serial.println("No I2C devices found");

    Serial.println("Scan complete.");
    Serial.printf("GPS RX pin: %d, GPS TX pin: %d\n", PIN_GPS_RX, PIN_GPS_TX);
}

void TEthEliteBoard::printPMU()
{
    Serial.print("isCharging:"); Serial.println(PMU->isCharging() ? "YES" : "NO");
    Serial.print("isDischarge:"); Serial.println(PMU->isDischarge() ? "YES" : "NO");
    Serial.print("isVbusIn:"); Serial.println(PMU->isVbusIn() ? "YES" : "NO");
    Serial.print("getBattVoltage:"); Serial.print(PMU->getBattVoltage()); Serial.println("mV");
    Serial.print("getVbusVoltage:"); Serial.print(PMU->getVbusVoltage()); Serial.println("mV");
    Serial.print("getSystemVoltage:"); Serial.print(PMU->getSystemVoltage()); Serial.println("mV");
    if (PMU->isBatteryConnect()) {
        Serial.print("getBatteryPercent:"); Serial.print(PMU->getBatteryPercent()); Serial.println("%");
    }
}
#endif

void TEthEliteBoard::startNetwork() {
#ifdef USE_ETHERNET
    startEthernet();
#elif defined(WIFI_SSID)
    startWifi();
#endif
}

void TEthEliteBoard::startEthernet() {
    pinMode(ETH_CS, OUTPUT);
    digitalWrite(ETH_CS, HIGH);

    ETH.begin(ETH_PHY_W5500, ETH_ADDR, ETH_CS, ETH_INT, -1, SPI2_HOST, ETH_SCLK, ETH_MISO, ETH_MOSI);
    delay(100);

    unsigned long t0 = millis();
    while (!ETH.linkUp() && millis() - t0 < 5000) {
        esp_task_wdt_reset();
        delay(100);
    }

    t0 = millis();
    while (ETH.localIP() == IPAddress(0, 0, 0, 0) && millis() - t0 < 5000) {
        esp_task_wdt_reset();
        delay(100);
    }

    if (ETH.localIP() == IPAddress(0, 0, 0, 0)) {
#ifdef ETH_STATIC_IP
        Serial.println("DHCP timeout, using static IP from build flags");
        ETH.config(IPAddress(ETH_STATIC_IP), IPAddress(ETH_GATEWAY), IPAddress(ETH_SUBNET), IPAddress(ETH_DNS));
#else
        Serial.println("DHCP timeout, using fallback IP");
        ETH.config(IPAddress(192, 168, 4, 2), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
#endif
    }

    eth_local_ip = ETH.localIP().toString();  // save IP for OTA use

    uint8_t mac[6];
    ETH.macAddress(mac);
    Serial.printf("ETH MAC %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.print("ETH IP "); Serial.println(ETH.localIP());
    Serial.println(ETH.linkUp() ? "ETH LINK UP" : "ETH LINK DOWN");
}
void TEthEliteBoard::reconfigureEthernet(uint32_t ip, uint32_t gw, uint32_t subnet, uint32_t dns1) {
  if (ip != 0) {
    uint8_t b1 = (ip >> 24) & 0xFF;
    uint8_t b2 = (ip >> 16) & 0xFF;
    uint8_t b3 = (ip >> 8) & 0xFF;
    uint8_t b4 = ip & 0xFF;

    uint8_t gw1 = (gw >> 24) & 0xFF;
    uint8_t gw2 = (gw >> 16) & 0xFF;
    uint8_t gw3 = (gw >> 8) & 0xFF;
    uint8_t gw4 = gw & 0xFF;

    uint8_t sub1 = (subnet >> 24) & 0xFF;
    uint8_t sub2 = (subnet >> 16) & 0xFF;
    uint8_t sub3 = (subnet >> 8) & 0xFF;
    uint8_t sub4 = subnet & 0xFF;

    uint8_t dns_1 = (dns1 >> 24) & 0xFF;
    uint8_t dns_2 = (dns1 >> 16) & 0xFF;
    uint8_t dns_3 = (dns1 >> 8) & 0xFF;
    uint8_t dns_4 = dns1 & 0xFF;

    bool ok = ETH.config(
      IPAddress(b1, b2, b3, b4),
      IPAddress(gw1, gw2, gw3, gw4),
      IPAddress(sub1, sub2, sub3, sub4),
      IPAddress(dns_1, dns_2, dns_3, dns_4)
    );
    if (ok) {
      Serial.printf("ETH reconfigured to %d.%d.%d.%d\n", b1, b2, b3, b4);
    } else {
      Serial.println("ETH reconfigure failed");
    }
    eth_local_ip = ETH.localIP().toString();
  }
}
#endif
