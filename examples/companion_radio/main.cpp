#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include "MyMesh.h"

#include "esp_log.h"
#include <lvgl.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h> 
#include <Adafruit_GFX.h>

#include "../src/fonts/fonts.h"
#include "../lvgl/lvgl.h"

#include "../include/uiExternals.h"
#include "../include/lgfx.h"
#include "../include/uiDefines.h"
#include "../include/uiManager.h"
#include "../include/uiTasks.h"
#include "uiTouch.h"

#define TAG "main"

static uint32_t screenWidth;
static uint32_t screenHeight;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t disp_draw_buf[800 * 480 / 10];
//static lv_color_t disp_draw_buf;
static lv_disp_drv_t disp_drv;

UIManager *uiManager;

SemaphoreHandle_t semaphoreData;

TwoWire I2Cone = TwoWire(0);
#ifndef SEEED_SENSECAP_INDICATOR
Adafruit_SSD1306 display = Adafruit_SSD1306(128, 64, &I2Cone, OLED_RESET);
#endif

SPIClass& spi = SPI;
uint16_t touchCalibration_x0 = 300, touchCalibration_x1 = 3600, touchCalibration_y0 = 300, touchCalibration_y1 = 3600;
uint8_t  touchCalibration_rotate = 1, touchCalibration_invert_x = 2, touchCalibration_invert_y = 0;

/* Display flushing */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);  

  //lcd.fillScreen(TFT_WHITE);
#if (LV_COLOR_16_SWAP != 0)
 lcd.pushImageDMA(area->x1, area->y1, w, h,(lgfx::rgb565_t*)&color_p->full);
#else
  lcd.pushImageDMA(area->x1, area->y1, w, h,(lgfx::rgb565_t*)&color_p->full);//
#endif

  lv_disp_flush_ready(disp);

}

void my_touchpad_read(lv_indev_drv_t * indev_driver, lv_indev_data_t * data)
{
  uint16_t x, y;

  if (lcd.getTouch(&x, &y))
  {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = x;
    data->point.y = y;
  }
  else
  {
    data->state = LV_INDEV_STATE_REL;
  }
}

// Believe it or not, this std C function is busted on some platforms!
static uint32_t _atoi(const char* sp) {
  uint32_t n = 0;
  while (*sp && *sp >= '0' && *sp <= '9') {
    n *= 10;
    n += (*sp++ - '0');
  }
  return n;
}

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <InternalFileSystem.h>
  #if defined(QSPIFLASH)
    #include <CustomLFS_QSPIFlash.h>
    DataStore store(InternalFS, QSPIFlash, rtc_clock);
  #else
  #if defined(EXTRAFS)
    #include <CustomLFS.h>
    CustomLFS ExtraFS(0xD4000, 0x19000, 128);
    DataStore store(InternalFS, ExtraFS, rtc_clock);
  #else
    DataStore store(InternalFS, rtc_clock);
  #endif
  #endif
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
  DataStore store(LittleFS, rtc_clock);
#elif defined(ESP32)
  #include <SPIFFS.h>
  DataStore store(SPIFFS, rtc_clock);
#endif

#ifdef ESP32
  #ifdef WIFI_SSID
    #include <helpers/esp32/SerialWifiInterface.h>
    SerialWifiInterface serial_interface;
    #ifndef TCP_PORT
      #define TCP_PORT 5000
    #endif
  #elif defined(BLE_PIN_CODE)
    #include <helpers/esp32/SerialBLEInterface.h>
    SerialBLEInterface serial_interface;
  #elif defined(SERIAL_RX)
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
    HardwareSerial companion_serial(1);
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(RP2040_PLATFORM)
  //#ifdef WIFI_SSID
  //  #include <helpers/rp2040/SerialWifiInterface.h>
  //  SerialWifiInterface serial_interface;
  //  #ifndef TCP_PORT
  //    #define TCP_PORT 5000
  //  #endif
  // #elif defined(BLE_PIN_CODE)
  //   #include <helpers/rp2040/SerialBLEInterface.h>
  //   SerialBLEInterface serial_interface;
  #if defined(SERIAL_RX)
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
    HardwareSerial companion_serial(1);
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(NRF52_PLATFORM)
  #ifdef BLE_PIN_CODE
    #include <helpers/nrf52/SerialBLEInterface.h>
    SerialBLEInterface serial_interface;
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(STM32_PLATFORM)
  #include <helpers/ArduinoSerialInterface.h>
  ArduinoSerialInterface serial_interface;
#else
  #error "need to define a serial interface"
#endif

/* GLOBAL OBJECTS */
// #ifdef DISPLAY_CLASS
//   #include "UITask.h"
//   UITask ui_task(&board, &serial_interface);
// #endif

StdRNG fast_rng;
SimpleMeshTables tables;
MyMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store
  //  #ifdef DISPLAY_CLASS
  //     , &ui_task
  //  #endif
);

/* END GLOBAL OBJECTS */

void halt() {
  while (1) ;
}

void configureDisplay() {
  ESP_LOGI(TAG, "Configuring display...");

  screenWidth = lcd.width();
  screenHeight = lcd.height();

  lv_disp_draw_buf_init(&draw_buf, disp_draw_buf, NULL, screenWidth * screenHeight / 10);

  /* Initialize the display */
  lv_disp_drv_init(&disp_drv);
  /* Change the following line to your display resolution */
  disp_drv.hor_res = screenWidth;
  disp_drv.ver_res = screenHeight;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  /* Initialize the (dummy) input device driver */
  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);
#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
#endif

  lcd.fillScreen(0x000000u);
}

void initializeDisplay() {
  ESP_LOGI(TAG, "Initializing display...");
  lcd.begin();
  lcd.fillScreen(0x000000u);
  lcd.setTextSize(2); 
  lcd.setRotation(1);  
  //lcd.setBrightness(127);
}

void initializeTouchScreen() {
  ESP_LOGI(TAG, "Initializing touch screen...");
  touch_init();
}

void initializeLVGL() {
  ESP_LOGI(TAG, "Initializing LVGL...");
  lv_init();
}

void initializeUI() {  

  Serial.println("initialize UI...");  
  
  /*
  #ifdef ENABLE_STARTUP_LOGO
  lv_disp_load_scr(ui_ScreenLogo);  
  for( int i=0; i < 100; i++ ){
      lv_task_handler();  
      delay(10);
  }
  lv_scr_load_anim(ui_Screen1, LV_SCR_LOAD_ANIM_MOVE_TOP, 500, 0, true);
  #else
  ui_init();
  #endif
  */
  uiManager = new UIManager();
  uiManager->setNightMode(false);  
}

void setup() {
  Serial.begin(115200);

  board.begin();

#ifdef SEEED_SENSECAP_INDICATOR
  initializeDisplay();  
  delay(200);

  initializeLVGL();
  initializeTouchScreen();
  configureDisplay();

  initializeUI();
  
  createTasks();
  lv_timer_handler();
#endif
// #ifdef DISPLAY_CLASS
//   DisplayDriver* disp = NULL;
//   if (display.begin()) {
//     disp = &display;
//     disp->startFrame();
//   #ifdef ST7789
//     disp->setTextSize(2);
//   #endif
//     disp->drawTextCentered(disp->width() / 2, 28, "Loading...");
//     disp->endFrame();
//   }
// #endif

  if (!radio_init()) { halt(); }

  fast_rng.begin(radio_get_rng_seed());

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  #if defined(QSPIFLASH)
    if (!QSPIFlash.begin()) {
      // debug output might not be available at this point, might be too early. maybe should fall back to InternalFS here?
      MESH_DEBUG_PRINTLN("CustomLFS_QSPIFlash: failed to initialize");
    } else {
      MESH_DEBUG_PRINTLN("CustomLFS_QSPIFlash: initialized successfully");
    }
  #else
  #if defined(EXTRAFS)
      ExtraFS.begin();
  #endif
  #endif
  store.begin();
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );

#ifdef BLE_PIN_CODE
  serial_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
#else
  serial_interface.begin(Serial);
#endif
  the_mesh.startInterface(serial_interface);
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  store.begin();
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );

  //#ifdef WIFI_SSID
  //  WiFi.begin(WIFI_SSID, WIFI_PWD);
  //  serial_interface.begin(TCP_PORT);
  // #elif defined(BLE_PIN_CODE)
  //   char dev_name[32+16];
  //   sprintf(dev_name, "%s%s", BLE_NAME_PREFIX, the_mesh.getNodeName());
  //   serial_interface.begin(dev_name, the_mesh.getBLEPin());
  #if defined(SERIAL_RX)
    companion_serial.setPins(SERIAL_RX, SERIAL_TX);
    companion_serial.begin(115200);
    serial_interface.begin(companion_serial);
  #else
    serial_interface.begin(Serial);
  #endif
    the_mesh.startInterface(serial_interface);
#elif defined(ESP32)
  SPIFFS.begin(true);
  store.begin();
  the_mesh.begin(
    // #ifdef DISPLAY_CLASS
    //     disp != NULL
    // #else
        false
    //#endif
  );

#ifdef WIFI_SSID
  WiFi.begin(WIFI_SSID, WIFI_PWD);
  serial_interface.begin(TCP_PORT);
#elif defined(BLE_PIN_CODE)
  serial_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
#elif defined(SERIAL_RX)
  companion_serial.setPins(SERIAL_RX, SERIAL_TX);
  companion_serial.begin(115200);
  serial_interface.begin(companion_serial);
#else
  serial_interface.begin(Serial);
#endif
  the_mesh.startInterface(serial_interface);
#else
  #error "need to define filesystem"
#endif

  sensors.begin();

// #ifdef DISPLAY_CLASS
//   ui_task.begin(disp, &sensors, the_mesh.getNodePrefs());  // still want to pass this in as dependency, as prefs might be moved
// #endif
  vTaskResume(t_core1_core);

  Serial.println("Setup completed");

  Serial.println(the_mesh.getNodeName());
  the_mesh.advert();

  Serial.print("MeshCore ");
  Serial.println(the_mesh.getFirmwareVer());
  Serial.print("Build date: ");
  Serial.println(the_mesh.getBuildDate());
}

// void loop() {
//   the_mesh.loop();
//   sensors.loop();
// // #ifdef DISPLAY_CLASS
// //   ui_task.loop();
// // #endif
//   rtc_clock.tick();
// }

void core_task(void *pvParameters) {

  vTaskSuspend(NULL);

  ESP_LOGI(TAG, "MeshCore: Task running on core %d", xPortGetCoreID());

  while (1) {    
    the_mesh.loop();
    sensors.loop();
    rtc_clock.tick();
    vTaskDelay(DELAY_CORE_TASK / portTICK_PERIOD_MS);
  }
}

void loop() {
   vTaskDelete(NULL);
}

void handleCommand(char *msg)
{
  Serial.println("Outgoing data:");
  Serial.println(msg);
  the_mesh.handleCommand(msg);
}