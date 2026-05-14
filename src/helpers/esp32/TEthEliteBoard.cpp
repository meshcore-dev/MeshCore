#if defined(T_ETH_ELITE_SX1262) || defined(T_ETH_ELITE_SX1276)

#include <Arduino.h>
#include <WiFi.h>
#include "esp_eth.h"
#include "esp_netif.h"
#include "esp_netif_defaults.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "TEthEliteBoard.h"
#include "target.h"
#include "helpers/ui/MomentaryButton.h"
#include <esp_task_wdt.h>

// Build IP addresses in lwIP's internal format (network byte order stored as LE uint32_t).
// Using a variadic macro so that comma-expanded build flags like ETH_STATIC_IP work:
// IP4_NBO(ETH_STATIC_IP) expands to _ip4_make(192,168,254,21) after arg expansion.
static inline uint32_t _ip4_make(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return ((uint32_t)d << 24) | ((uint32_t)c << 16) | ((uint32_t)b << 8) | (uint32_t)a;
}
#define IP4_NBO(...) _ip4_make(__VA_ARGS__)

extern MomentaryButton user_btn;

uint32_t deviceOnline = 0x00;
String eth_local_ip;  // global, accessible from ESP32Board.cpp via extern

#ifdef USE_ETHERNET
static esp_netif_t     *eth_netif  = NULL;
static esp_eth_handle_t eth_handle = NULL;

// Refresh eth_local_ip from the current netif state.
static void updateLocalIpString() {
    esp_netif_ip_info_t info = {};
    esp_netif_get_ip_info(eth_netif, &info);
    char buf[16];
    esp_ip4addr_ntoa(&info.ip, buf, sizeof(buf));
    eth_local_ip = String(buf);
}

// Apply static IP config. All uint32_t params are already in network byte order
// (ie. in the format accepted by esp_ip4_addr_t.addr). Pass dns_nbo=0 to skip DNS.
static void applyStaticIp(uint32_t ip_nbo, uint32_t gw_nbo, uint32_t mask_nbo, uint32_t dns_nbo) {
    esp_netif_dhcpc_stop(eth_netif);

    esp_netif_ip_info_t info = {};
    info.ip.addr      = ip_nbo;
    info.gw.addr      = gw_nbo;
    info.netmask.addr = mask_nbo;
    esp_netif_set_ip_info(eth_netif, &info);

    if (dns_nbo != 0) {
        esp_netif_dns_info_t dns = {};
        dns.ip.u_addr.ip4.addr = dns_nbo;
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        esp_netif_set_dns_info(eth_netif, ESP_NETIF_DNS_MAIN, &dns);
    }

    updateLocalIpString();
}
#endif

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

    if (esp_reset_reason() == ESP_RST_DEEPSLEEP) {
      uint64_t wakeup_source = esp_sleep_get_ext1_wakeup_status();
      if (wakeup_source & (1ULL << P_LORA_WAKE_DIO)) {
        startup_reason = BD_STARTUP_RX_PACKET;
      }
      rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
      rtc_gpio_deinit((gpio_num_t)P_LORA_WAKE_DIO);
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
#ifdef USE_ETHERNET
    // Arduino-ESP32 3.x does not initialize these until WiFi is actively used.
    // The native Ethernet path needs them up-front. Each returns ESP_ERR_INVALID_STATE
    // on second call — treat that as success.
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(err);
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(err);
    // W5500 driver calls gpio_isr_handler_add() on ETH_INT; needs ISR service running.
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(err);

    // Initialize SPI2 (FSPI) bus with W5500 pins. The LoRa radio uses the
    // default Arduino SPIClass, which on ESP32-S3 is HSPI → SPI3 — so W5500
    // must stay on SPI2 to keep the two drivers on separate buses.
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num   = ETH_MOSI;
    buscfg.miso_io_num   = ETH_MISO;
    buscfg.sclk_io_num   = ETH_SCLK;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // W5500 SPI device: 16-bit address phase + 8-bit control byte
    spi_device_interface_config_t spi_devcfg = {};
    spi_devcfg.command_bits   = 16;
    spi_devcfg.address_bits   = 8;
    spi_devcfg.mode           = 0;
    spi_devcfg.clock_speed_hz = 20 * 1000 * 1000;
    spi_devcfg.spics_io_num   = ETH_CS;
    spi_devcfg.queue_size     = 20;

    spi_device_handle_t spi_handle = NULL;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &spi_devcfg, &spi_handle));

    // W5500 MAC + PHY via native ESP-IDF driver. Bump the receive task stack:
    // the default 2048 bytes overflows under load (observed w5500_tsk stack canary).
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    mac_config.rx_task_stack_size = 4096;
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr       = ETH_ADDR;
    phy_config.reset_gpio_num = ETH_RST;

    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(spi_handle);
    w5500_config.int_gpio_num = ETH_INT;

    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &eth_handle));

    // W5500 has no factory MAC — derive one from the ESP32 efuse base MAC and
    // program it into the driver. Without this, DHCP never completes and the
    // ETH interface transmits with MAC 00:00:00:00:00:00.
    uint8_t mac_addr[6];
    ESP_ERROR_CHECK(esp_read_mac(mac_addr, ESP_MAC_ETH));
    ESP_ERROR_CHECK(esp_eth_ioctl(eth_handle, ETH_CMD_S_MAC_ADDR, mac_addr));

    // Create default ETH netif and attach driver glue
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    eth_netif = esp_netif_new(&netif_cfg);
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));

    ESP_ERROR_CHECK(esp_eth_start(eth_handle));

    // Wait for link up (netif up means PHY link is established)
    unsigned long t0 = millis();
    while (!esp_netif_is_netif_up(eth_netif) && millis() - t0 < 5000) {
        esp_task_wdt_reset();
        delay(100);
    }

    // Wait for DHCP IP assignment
    esp_netif_ip_info_t ip_info = {};
    t0 = millis();
    while (ip_info.ip.addr == 0 && millis() - t0 < 5000) {
        esp_task_wdt_reset();
        esp_netif_get_ip_info(eth_netif, &ip_info);
        delay(100);
    }

    if (ip_info.ip.addr == 0) {
#ifdef ETH_STATIC_IP
        Serial.println("DHCP timeout, using static IP from build flags");
  #ifdef ETH_DNS
        applyStaticIp(IP4_NBO(ETH_STATIC_IP), IP4_NBO(ETH_GATEWAY),
                      IP4_NBO(ETH_SUBNET),    IP4_NBO(ETH_DNS));
  #else
        applyStaticIp(IP4_NBO(ETH_STATIC_IP), IP4_NBO(ETH_GATEWAY),
                      IP4_NBO(ETH_SUBNET),    0);
  #endif
#else
        Serial.println("DHCP timeout, using fallback IP");
        applyStaticIp(IP4_NBO(192, 168, 4, 2), IP4_NBO(192, 168, 4, 1),
                      IP4_NBO(255, 255, 255, 0), 0);
#endif
    } else {
        updateLocalIpString();
    }

    Serial.printf("ETH MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac_addr[0], mac_addr[1], mac_addr[2],
                  mac_addr[3], mac_addr[4], mac_addr[5]);
    Serial.printf("ETH IP %s\n", eth_local_ip.c_str());
    Serial.println(esp_netif_is_netif_up(eth_netif) ? "ETH LINK UP" : "ETH LINK DOWN");
#endif
}

#ifdef USE_ETHERNET
// Packed big-endian uint32 (b1 in MSB) → network-byte-order as stored in ip4_addr_t.addr
// on little-endian ESP32. Host→net byte swap achieves exactly this layout.
static inline uint32_t packedToNbo(uint32_t packed) {
    return __builtin_bswap32(packed);
}
#endif

void TEthEliteBoard::reconfigureEthernet(uint32_t ip, uint32_t gw, uint32_t subnet, uint32_t dns1) {
#ifdef USE_ETHERNET
    if (ip == 0 || eth_netif == NULL) return;

    applyStaticIp(packedToNbo(ip), packedToNbo(gw),
                  packedToNbo(subnet), dns1 ? packedToNbo(dns1) : 0);
    Serial.printf("ETH reconfigured to %s\n", eth_local_ip.c_str());
#endif
}

#endif
