"""Run the production T-Deck renderer against a recording SPI panel and PSRAM."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

SANITIZER_FLAGS = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"] if os.name != "nt" else []

ROOT = Path(__file__).resolve().parents[1]

ARDUINO = r'''
#pragma once
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#define LOW 0
#define HIGH 1
#define OUTPUT 1
#define HSPI 1
#define PIN_TFT_CS 12
#define PIN_TFT_DC 11
#define PIN_TFT_SCL 40
#define PIN_TFT_SDA 41
#define PIN_TFT_RST -1
#define PIN_TFT_LEDA_CTL 42
inline uint32_t now_us=0;
inline unsigned initializations=0, direct_pixels=0, spi_begins=0;
inline bool transaction=false, cs_high=true;
inline int levels[64]={};
inline void pinMode(int,int) {}
inline void digitalWrite(int pin,int value) { assert(pin>=0 && pin<64); levels[pin]=value; }
inline void delay(unsigned ms) { now_us+=ms*1000; }
inline uint32_t micros() { now_us+=25; return now_us; }
'''

# This stand-in supplies GFX's primitive dispatch and cursor contract. The
# implementation under test is the real TDeckCanvas and LCD driver, including
# every framebuffer write, clipping decision, strip copy and callback boundary.
GFX = r'''
#pragma once
#include <Arduino.h>
#include <algorithm>
#include <cstdlib>
#include <vector>
class Adafruit_GFX {
protected:
 const int16_t WIDTH, HEIGHT;
 int16_t cursor_x=0,cursor_y=0;
 uint8_t textsize=1;
 uint16_t textcolor=0xffff;
public:
 Adafruit_GFX(int w,int h):WIDTH(w),HEIGHT(h) {}
 virtual ~Adafruit_GFX()=default;
 virtual void drawPixel(int16_t,int16_t,uint16_t)=0;
 virtual void drawFastHLine(int16_t x,int16_t y,int16_t w,uint16_t c) {
   for(int i=0;i<w;++i) drawPixel(x+i,y,c);
 }
 virtual void drawFastVLine(int16_t x,int16_t y,int16_t h,uint16_t c) {
   for(int i=0;i<h;++i) drawPixel(x,y+i,c);
 }
 virtual void fillRect(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t c) {
   for(int row=0;row<h;++row) drawFastHLine(x,y+row,w,c);
 }
 virtual void fillScreen(uint16_t c) { fillRect(0,0,WIDTH,HEIGHT,c); }
 virtual void writeFastHLine(int16_t x,int16_t y,int16_t w,uint16_t c) {drawFastHLine(x,y,w,c);}
 virtual void writeFastVLine(int16_t x,int16_t y,int16_t h,uint16_t c) {drawFastVLine(x,y,h,c);}
 virtual void writeFillRect(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t c) {fillRect(x,y,w,h,c);}
 void drawRect(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t c) {
   if(w<=0 || h<=0)return;
   drawFastHLine(x,y,w,c); drawFastHLine(x,y+h-1,w,c);
   drawFastVLine(x,y,h,c); drawFastVLine(x+w-1,y,h,c);
 }
 void drawBitmap(int16_t x,int16_t y,const uint8_t* bits,int16_t w,int16_t h,uint16_t c) {
   for(int row=0;row<h;++row)for(int col=0;col<w;++col)
     if(bits[row*((w+7)/8)+col/8] & (0x80>>(col&7)))drawPixel(x+col,y+row,c);
 }
 void setTextWrap(bool) {}
 void cp437(bool) {}
 void setTextSize(uint8_t size) {textsize=size;}
 void setTextColor(uint16_t c) {textcolor=c;}
 void setCursor(int16_t x,int16_t y) {cursor_x=x;cursor_y=y;}
 int16_t getCursorX() const {return cursor_x;}
 int16_t getCursorY() const {return cursor_y;}
 void getTextBounds(const char* s,int16_t x,int16_t y,int16_t* bx,int16_t* by,uint16_t* w,uint16_t* h) {
   *bx=x;*by=y;*w=strlen(s)*6*textsize;*h=*w?8*textsize:0;
 }
 void print(const char* s) {
   while(*s++) {writeFillRect(cursor_x,cursor_y,5*textsize,7*textsize,textcolor);cursor_x+=6*textsize;}
 }
};
'''

PANEL = r'''
#pragma once
#include <Adafruit_GFX.h>
#include <SPI.h>
#define ST77XX_BLACK 0
#define ST77XX_WHITE 0xffff
#define ST77XX_BLUE 0x001f
#define ST77XX_ORANGE 0xfd20
#define ST77XX_CYAN 0x07ff
inline std::vector<uint16_t> panel(320*240);
struct Window {int x,y,w,h;};
inline std::vector<Window> windows;
class Adafruit_ST7789:public Adafruit_GFX {
 int _w=320,_h=240;
 Window window{};
public:
 template<class... T> explicit Adafruit_ST7789(T...):Adafruit_GFX(320,240) {}
 void init(int,int) {++initializations;}
 void setRotation(int r) {_w=(r&1)?320:240;_h=(r&1)?240:320;}
 int width()const{return _w;} int height()const{return _h;}
 void setSPISpeed(double) {}
 void drawPixel(int16_t x,int16_t y,uint16_t c) override {
   ++direct_pixels;
   if(x>=0 && y>=0 && x<_w && y<_h)panel[size_t(y)*_w+x]=c;
 }
 void startWrite() {assert(!transaction && cs_high);transaction=true;cs_high=false;}
 void setAddrWindow(int x,int y,int w,int h) {
   assert(transaction && !cs_high && x>=0 && y>=0 && w>0 && h>0 && x+w<=_w && y+h<=_h);
   window={x,y,w,h};windows.push_back(window);
 }
 void writePixels(uint16_t* pixels,uint32_t count,bool blocking) {
   assert(transaction && !cs_high && blocking && count==unsigned(window.w*window.h) && count<=960);
   for(int row=0;row<window.h;++row)
     std::copy_n(pixels+row*window.w,window.w,panel.begin()+(window.y+row)*_w+window.x);
 }
 void endWrite() {assert(transaction && !cs_high);transaction=false;cs_high=true;}
};
'''

HEAP = r'''
#pragma once
#include <Arduino.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
inline bool fail_psram=false;
inline unsigned allocation_calls=0, frees=0;
inline size_t allocated_bytes=0;
inline uint8_t* allocation=nullptr;
inline void* heap_caps_calloc(size_t count,size_t size,unsigned caps) {
 ++allocation_calls;assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 if(fail_psram)return nullptr;
 assert(!allocation);allocated_bytes=count*size;
 allocation=static_cast<uint8_t*>(malloc(allocated_bytes+64));
 memset(allocation,0xA5,allocated_bytes+64);memset(allocation+32,0,allocated_bytes);
 return allocation+32;
}
inline void heap_caps_free(void* pointer) {
 if(!pointer)return;
 assert(pointer==allocation+32);
 for(unsigned i=0;i<32;++i)assert(allocation[i]==0xA5 && allocation[allocated_bytes+32+i]==0xA5);
 free(allocation);allocation=nullptr;++frees;
}
'''

CONSOLE = r'''
#pragma once
#include <Arduino.h>
inline std::vector<std::string> warnings;
struct Console {void println(const char* text){warnings.emplace_back(text);}};
inline Console Serial;
'''

def write_stubs(directory):
    (directory / "helpers").mkdir(exist_ok=True)
    for name, source in {
        "Arduino.h": ARDUINO + CONSOLE.replace("#include <Arduino.h>", ""), "Adafruit_GFX.h": GFX,
        "Adafruit_ST7789.h": PANEL, "esp_heap_caps.h": HEAP,
        "Wire.h": "#pragma once\nclass TwoWire {};\n",
        "SPI.h": "#pragma once\n#include <Arduino.h>\nstruct SPIClass {explicit SPIClass(int=0) {} void begin(int,int,int,int){++spi_begins;}};\ninline SPIClass SPI;\n",
    }.items():
        (directory / name).write_text(source)


MAIN = r'''
#include <helpers/ui/ST7789LCDDisplay.h>
#include <esp_heap_caps.h>
inline unsigned callbacks=0, depth=0, max_depth=0;
inline ST7789LCDDisplay* current=nullptr;
inline bool reenter=false;
void cooperate() {
 assert(!transaction && cs_high && "radio callback must run with completed TFT transaction");
 ++callbacks;++depth;max_depth=std::max(max_depth,depth);
 if(reenter){reenter=false;current->fillRect(1,1,2,2);}
 --depth;
}
void scene(ST7789LCDDisplay& d) {
 const uint8_t bits[]={0x81,0x42};
 d.startFrame(0x1234);d.setColor(0xCAFE);
 d.fillRect(-3,-4,8,9);d.fillRect(318,237,20,20);
 d.drawRect(20,20,9,7);d.drawRect(-2,60,6,7);
 d.drawXbm(35,35,bits,8,2);d.setTextSize(2);d.setCursor(60,60);d.print("AB");d.endFrame();
}
int main(int argc,char** argv) {
 assert(argc==2);const std::string scenario=argv[1];
 fail_psram=scenario=="allocation-failure";
 {
  ST7789LCDDisplay d;current=&d;d.setRenderYieldCallback(cooperate);
  assert(!d.isBufferedRendering());assert(d.begin());
  assert(allocation_calls==1 && d.isBufferedRendering()==!fail_psram);
  if(fail_psram) {
    assert(warnings.size()==1 && warnings[0].find("PSRAM allocation failed")!=std::string::npos);
    scene(d);assert(direct_pixels>0 && callbacks==0 && windows.empty());
    d.setBufferedRendering(false);d.setBufferedRendering(true);d.turnOff();d.turnOn();
    assert(allocation_calls==1 && warnings.size()==1 && initializations==1);
  } else {
    assert(allocated_bytes==153600 && warnings.empty());
    windows.clear();const auto direct_before=direct_pixels;
    if (DISPLAY_SCALE_X == 1) {
    d.startFrame(0x1234);d.setColor(0xCAFE);d.fillRect(-3,-4,8,9);
    assert(windows.empty() && direct_pixels==direct_before && callbacks>0);
    d.endFrame();assert(windows.size()==80);
    for(const auto& w:windows)assert(w.x==0 && w.w==320 && w.h==3);
    assert(panel[0]==0xCAFE && panel[4*320+4]==0xCAFE && panel[5*320+5]==0x1234);
    windows.clear();d.fillRect(-2,238,7,9);
    assert(windows.size()==1 && windows[0].x==0 && windows[0].y==238 && windows[0].w==5 && windows[0].h==2);
    assert(panel[239*320+4]==0xCAFE && panel[239*320+5]==0x1234);
    windows.clear();d.fillRect(900,900,10,10);d.fillRect(0,0,-2,5);assert(windows.empty());
    }
    scene(d);const auto buffered=panel;const auto buffered_callbacks=callbacks;
    d.setBufferedRendering(false);assert(!d.isBufferedRendering());scene(d);
    assert(panel==buffered && callbacks==buffered_callbacks && direct_pixels>direct_before);
    d.setBufferedRendering(true);assert(d.isBufferedRendering() && allocation_calls==1);scene(d);
    assert(panel==buffered && callbacks>buffered_callbacks);
    reenter=true;d.startFrame(0x0010);d.endFrame();assert(max_depth==1 && !reenter);
    // Unframed text must flush the actual cursor rectangle, not a full screen.
    if (DISPLAY_SCALE_X == 1) {
    windows.clear();d.setTextSize(1);d.setCursor(20,30);d.print("A");
    assert(windows.size()==1 && windows[0].x==20 && windows[0].y==30 && windows[0].w==6 && windows[0].h==8);
    }
  }
  for(bool buffered:{true,false}) {
    d.setBufferedRendering(buffered);d.turnOff();
    const auto old_panel=panel;const auto draws=direct_pixels;const auto yields=callbacks;windows.clear();
    scene(d);d.clear();assert(panel==old_panel && direct_pixels==draws && callbacks==yields && windows.empty());
    assert(!d.isOn() && levels[PIN_TFT_LEDA_CTL]==LOW);
    d.turnOn();assert(d.isOn() && initializations==1 && spi_begins==1 && levels[PIN_TFT_LEDA_CTL]==HIGH);
  }
 }
 assert(allocation==nullptr && frees==unsigned(!fail_psram));
}
'''


class TDeckDisplayCooperationTests(unittest.TestCase):
    def test_production_renderer_pixels_transactions_psram_fallback_and_sleep(self):
        with tempfile.TemporaryDirectory(prefix="tdeck-display-") as temporary:
            directory = Path(temporary)
            write_stubs(directory)
            (directory / "main.cpp").write_text(MAIN)
            binary = directory / "driver"
            for sx, sy in (("1", "1"), ("2.5", "3.75")):
                result = subprocess.run([
                    os.environ.get("CXX", "g++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-parameter", *SANITIZER_FLAGS, "-DLILYGO_TDECK",
                    "-DDISPLAY_SCALE_X="+sx, "-DDISPLAY_SCALE_Y="+sy, "-I"+str(directory),
                    "-I"+str(ROOT / "src"), str(directory / "main.cpp"),
                    str(ROOT / "src/helpers/ui/ST7789LCDDisplay.cpp"), "-o", str(binary),
                ], capture_output=True, text=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
                for scenario in ("buffered", "allocation-failure"):
                    with self.subTest(scale=sx, scenario=scenario):
                        result = subprocess.run([str(binary), scenario], capture_output=True, text=True, timeout=10)
                        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)



if __name__ == "__main__":
    unittest.main()
