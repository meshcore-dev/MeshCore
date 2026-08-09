#pragma once

#include <helpers/ui/DisplayDriver.h>
#include <helpers/CommonCLI.h>

class MyMesh;  // fwd decl (defined in MyMesh.h) - keeps this header light

class UITask {
  DisplayDriver* _display;
  MyMesh* _mesh;
  unsigned long _next_read, _next_refresh, _auto_off, _btn_down_at;
  int _prevBtnState;
  NodePrefs* _node_prefs;
  char _version_info[32];
  bool _is_eink;      // richer, multi-page layout is used on large e-ink panels
  uint8_t _page;      // currently displayed page (e-ink only)

  void renderCurrScreen();
  void renderBootScreen();
  void renderCompactScreen();   // 128x64 OLED layout (unchanged legacy view)
  void renderRichScreen();      // large e-ink multi-page layout
public:
  UITask(DisplayDriver& display)
    : _display(&display), _mesh(NULL), _node_prefs(NULL), _is_eink(false), _page(0)
  {
    _next_read = _next_refresh = _auto_off = _btn_down_at = 0;
  }
  void begin(MyMesh* mesh, NodePrefs* node_prefs, const char* build_date, const char* firmware_version);

  void loop();
};
