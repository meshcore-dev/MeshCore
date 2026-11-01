#pragma once

class ExternalWatchdogManager {
protected:
  unsigned long last_feed_watchdog;
public:
  ExternalWatchdogManager() { last_feed_watchdog = 0; }
  virtual bool begin() { return false; }
  virtual void loop() { }
  virtual unsigned long getIntervalMs() const { return 0; }
  virtual void feed() { }
};
