#pragma once

// The companion firmware for this board does not generate its own identity: the app imports a private key.
// Until then the node runs with a placeholder identity derived from the chip ID, which must never transmit.
bool asr650xTxAllowed();
void asr650xSetTxAllowed(bool allowed);
