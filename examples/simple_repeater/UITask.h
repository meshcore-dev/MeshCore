#pragma once

#include <helpers/ui/DisplayDriver.h>
#include <helpers/CommonCLI.h>

class MyMesh;  // fwd decl (defined in MyMesh.h) - keeps this header light

class UITask {
  mesh::MainBoard* _board;
  DisplayDriver* _display;
  MyMesh* _mesh = nullptr;
  unsigned long _next_read, _next_refresh, _auto_off;
  int _prevBtnState;
  NodePrefs* _node_prefs;
  char _version_info[32];
  unsigned long _powering_off_at = 0;
  unsigned long _started_at = 0;
  bool _is_eink = false;   // richer, multi-page layout is used on large e-ink panels
  uint8_t _page = 0;       // currently displayed page (e-ink only)

  void renderCurrScreen();
  void renderRichScreen();   // large e-ink multi-page dashboard
public:
  UITask(mesh::MainBoard& board, DisplayDriver& display) : _board(&board), _display(&display) { _next_read = _next_refresh = 0; }
  void begin(MyMesh* mesh, NodePrefs* node_prefs, const char* build_date, const char* firmware_version);

  void loop();
};
