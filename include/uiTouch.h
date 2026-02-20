/*******************************************************************************
 * Touch libraries:
 * FT6X36: https://github.com/strange-v/FT6X36.git
 * GT911: https://github.com/TAMCTec/gt911-arduino.git
 * XPT2046: https://github.com/PaulStoffregen/XPT2046_Touchscreen.git
 ******************************************************************************/

/* uncomment for FT6X36 */
// #define TOUCH_FT6X36
// #define TOUCH_FT6X36_SCL 38//19
// #define TOUCH_FT6X36_SDA 37//18
// #define TOUCH_FT6X36_INT 4//39
// #define TOUCH_SWAP_XY
// #define TOUCH_MAP_X1 800
// #define TOUCH_MAP_X2 0
// #define TOUCH_MAP_Y1 0
// #define TOUCH_MAP_Y2 480

/* uncomment for GT911 */
//  #define TOUCH_GT911
//  #define TOUCH_GT911_SCL 20//20
//  #define TOUCH_GT911_SDA 19//19
//  #define TOUCH_GT911_INT -1//-1
//  #define TOUCH_GT911_RST -1//38
//  #define TOUCH_GT911_ROTATION ROTATION_NORMAL
//  #define TOUCH_MAP_X1 800//480
//  #define TOUCH_MAP_X2 0
//  #define TOUCH_MAP_Y1 480//272
//  #define TOUCH_MAP_Y2 0

/* uncomment for XPT2046 */
// #define TOUCH_XPT2046
// #define TOUCH_XPT2046_SCK 12
// #define TOUCH_XPT2046_MISO 13
// #define TOUCH_XPT2046_MOSI 11
// #define TOUCH_XPT2046_CS 38
// #define TOUCH_XPT2046_INT 36
// #define TOUCH_XPT2046_ROTATION 0
// #define TOUCH_MAP_X1 4000
// #define TOUCH_MAP_X2 100
// #define TOUCH_MAP_Y1 100
// #define TOUCH_MAP_Y2 4000

LGFX lcd;

int touch_last_x = 0, touch_last_y = 0;

#if defined(TOUCH_FT6X36)
#include <Wire.h>
#include <FT6X36.h>
FT6X36 ts(&Wire, TOUCH_FT6X36_INT);
bool touch_touched_flag = true, touch_released_flag = true;

#elif defined(TOUCH_GT911)
#include <Wire.h>
#include <TAMC_GT911.h>
TAMC_GT911 ts = TAMC_GT911(TOUCH_GT911_SDA, TOUCH_GT911_SCL, TOUCH_GT911_INT, TOUCH_GT911_RST, max(TOUCH_MAP_X1, TOUCH_MAP_X2), max(TOUCH_MAP_Y1, TOUCH_MAP_Y2));

#elif defined(TOUCH_XPT2046)
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
XPT2046_Touchscreen ts(TOUCH_XPT2046_CS, TOUCH_XPT2046_INT);

#endif

#if defined(TOUCH_FT6X36)
void touch(TPoint p, TEvent e)
{
  if (e != TEvent::Tap && e != TEvent::DragStart && e != TEvent::DragMove && e != TEvent::DragEnd)
  {
    return;
  }
  // translation logic depends on screen rotation
#if defined(TOUCH_SWAP_XY)
  touch_last_x = map(p.y, TOUCH_MAP_X1, TOUCH_MAP_X2, 0, lcd.width());
  touch_last_y = map(p.x, TOUCH_MAP_Y1, TOUCH_MAP_Y2, 0, lcd.height());
#else
  touch_last_x = map(p.x, TOUCH_MAP_X1, TOUCH_MAP_X2, 0, lcd.width());
  touch_last_y = map(p.y, TOUCH_MAP_Y1, TOUCH_MAP_Y2, 0, lcd.height());
#endif
  switch (e)
  {
  case TEvent::Tap:
    Serial.println("Tap");
    touch_touched_flag = true;
    touch_released_flag = true;
    break;
  case TEvent::DragStart:
    Serial.println("DragStart");
    touch_touched_flag = true;
    break;
  case TEvent::DragMove:
    Serial.println("DragMove");
    touch_touched_flag = true;
    break;
  case TEvent::DragEnd:
    Serial.println("DragEnd");
    touch_released_flag = true;
    break;
  default:
    Serial.println("UNKNOWN");
    break;
  }
}
#endif

void touch_init()
{
#if defined(TOUCH_FT6X36)
  Wire.begin(TOUCH_FT6X36_SDA, TOUCH_FT6X36_SCL);
  ts.begin();
  ts.registerTouchHandler(touch);

#elif defined(TOUCH_GT911)
  Wire.begin(TOUCH_GT911_SDA, TOUCH_GT911_SCL);
  ts.begin();
  ts.setRotation(TOUCH_GT911_ROTATION);

#elif defined(TOUCH_XPT2046)
  SPI.begin(TOUCH_XPT2046_SCK, TOUCH_XPT2046_MISO, TOUCH_XPT2046_MOSI, TOUCH_XPT2046_CS);
  ts.begin();
  ts.setRotation(TOUCH_XPT2046_ROTATION);

#endif
}

bool touch_has_signal()
{
#if defined(TOUCH_FT6X36)
  ts.loop();
  return touch_touched_flag || touch_released_flag;

#elif defined(TOUCH_GT911)
  return true;

#elif defined(TOUCH_XPT2046)
  return ts.tirqTouched();

#else
  return false;
#endif
}

bool touch_touched()
{
#if defined(TOUCH_FT6X36)
  if (touch_touched_flag)
  {
    touch_touched_flag = false;
    return true;
  }
  else
  {
    return false;
  }

#elif defined(TOUCH_GT911)
  ts.read();
  if (ts.isTouched)
  {
#if defined(TOUCH_SWAP_XY)
    touch_last_x = map(ts.points[0].y, TOUCH_MAP_X1, TOUCH_MAP_X2, 0, lcd.width() - 1);
    touch_last_y = map(ts.points[0].x, TOUCH_MAP_Y1, TOUCH_MAP_Y2, 0, lcd.height() - 1);
#else
    touch_last_x = map(ts.points[0].x, TOUCH_MAP_X1, TOUCH_MAP_X2, 0, lcd.width() - 1);
    touch_last_y = map(ts.points[0].y, TOUCH_MAP_Y1, TOUCH_MAP_Y2, 0, lcd.height() - 1);
#endif
    return true;
  }
  else
  {
    return false;
  }

#elif defined(TOUCH_XPT2046)
  if (ts.touched())
  {
    TS_Point p = ts.getPoint();
#if defined(TOUCH_SWAP_XY)
    touch_last_x = map(p.y, TOUCH_MAP_X1, TOUCH_MAP_X2, 0, lcd.width() - 1);
    touch_last_y = map(p.x, TOUCH_MAP_Y1, TOUCH_MAP_Y2, 0, lcd.height() - 1);
#else
    touch_last_x = map(p.x, TOUCH_MAP_X1, TOUCH_MAP_X2, 0, lcd.width() - 1);
    touch_last_y = map(p.y, TOUCH_MAP_Y1, TOUCH_MAP_Y2, 0, lcd.height() - 1);
#endif
    return true;
  }
  else
  {
    return false;
  }

#else
  return false;
#endif
}

bool touch_released()
{
#if defined(TOUCH_FT6X36)
  if (touch_released_flag)
  {
    touch_released_flag = false;
    return true;
  }
  else
  {
    return false;
  }

#elif defined(TOUCH_GT911)
  return true;

#elif defined(TOUCH_XPT2046)
  return true;

#else
  return false;
#endif
}
