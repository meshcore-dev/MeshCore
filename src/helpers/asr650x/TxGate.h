#pragma once

// The companion firmware for this board does not generate its own identity: the app imports a private key.
// Until then the node runs with a placeholder identity derived from the chip ID, which must never transmit.
bool asr650x_tx_allowed();
void asr650x_set_tx_allowed(bool allowed);
