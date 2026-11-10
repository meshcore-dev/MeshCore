#include "NFCTagHandler.h"

#ifdef NRF52_PLATFORM

// Static member definitions
uint8_t         NFCTagHandler::_ndef_buf[256];
volatile bool   NFCTagHandler::_new_data   = false;
volatile size_t NFCTagHandler::_data_len   = 0;

/* ------------------------------------------------------------------ */
/* NDEF record parser                                                   */
/*                                                                      */
/* NDEF record layout (short record, MB=1 ME=1 CF=0 SR=1 IL=0):       */
/*   [0]  flags byte: TNF in bits[2:0]                                 */
/*   [1]  type_length                                                   */
/*   [2]  payload_length  (SR=1: 1 byte; SR=0: 4 bytes)               */
/*   [type_length bytes]  type                                          */
/*   [payload_length bytes] payload                                     */
/* ------------------------------------------------------------------ */

#define NDEF_SR_BIT  0x10   // Short Record flag in flags byte
#define NDEF_IL_BIT  0x08   // ID Length present

/*static*/
const uint8_t* NFCTagHandler::parseNDEFPayload(const uint8_t* ndef, size_t ndef_len, size_t* out_len) {
    if (ndef_len < 3) return NULL;

    uint8_t flags        = ndef[0];
    uint8_t type_length  = ndef[1];
    bool    short_record = (flags & NDEF_SR_BIT) != 0;
    bool    id_present   = (flags & NDEF_IL_BIT) != 0;

    size_t  hdr_offset = 2;  // past flags + type_length

    // payload length: 1 byte (SR) or 4 bytes
    uint32_t payload_length;
    if (short_record) {
        if (hdr_offset >= ndef_len) return NULL;
        payload_length = ndef[hdr_offset++];
    } else {
        if (hdr_offset + 4 > ndef_len) return NULL;
        payload_length = ((uint32_t)ndef[hdr_offset]     << 24) |
                         ((uint32_t)ndef[hdr_offset + 1] << 16) |
                         ((uint32_t)ndef[hdr_offset + 2] <<  8) |
                          (uint32_t)ndef[hdr_offset + 3];
        hdr_offset += 4;
    }

    // optional ID length byte
    uint8_t id_length = 0;
    if (id_present) {
        if (hdr_offset >= ndef_len) return NULL;
        id_length = ndef[hdr_offset++];
    }

    // skip type + id
    hdr_offset += type_length + id_length;

    if (hdr_offset + payload_length > ndef_len) return NULL;

    *out_len = (size_t)payload_length;
    return &ndef[hdr_offset];
}

/* ------------------------------------------------------------------ */
/* nrfx_nfct event handler (called from ISR / SoftDevice context)      */
/* ------------------------------------------------------------------ */

/*static*/
void NFCTagHandler::nfct_event_handler(nrfx_nfct_evt_t const* event) {
    switch (event->evt_id) {

        case NRFX_NFCT_EVT_RX_FRAMEEND: {
            // A complete frame was received — interpret as NDEF write
            nrfx_nfct_data_desc_t const* rx = &event->params.rx_frameend.rx_data;
            if (rx->p_data == NULL || rx->data_length == 0) break;

            size_t copy_len = rx->data_length;
            if (copy_len > sizeof(_ndef_buf)) copy_len = sizeof(_ndef_buf);
            memcpy(_ndef_buf, rx->p_data, copy_len);
            _data_len = copy_len;
            _new_data = true;
            break;
        }

        case NRFX_NFCT_EVT_FIELD_DETECTED:
            // NFC field present — re-arm for RX if not already active
            nrfx_nfct_state_force(NRFX_NFCT_STATE_ACTIVATED);
            break;

        case NRFX_NFCT_EVT_FIELD_LOST:
            // Field gone — re-arm for next approach
            nrfx_nfct_state_force(NRFX_NFCT_STATE_SENSE);
            break;

        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void NFCTagHandler::begin() {
    _new_data = false;
    _data_len = 0;
    memset(_ndef_buf, 0, sizeof(_ndef_buf));

    nrfx_nfct_config_t cfg = {
        .rxtx_int_mask  = NRFX_NFCT_EVT_RX_FRAMEEND |
                          NRFX_NFCT_EVT_FIELD_DETECTED |
                          NRFX_NFCT_EVT_FIELD_LOST,
        .cb             = nfct_event_handler,
    };

    nrfx_nfct_init(&cfg);
    nrfx_nfct_enable();
}

void NFCTagHandler::stop() {
    nrfx_nfct_disable();
    _new_data = false;
    _data_len = 0;
}

#endif  // NRF52_PLATFORM
