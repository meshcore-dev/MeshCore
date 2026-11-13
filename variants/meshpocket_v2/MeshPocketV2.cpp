#include "MeshPocketV2.h"
#include "PowerBankPage.h"
#include <helpers/ui/BoardUiExtension.h>
#include <helpers/ui/MomentaryButton.h>
#include <nrf_gpio.h>

class MeshPocketV2::UiExtension : public mesh::BoardUiExtension {
  MeshPocketV2& board;
  PowerBankPage page;
  MomentaryButton secondaryButton;

public:
  explicit UiExtension(MeshPocketV2& board) :
      board(board), secondaryButton(PIN_BUTTON2, 1000, true, true, false) {}

  void begin() {
    secondaryButton.begin();
  }

  uint8_t pollEvents() override {
    uint8_t events = EVENT_NONE;
    const int buttonEvent = secondaryButton.check();
    if (buttonEvent == BUTTON_EVENT_CLICK) {
      events |= EVENT_SHOW_PAGE;
    }
#ifdef MESHPOCKET_V2_USB_SWITCHABLE
    else if (buttonEvent == BUTTON_EVENT_LONG_PRESS) {
      events |= EVENT_PAGE_ACTION;
    }
#endif
    if (board.powerBank.takePageEvent()) events |= EVENT_REQUEST_FOCUS;
    return events;
  }

#ifdef MESHPOCKET_V2_USB_SWITCHABLE
  void handlePageAction() override {
    board.toggleUsbData();
  }
#endif

  uint32_t render(DisplayDriver& display, bool fullRefresh) override {
    if (fullRefresh) page.invalidate();
    display.setNativePixels(true);
    page.draw(display, board.powerBank.snapshot(), board.isUsbDataEnabled());
    display.setNativePixels(false);
    return 500;
  }
};

void MeshPocketV2::begin() {
  NRF52Board::begin();
  powerBank.begin();
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
  uiExtension().begin();
}

MeshPocketV2::UiExtension& MeshPocketV2::uiExtension() {
  static UiExtension extension(*this);
  return extension;
}

mesh::BoardUiExtension* MeshPocketV2::getUiExtension() {
  return &uiExtension();
}

uint16_t MeshPocketV2::getBattMilliVolts() {
  const auto& status = powerBank.snapshot();
  if (status.cw.ready) return status.cw.voltageMv;
  return status.hltLive() ? status.hlt.batteryMv : 0;
}

void MeshPocketV2::setUsbDataEnabled(bool enabled) {
  digitalWrite(PIN_IO_CSA, enabled ? HIGH : LOW);
}

bool MeshPocketV2::toggleUsbData() {
  const bool enabled = !isUsbDataEnabled();
  setUsbDataEnabled(enabled);
  return enabled;
}

bool MeshPocketV2::isUsbDataEnabled() const {
  return digitalRead(PIN_IO_CSA) == HIGH;
}

void MeshPocketV2::shutdownPeripherals() {
  NRF52Board::shutdownPeripherals();
  powerBank.end();
  nrf_gpio_cfg_sense_input(PIN_USER_BTN, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
  pinMode(PIN_9006_IO1, INPUT_PULLDOWN);
  // A held-high IO1 must not immediately wake the board from SYSTEM OFF.
  const auto sense = digitalRead(PIN_9006_IO1) ? NRF_GPIO_PIN_SENSE_LOW : NRF_GPIO_PIN_SENSE_HIGH;
  nrf_gpio_cfg_sense_input(PIN_9006_IO1, NRF_GPIO_PIN_PULLDOWN, sense);
}
