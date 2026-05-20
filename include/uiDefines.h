#ifndef DEFINES_h
#define DEFINES_h


#define BUTTONS_ON_SCREEN 7

#define DELAY_CORE_TASK 5
#define DELAY_LVGL_TASK 10
#define DELAY_MAIN_TASK 500
#define DELAY_CLOCK_TASK 1000
#define DELAY_NTP_TASK 30000
#define DELAY_WIFI_RECONNECT_TASK 5000
// every 5 minutes on success
#define DELAY_OPEN_WEATHER_TASK 300000
// every 1 minute on failure
#define DELAY_OPEN_WEATHER_SHORT_TASK 60000

#define STATE_OFF HIGH
#define STATE_ON LOW

#define MAX_CHAT_MESSAGES 50

// Colors
#define COLOR_WHITE 0xFFFFFF
#define COLOR_BLACK 0
#define COLOR_RED 0xFF0000
#define COLOR_BLUE 0x0077FF
#define COLOR_BLUE_LIGHT 0x22A8FD
#define COLOR_GREY 0x8F8F8F
#define BUTTON_BACKGROUND 0x1B1B1B

// Device Elecrow 5 & 7 inches
#define SD_MOSI 11
#define SD_MISO 13
#define SD_SCK 12
#define SD_CS 10

#define I2S_DOUT      17
#define I2S_BCLK      42
#define I2S_LRC       18

#define GPIO_D  38

#define UART_RX 44
#define UART_TX 43
#define I2C_SDA 19
#define I2C_SCL 20
#define LCD_Backlight	2

// Contact list: show avatar circle with initials (disable on small screens)
//#define SHOW_CONTACT_AVATAR

// Other defines
#define DELAY_MAIN_TASK 10
#define DELAY_REFRESH_VIEW 10
#define OLED_RESET     -1 

#define TFT_BL 2
#define LGFX_USE_V1

#endif
