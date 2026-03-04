#pragma once

class ExternalWatchdogManager {
protected:
  unsigned long next_feed_watchdog;
public:
  ExternalWatchdogManager() { next_feed_watchdog = 0; }
  virtual bool begin() { return false; }
  virtual void loop() { }
  virtual unsigned long getIntervalMs() const { return 0; }
  virtual void feed() { }
};
