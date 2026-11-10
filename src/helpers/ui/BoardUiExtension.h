#pragma once

#include <stdint.h>

class DisplayDriver;

namespace mesh {

class BoardUiExtension {
public:
  enum Event : uint8_t {
    EVENT_NONE = 0,
    EVENT_SHOW_PAGE = 1 << 0,
    EVENT_PAGE_ACTION = 1 << 1,
    EVENT_REQUEST_FOCUS = 1 << 2,
  };

  // Consume and return all board-owned input and focus events for this loop.
  virtual uint8_t pollEvents() = 0;
  virtual void handlePageAction() {}

  // The extension owns startFrame() and any coordinate-mode changes. The UI
  // host calls endFrame() after render() returns.
  virtual uint32_t render(DisplayDriver& display, bool fullRefresh) = 0;
};

}
