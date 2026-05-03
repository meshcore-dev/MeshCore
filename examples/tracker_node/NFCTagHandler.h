#pragma once

#include <stdint.h>
#include <stddef.h>

// NFCTagHandler is nRF52-only — uses nrfx_nfct peripheral
#ifdef NRF52_PLATFORM

// nrfx_nfct is available via the Adafruit nRF52 BSP which bundles the nRF5 SDK
#include <nrfx_nfct.h>

class NFCTagHandler {
public:
    void begin();   // initialise NFCT peripheral, arm for writes
    void stop();    // disable NFCT

    bool hasNewData() const { return _new_data; }
    const uint8_t* getData() const { return _ndef_buf; }
    size_t getDataLen() const { return _data_len; }
    void clearNewData() { _new_data = false; _data_len = 0; }

private:
    static uint8_t          _ndef_buf[256];
    static volatile bool    _new_data;
    static volatile size_t  _data_len;

    static void nfct_event_handler(nrfx_nfct_evt_t const* event);

    // Parse NDEF message — extract first record payload.
    // Returns pointer into _ndef_buf (not a copy) and sets *out_len.
    // Returns NULL on parse error.
    static const uint8_t* parseNDEFPayload(const uint8_t* ndef, size_t ndef_len, size_t* out_len);
};

#endif  // NRF52_PLATFORM
