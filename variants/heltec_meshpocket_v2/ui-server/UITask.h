// Based on src/helpers/ui/server/UITask.h at upstream 88bbae29.
#pragma once

#include <helpers/ui/BaseUITask.h>
#include <helpers/CommonCLI.h>
#include "HeltecMeshPocketV2Board.h"
#include "PowerBankUi.h"

class UITask : public BaseUITask {
  PowerBankUi powerUi;
  bool _power_page_selected = false;
  bool _power_page_full_refresh = true;
#ifdef MESHPOCKET_V2_ROOM_SERVER
  unsigned long _next_read = 0;
  int _prevBtnState;
#endif
  NodePrefs* _node_prefs;
  char _version_info[32];
  const char* _role_label;
  unsigned long _powering_off_at = 0;
  unsigned long _started_at = 0;

  int readPrimaryButton();
  void showPowerPage();

protected:
  long renderCurrScreen() override;

public:
  UITask(HeltecMeshPocketV2Board& board, DisplayDriver* display) : BaseUITask(&board, display), powerUi(board) {}
  void begin(NodePrefs* node_prefs, const char* build_date, const char* firmware_version, const char* role_lbl);

  void loop() override;
};
