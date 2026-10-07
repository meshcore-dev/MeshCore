#include "TxGate.h"

static bool g_tx_allowed = false;

bool asr650xTxAllowed() {
  return g_tx_allowed;
}
void asr650xSetTxAllowed(bool allowed) {
  g_tx_allowed = allowed;
}
