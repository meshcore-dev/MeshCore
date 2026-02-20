#include "BAPScreen.h"
#include <helpers/ui/DisplayDriver.h>

BAPScreen::BAPScreen(DisplayDriver* display)
  : _display(display), _last_update(0) {
  strcpy(_stop_name, "Unknown Stop");
}

bool BAPScreen::begin() {
  if (!_display) return false;
  // Display initialization is handled by the concrete display class (E290Display)
  // before being passed to BAPScreen
  return true;
}

void BAPScreen::update(uint32_t stop_id, const BusArrival* arrivals, int count, uint32_t generated_at, int mesh_nodes) {
  if (!_display) return;

  _display->startFrame();

  // Draw header with stop ID (black text, no fill)
  _display->setColor(DisplayDriver::Color::DARK);
  _display->setTextSize(1);
  _display->setCursor(2, 3);

  char stop_buf[20];
  snprintf(stop_buf, sizeof(stop_buf), "Stop %u", stop_id);
  _display->print(stop_buf);

  // Show live indicator if data is fresh
  uint32_t now = time(nullptr);
  bool is_fresh = (now - generated_at) < BAP_STALE_THRESHOLD;

  if (is_fresh) {
    _display->setCursor(BAP_SCREEN_WIDTH - 8, 3);
    _display->print("*");
  }

  // Draw header separator line
  _display->drawRect(0, BAP_HEADER_HEIGHT - 1, BAP_SCREEN_WIDTH, 1);

  // Draw bus arrival rows
  int rows = (count > BAP_MAX_ROWS) ? BAP_MAX_ROWS : count;

  for (int i = 0; i < rows; i++) {
    const BusArrival& arr = arrivals[i];
    int row_top = BAP_HEADER_HEIGHT + (i * BAP_BUS_ROW_HEIGHT);

    // Draw separator line between rows
    if (i > 0) {
      _display->drawRect(0, row_top - 1, BAP_SCREEN_WIDTH, 1);
    }

    // Build "XX to Destination in" line
    char header_line[40];
    snprintf(header_line, sizeof(header_line), "%s to %.12s in", arr.route, arr.destination);

    // Draw route/destination line (small text, centered)
    _display->setColor(DisplayDriver::Color::DARK);
    _display->setTextSize(1);
    int header_width = _display->getTextWidth(header_line);
    _display->setCursor((BAP_SCREEN_WIDTH - header_width) / 2, row_top + 2);
    _display->print(header_line);

    // Format minutes for display
    char min_buf[16];
    formatMinutes(arr.minutes, min_buf, sizeof(min_buf));

    // Draw big centered minutes
    _display->setTextSize(3);  // Large text

    int text_width = _display->getTextWidth(min_buf);
    int x = (BAP_SCREEN_WIDTH - text_width) / 2;
    int y = row_top + 20;

    _display->setCursor(x, y);
    _display->print(min_buf);
  }

  // If fewer arrivals than rows, show placeholders
  for (int i = rows; i < BAP_MAX_ROWS; i++) {
    int row_top = BAP_HEADER_HEIGHT + (i * BAP_BUS_ROW_HEIGHT);
    int row_center = row_top + (BAP_BUS_ROW_HEIGHT / 2);

    // Draw separator line
    _display->drawRect(0, row_top - 1, BAP_SCREEN_WIDTH, 1);

    _display->setTextSize(2);
    _display->setColor(DisplayDriver::Color::DARK);
    const char* placeholder = "---";
    int text_width = _display->getTextWidth(placeholder);
    _display->setCursor((BAP_SCREEN_WIDTH - text_width) / 2, row_center - 8);
    _display->print(placeholder);
  }

  // Draw footer
  int footer_y = BAP_SCREEN_HEIGHT - BAP_FOOTER_HEIGHT;
  _display->drawRect(0, footer_y - 1, BAP_SCREEN_WIDTH, 1);

  _display->setTextSize(1);
  _display->setCursor(4, footer_y + 1);

  // Show last update time
  char time_buf[16];
  formatTime(generated_at, time_buf, sizeof(time_buf));
  _display->print(time_buf);

  _display->endFrame();
  _last_update = millis();
}

void BAPScreen::showMessage(const char* line1, const char* line2) {
  if (!_display) return;

  _display->startFrame();
  _display->clear();

  _display->setColor(DisplayDriver::Color::DARK);
  _display->setTextSize(2);

  if (line1) {
    _display->drawTextCentered(BAP_SCREEN_WIDTH / 2, BAP_SCREEN_HEIGHT / 2 - 20, line1);
  }

  if (line2) {
    _display->setTextSize(1);
    _display->drawTextCentered(BAP_SCREEN_WIDTH / 2, BAP_SCREEN_HEIGHT / 2 + 10, line2);
  }

  _display->endFrame();
}

void BAPScreen::showError(const char* message) {
  if (!_display) return;

  _display->startFrame();

  // Draw error box centered in portrait mode
  int box_x = 5;
  int box_y = 100;
  int box_w = BAP_SCREEN_WIDTH - 10;
  int box_h = 80;

  _display->fillRect(box_x, box_y, box_w, box_h);
  _display->setColor(DisplayDriver::Color::LIGHT);

  _display->setTextSize(1);
  _display->drawTextCentered(BAP_SCREEN_WIDTH / 2, box_y + 15, "ERROR");

  _display->setTextSize(1);
  _display->setCursor(box_x + 5, box_y + 40);

  // Word wrap the error message
  _display->printWordWrap(message, box_w - 10);

  _display->endFrame();
}

void BAPScreen::clear() {
  if (!_display) return;
  _display->clear();
}

void BAPScreen::setStopName(const char* name) {
  strncpy(_stop_name, name, sizeof(_stop_name) - 1);
  _stop_name[sizeof(_stop_name) - 1] = '\0';
}

void BAPScreen::formatMinutes(int16_t minutes, char* buf, size_t len) {
  if (minutes == ARRIVAL_MINUTES_NA) {
    strncpy(buf, "N/A", len);
  } else if (minutes == ARRIVAL_MINUTES_DELAYED) {
    strncpy(buf, "DELAY", len);
  } else if (minutes <= 0) {
    strncpy(buf, "NOW", len);
  } else if (minutes < 60) {
    snprintf(buf, len, "%dm", minutes);
  } else {
    int hours = minutes / 60;
    int mins = minutes % 60;
    snprintf(buf, len, "%dh%dm", hours, mins);
  }
}

void BAPScreen::formatTime(uint32_t timestamp, char* buf, size_t len) {
  time_t t = (time_t)timestamp;
  struct tm* tm_info = localtime(&t);

  if (tm_info) {
    // Format as "10:42 AM"
    int hour = tm_info->tm_hour;
    const char* ampm = "AM";

    if (hour >= 12) {
      ampm = "PM";
      if (hour > 12) hour -= 12;
    }
    if (hour == 0) hour = 12;

    snprintf(buf, len, "%d:%02d %s", hour, tm_info->tm_min, ampm);
  } else {
    strncpy(buf, "--:--", len);
  }
}

const char* BAPScreen::getStatusIndicator(uint8_t status) {
  switch (status) {
    case ARRIVAL_STATUS_DELAYED:
      return "!";  // Exclamation mark for delayed
    case ARRIVAL_STATUS_CANCELLED:
      return "X";  // X for cancelled
    default:
      return "";
  }
}
