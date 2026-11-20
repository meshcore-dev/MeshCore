#pragma once

#include <helpers/ui/BaseUITask.h>
#include <helpers/CommonCLI.h>

class UITask : public BaseUITask {
  unsigned long _next_read;
  int _prevBtnState;
  NodePrefs* _node_prefs;
  char _version_info[32];
  const char* _role_label;
  unsigned long _powering_off_at = 0;
  unsigned long _started_at = 0;

protected:
  long renderCurrScreen() override;

public:
  UITask(mesh::MainBoard* board, DisplayDriver* display) : BaseUITask(board, display) { _next_read = 0; }
  void begin(NodePrefs* node_prefs, const char* build_date, const char* firmware_version, const char* role_lbl);

  void loop() override;
};
