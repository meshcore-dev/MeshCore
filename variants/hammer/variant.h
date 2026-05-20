// Hammer Board Variant Header

// OLED
#define I2C_SDA 21
#define I2C_SCL 22
#define HAS_SCREEN
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1  // No reset pin
#define OLED_I2C_ADDR 0x3C

// GPS (u-blox)
#define GPS_RX_PIN 15
#define GPS_TX_PIN 12
#define GPS_UBLOX
#define GPS_BAUDRATE 9600
#define HAS_GPS

// Power and Buttons
#define EXT_PWR_DETECT 4
#define BUTTON_PIN 39
#define SECOND_BUTTON_PIN 0  // Boot button often used as second
#define BATTERY_PIN 35
#define ADC_CHANNEL ADC1_GPIO35_CHANNEL
#define ADC_MULTIPLIER 1.85

// LoRa (E22 on VSPI)
#define USE_SX1262  // Primary for E22-900M30S
#define USE_SX1268  // Optional for E22-400M30S
#define SX126X_MAX_POWER 22
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#define TCXO_OPTIONAL

#define SX126X_CS 18
#define SX126X_SCK 5
#define SX126X_MOSI 27
#define SX126X_MISO 19
#define SX126X_RESET 23
#define SX126X_DIO1 33
#define SX126X_BUSY 32
#define SX126X_TXEN 13
#define SX126X_RXEN 14

// Compatibility defines (for RadioLib)
#define P_LORA_NSS SX126X_CS
#define P_LORA_SCLK SX126X_SCK
#define P_LORA_MOSI SX126X_MOSI
#define P_LORA_MISO SX126X_MISO
#define P_LORA_RESET SX126X_RESET
#define P_LORA_DIO_1 SX126X_DIO1
#define P_LORA_BUSY SX126X_BUSY

// Ethernet (W5500 on VSPI - shared with LoRa)
// HAS_ETHERNET is defined per-variant in platformio.ini, not here
#define ETH_PHY_TYPE ETH_PHY_W5500
#define ETH_CS_PIN 16
#define ETH_RST_PIN 17
#define ETH_INT_PIN -1  // Not connected
#define ETH_SPI_HOST VSPI_HOST  // Shared with LoRa
#define ETH_SCLK SX126X_SCK    // 5
#define ETH_MOSI SX126X_MOSI   // 27
#define ETH_MISO SX126X_MISO   // 19
