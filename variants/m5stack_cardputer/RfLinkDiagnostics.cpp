#include <Arduino.h>
#include "target.h"

#ifdef CARDPUTER_RF_DIAG_OVERLAY

namespace {
struct RfDiagState {
  uint32_t lastRecvCount = 0;
  uint32_t lastSentCount = 0;
  uint32_t lastRxMillis = 0;
  uint32_t bestScoreMillis = 0;
  uint32_t windowStartMillis = 0;
  uint32_t windowRecvStart = 0;
  uint8_t bestScore = 0;
  float lastRssi = 0.0f;
  float lastSnr = 0.0f;
  float avgRssi = 0.0f;
  float avgSnr = 0.0f;
  uint16_t avgSamples = 0;
};

RfDiagState rf_diag;

static bool hasRxSample() {
  return rf_diag.lastRxMillis != 0;
}

static uint8_t clampScore(int value) {
  if (value < 0) return 0;
  if (value > 100) return 100;
  return (uint8_t)value;
}

static int scoreFromSnr(float snr) {
  if (snr <= -20.0f) return 0;
  if (snr >= 5.0f) return 35;
  return (int)((snr + 20.0f) * 35.0f / 25.0f);
}

static int scoreFromRssi(float rssi) {
  if (rssi <= -130.0f) return 0;
  if (rssi >= -95.0f) return 25;
  return (int)((rssi + 130.0f) * 25.0f / 35.0f);
}

static int scoreFromFreshness(uint32_t age_ms) {
  if (!hasRxSample()) return 0;
  if (age_ms <= 5000UL) return 25;
  if (age_ms >= 120000UL) return 0;
  return (int)(25 - ((age_ms - 5000UL) * 25UL / 115000UL));
}

static int scoreFromBattery(uint16_t mv) {
  if (mv == 0) return 0;
  if (mv >= 3900) return 15;
  if (mv <= 3400) return 0;
  return (int)((mv - 3400) * 15 / 500);
}

static const char* hintFor(uint8_t score, uint16_t batt_mv, uint32_t age_ms) {
  if (batt_mv > 0 && batt_mv < 3550) return "BAT";
  if (!hasRxSample() || age_ms > 120000UL) return "MOVE";
  if (score >= 70 && age_ms <= 15000UL) return "SEND";
  if (score >= 45) return "HOLD";
  return "MOVE";
}

static void formatAge(char* dest, size_t len, uint32_t age_ms) {
  if (!hasRxSample()) {
    snprintf(dest, len, "--");
    return;
  }

  uint32_t secs = age_ms / 1000UL;
  if (secs < 100UL) {
    snprintf(dest, len, "%02lus", (unsigned long)secs);
  } else if (secs < 3600UL) {
    snprintf(dest, len, "%02lum", (unsigned long)(secs / 60UL));
  } else {
    snprintf(dest, len, "%02luh", (unsigned long)(secs / 3600UL));
  }
}

static uint16_t rxPerMinute(uint32_t now, uint32_t recvCount) {
  if (rf_diag.windowStartMillis == 0 || (uint32_t)(now - rf_diag.windowStartMillis) > 60000UL) {
    rf_diag.windowStartMillis = now;
    rf_diag.windowRecvStart = recvCount;
    return 0;
  }

  uint32_t elapsed = now - rf_diag.windowStartMillis;
  if (elapsed < 1000UL) return 0;
  uint32_t delta = recvCount - rf_diag.windowRecvStart;
  return (uint16_t)min<uint32_t>((delta * 60000UL) / elapsed, 999UL);
}
}

void cardputerDrawRfOverlay(M5CardputerDisplay& disp) {
  uint32_t now = millis();
  uint32_t recvCount = radio_driver.getPacketsRecv();
  uint32_t sentCount = radio_driver.getPacketsSent();

  if (recvCount != rf_diag.lastRecvCount) {
    rf_diag.lastRecvCount = recvCount;
    rf_diag.lastRxMillis = now;
    rf_diag.lastRssi = radio_driver.getLastRSSI();
    rf_diag.lastSnr = radio_driver.getLastSNR();

    if (rf_diag.avgSamples == 0) {
      rf_diag.avgRssi = rf_diag.lastRssi;
      rf_diag.avgSnr = rf_diag.lastSnr;
      rf_diag.avgSamples = 1;
    } else {
      // Exponential moving average; cheap and stable on small MCU.
      rf_diag.avgRssi = (rf_diag.avgRssi * 0.80f) + (rf_diag.lastRssi * 0.20f);
      rf_diag.avgSnr = (rf_diag.avgSnr * 0.80f) + (rf_diag.lastSnr * 0.20f);
      if (rf_diag.avgSamples < 1000) rf_diag.avgSamples++;
    }
  }
  rf_diag.lastSentCount = sentCount;

  uint16_t batt_mv = board.getBattMilliVolts();
  uint32_t age_ms = hasRxSample() ? now - rf_diag.lastRxMillis : 0xFFFFFFFFUL;

  int score = 0;
  if (hasRxSample()) {
    score += scoreFromSnr(rf_diag.lastSnr);
    score += scoreFromRssi(rf_diag.lastRssi);
    score += scoreFromFreshness(age_ms);
  }
  score += scoreFromBattery(batt_mv);
  uint8_t linkScore = clampScore(score);

  if (linkScore > rf_diag.bestScore || (now - rf_diag.bestScoreMillis) > 60000UL) {
    rf_diag.bestScore = linkScore;
    rf_diag.bestScoreMillis = now;
  }

  char age[8];
  formatAge(age, sizeof(age), age_ms);
  uint16_t rpm = rxPerMinute(now, recvCount);

  char line1[42];
  char line2[42];
  const char* hint = hintFor(linkScore, batt_mv, age_ms);

  snprintf(line1, sizeof(line1), "RF %03u %-4s RX%03u/m BAT %u.%02uV",
           (unsigned)linkScore,
           hint,
           (unsigned)rpm,
           (unsigned)(batt_mv / 1000),
           (unsigned)((batt_mv % 1000) / 10));

  if (hasRxSample()) {
    snprintf(line2, sizeof(line2), "S%+05.1f R%4d A%+04.1f/%4d %s",
             rf_diag.lastSnr,
             (int)rf_diag.lastRssi,
             rf_diag.avgSnr,
             (int)rf_diag.avgRssi,
             age);
  } else {
    snprintf(line2, sizeof(line2), "S --.- R ---- A --.-/---- RX --");
  }

  const int overlayY = disp.height() - 22;
  disp.setTextSize(1);
  disp.setColor(DisplayDriver::DARK);
  disp.fillRect(0, overlayY - 1, disp.width(), 23);
  disp.setColor(linkScore >= 70 ? DisplayDriver::GREEN : (linkScore >= 45 ? DisplayDriver::YELLOW : DisplayDriver::RED));
  disp.setCursor(0, overlayY);
  disp.print(line1);
  disp.setCursor(0, overlayY + 11);
  disp.print(line2);
}

#endif
