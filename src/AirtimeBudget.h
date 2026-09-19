#pragma once

#include <stdint.h>
#include <MeshCore.h>

namespace mesh {

/**
 * \brief  Rolling-window airtime ledger.
 *
 * Accounts for airtime (milliseconds) spent transmitting, and reports how much
 * of the trailing window has been used, so a caller can bound how much of the
 * channel it occupies over a moving window (eg. a 1% duty-cycle band).
 *
 * Airtime is accumulated into fixed-size slots.  A slot is only forgotten once
 * it lies entirely outside the window, so the reported usage is never lower
 * than the true trailing-window total: the ledger errs on the side of caution.
 * More slots give a finer (less conservative) figure at the cost of RAM.
 */
class AirtimeBudget {
public:
  static const uint8_t MAX_SLOTS = 12;

private:
  uint32_t _window_ms;
  uint32_t _slot_ms;
  uint32_t _limit_ms;
  uint32_t _used_ms;
  uint32_t _slot_start;
  uint32_t _slots[MAX_SLOTS];
  uint8_t  _slot_count;
  uint8_t  _current;

  void clear();

public:
  AirtimeBudget() { clear(); }

  /**
   * \brief  Drop any usage that has fallen outside the window.
   * \param  now  current clock, in milliseconds
   */
  void update(uint32_t now);

  /**
   * \brief  (Re)initialise the ledger, eg. when prefs change.
   * \param  window_ms   length of the rolling window
   * \param  limit_ms    maximum airtime allowed per window (0 = no limit)
   * \param  now         current clock, in milliseconds
   * \param  slot_count  slots to divide the window into (1..MAX_SLOTS)
   */
  void begin(uint32_t window_ms, uint32_t limit_ms, uint32_t now, uint8_t slot_count);

  /**
   * \returns  true if airtime_ms of transmit airtime still fits in the budget.
   */
  bool canSpend(uint32_t airtime_ms, uint32_t now);

  /**
   * \brief  Record airtime that was actually spent transmitting.
   */
  void record(uint32_t airtime_ms, uint32_t now);

  void setLimit(uint32_t limit_ms) { _limit_ms = limit_ms; }

  uint32_t getWindow() const { return _window_ms; }
  uint32_t getLimit() const { return _limit_ms; }
  uint32_t getUsed() const { return _used_ms; }
  uint32_t getRemaining() const { return (!isEnabled() || _used_ms >= _limit_ms) ? 0 : _limit_ms - _used_ms; }

  /// A budget only binds when it is set below a full window of airtime.
  bool isEnabled() const { return _slot_count > 0 && _limit_ms > 0 && _limit_ms < _window_ms; }
};

}
