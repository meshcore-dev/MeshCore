#include "PowerBank.h"

namespace {
constexpr uint32_t hltPollMs = 100;
constexpr uint32_t hltStatusMs = 500;
constexpr uint32_t hltStaleMs = 3000;
constexpr uint32_t io1WindowMs = 120;
constexpr uint32_t gaugeNormalMs = 3000;
constexpr uint32_t gaugeFastMs = 500;

// Only the ISR writes this counter. The main loop consumes a single aligned read.
volatile uint32_t io1Pulses = 0;
void onIo1Rise() {
  ++io1Pulses;
}

bool stateChanged(const HLT331Serial::Status& before, const HLT331Serial::Status& after) {
  return !before.valid || before.systemWorking != after.systemWorking ||
         (after.systemWorking &&
          (before.charging != after.charging || before.c1State != after.c1State ||
           before.c2State != after.c2State || before.wirelessWork != after.wirelessWork ||
           before.wirelessFull != after.wirelessFull));
}
}

bool PowerBankSnapshot::hltLive() const {
  return hlt.valid && hlt.systemWorking && uint32_t(millis() - hlt.lastFrameMs) <= hltStaleMs;
}

bool PowerBankSnapshot::hasChargingInput() const {
  return hltLive() && (hlt.charging || hlt.c1State == HLT331Serial::PORT_CHARGING ||
                       hlt.c2State == HLT331Serial::PORT_CHARGING);
}

void PowerBank::begin() {
  gaugeStarted = gauge.begin(Wire);
  if (gaugeStarted) readGauge();
  hltStarted = hlt.begin(Serial2, PIN_SERIAL2_RX, PIN_SERIAL2_TX);
  if (hltStarted) {
    hlt.configurePins(PIN_CW_INT, PIN_9006_IO1, PIN_9006_IO2, -1);
    attachInterrupt(digitalPinToInterrupt(PIN_9006_IO1), onIo1Rise, RISING);
    readHlt();
  }
  // Capture the boot sample as a baseline before the UI starts.
  pageEvent = false;
}

void PowerBank::updateFault(bool& sourceFault, bool active) {
  const bool wasFault = cwFault || hltFault;
  sourceFault = active;
  if (wasFault != (cwFault || hltFault)) {
    pageEvent = true;
    fastGaugeReads = 3;
  }
}

void PowerBank::readGauge() {
  status.cw.ready = gauge.readStatus(status.cw);
  lastGauge = millis();
  if (fastGaugeReads) --fastGaugeReads;
  if (status.cw.ready) updateFault(cwFault, !status.cw.profile.matches);
}

void PowerBank::readHlt() {
  const bool valid = hlt.readStatus(status.hlt);
  lastHlt = millis();
  if (!valid || status.hlt.ageMs > hltStaleMs) return;

  const auto& sample = status.hlt;
  if (sample.systemWorking) updateFault(hltFault, sample.ntcOverTemp || sample.wirelessFod);
  if (stateChanged(previous, sample)) {
    pageEvent = true;
    fastGaugeReads = 3;
  }
  previous = sample;
}

void PowerBank::poll() {
  const uint32_t now = millis();
  if (hltStarted && now - lastPoll >= hltPollMs) {
    lastPoll = now;
    hlt.poll();
  }
  const uint32_t pulses = io1Pulses;
  if (pulses != seenPulses) {
    seenPulses = pulses;
    drainStarted = now;
    draining = true;
    fastGaugeReads = 3;
  }
  if (draining && now - drainStarted >= io1WindowMs) {
    // Collect any bytes received since the regular UART poll.
    hlt.poll();
    readHlt();
    pageEvent = true;
    draining = false;
  } else if (hltStarted && !draining && now - lastHlt >= hltStatusMs) {
    readHlt();
  }
  const uint32_t interval = fastGaugeReads ? gaugeFastMs : gaugeNormalMs;
  if (gaugeStarted && now - lastGauge >= interval) readGauge();
}

bool PowerBank::takePageEvent() {
  const bool pending = pageEvent;
  pageEvent = false;
  return pending;
}

void PowerBank::end() {
  if (hltStarted) {
    detachInterrupt(digitalPinToInterrupt(PIN_9006_IO1));
    Serial2.end();
  }
}
