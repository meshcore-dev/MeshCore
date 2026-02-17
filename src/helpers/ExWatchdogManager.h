#pragma once

class ExWatchdogManager {
protected:
  unsigned long next_feed_watchdog;
public:
  ExWatchdogManager() { next_feed_watchdog = 0; }
  virtual bool begin() { return false; }
  virtual void loop() { }
  virtual unsigned long getIntervalMs() const { return 0; }
  virtual void feed() { }
};
