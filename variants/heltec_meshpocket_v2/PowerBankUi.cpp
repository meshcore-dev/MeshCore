#include "PowerBankUi.h"
#include "HeltecMeshPocketV2Board.h"
#include <helpers/ui/NV3001BDisplay.h>

PowerBankUi::PowerBankUi(HeltecMeshPocketV2Board& board)
    : board(board), secondaryButton(PIN_BUTTON2, 1000, true, true, false) {}

void PowerBankUi::begin() {
  secondaryButton.begin();
}

uint8_t PowerBankUi::pollEvents() {
  uint8_t events = 0;
  const int buttonEvent = secondaryButton.check();
  if (buttonEvent == BUTTON_EVENT_CLICK) events |= EVENT_SHOW_PAGE;
#ifdef MESHPOCKET_V2_USB_SWITCHABLE
  if (buttonEvent == BUTTON_EVENT_LONG_PRESS) events |= EVENT_PAGE_ACTION;
#endif
  if (board.powerBank.takePageEvent()) events |= EVENT_REQUEST_FOCUS;
  return events;
}

void PowerBankUi::handlePageAction() {
#ifdef MESHPOCKET_V2_USB_SWITCHABLE
  board.toggleUsbData();
#endif
}

uint32_t PowerBankUi::render(NV3001BDisplay& display, bool fullRefresh) {
  if (fullRefresh) page.invalidate();
  display.setNativePixels(true);
  page.draw(display, board.powerBank.snapshot(), board.isUsbDataEnabled());
  display.setNativePixels(false);
  return 500;
}
