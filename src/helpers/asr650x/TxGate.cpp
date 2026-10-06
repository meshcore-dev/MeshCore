#include "TxGate.h"

static bool g_tx_allowed = false;

bool asr650x_tx_allowed() { return g_tx_allowed; }
void asr650x_set_tx_allowed(bool allowed) { g_tx_allowed = allowed; }
