#pragma once

#include <helpers/ui/MomentaryButton.h>
#include "PowerBankPage.h"

class HeltecMeshPocketV2Board;
class NV3001BDisplay;

class PowerBankUi {
  HeltecMeshPocketV2Board& board;
  PowerBankPage page;
  MomentaryButton secondaryButton;

public:
  enum Event : uint8_t {
    EVENT_SHOW_PAGE = 1 << 0,
    EVENT_PAGE_ACTION = 1 << 1,
    EVENT_REQUEST_FOCUS = 1 << 2,
  };

  explicit PowerBankUi(HeltecMeshPocketV2Board& board);
  void begin();
  uint8_t pollEvents();
  void handlePageAction();
  uint32_t render(NV3001BDisplay& display, bool fullRefresh);
};
