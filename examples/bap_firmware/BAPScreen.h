#pragma once

#include <Arduino.h>
#include "MyMesh.h"

// Forward declaration
class DisplayDriver;

// Screen layout constants for 128x296 portrait e-ink display
#define BAP_SCREEN_WIDTH    128
#define BAP_SCREEN_HEIGHT   296

#define BAP_HEADER_HEIGHT   0
#define BAP_BUS_ROW_HEIGHT  70
#define BAP_FOOTER_HEIGHT   16
#define BAP_MAX_ROWS        4

#define BAP_STALE_THRESHOLD 300  // 5 minutes in seconds

class BAPScreen {
public:
  BAPScreen(DisplayDriver* display);

  // Initialize display
  bool begin();

  // Update the display with new arrival data
  void update(uint32_t stop_id, const BusArrival* arrivals, int count, uint32_t generated_at, int mesh_nodes = 0);

  // Show a status message (e.g., "Connecting...", "No Data")
  void showMessage(const char* line1, const char* line2 = nullptr);

  // Show error state
  void showError(const char* message);

  // Clear the display
  void clear();

  // Get stop name for display (looked up from stop_id)
  void setStopName(const char* name);

private:
  DisplayDriver* _display;
  char _stop_name[32];
  uint32_t _last_update;

  // Format minutes for display
  void formatMinutes(int16_t minutes, char* buf, size_t len);

  // Format timestamp for display
  void formatTime(uint32_t timestamp, char* buf, size_t len);

  // Get status indicator character
  const char* getStatusIndicator(uint8_t status);
};
