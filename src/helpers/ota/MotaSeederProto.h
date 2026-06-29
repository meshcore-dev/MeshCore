#pragma once

#include <stdint.h>

// Wire contract for the "mota-seeder" link: a device (CLIENT) pulls `.mota` bytes on demand from a host
// daemon (SERVER) that owns a folder of `.mota` files. This is the FIRST concrete MotaSource transport
// (docs/ota_protocol.md §9) — the device speaks it over a dedicated Stream (a spare UART / USB-UART), so
// it never contends with the line-based text CLI on the main console.
//
// The device always initiates; every request gets exactly one response. Framing is resync-safe: the
// reader scans for the 2-byte magic, so line noise / a half-read frame just times out and is retried
// (OTA is lowest priority — eventually-upgradable). All multi-byte fields are little-endian.
//
//   request  (device -> host):  'M' 'S'  op(1)  args...                     xsum(1 = XOR of op+args)
//   response (host -> device):  'm' 's'  op(1)  status(1)  payload...       xsum(1 = XOR of all prior)
//
//   OP_COUNT     0x01  args: -            resp payload: count(1)
//   OP_DESCRIBE  0x02  args: idx(1)       resp payload: MotaDesc wire (38 B, see below) [status OK]
//   OP_READ      0x03  args: idx(1) off(4) len(2)   resp payload: len bytes [status OK]
//
//   MotaDesc wire (38 B): mid[4] target_id(4) fw_version(4) codec(1) flags(1) total_size(4)
//                         leaves_off(4) block_count(4) payload_off(4) payload_size(4)
//
// status: 0 = OK, non-zero = error (idx out of range, read past EOF, ...). On error the response carries
// no payload (just magic+op+status+xsum).

namespace mesh {
namespace ota {

static const uint8_t  MOTA_SEEDER_REQ_MAGIC0 = 'M';
static const uint8_t  MOTA_SEEDER_REQ_MAGIC1 = 'S';
static const uint8_t  MOTA_SEEDER_RSP_MAGIC0 = 'm';
static const uint8_t  MOTA_SEEDER_RSP_MAGIC1 = 's';

static const uint8_t  MS_OP_COUNT    = 0x01;
static const uint8_t  MS_OP_DESCRIBE = 0x02;
static const uint8_t  MS_OP_READ     = 0x03;

static const uint8_t  MS_STATUS_OK   = 0x00;

static const uint16_t MOTA_DESC_WIRE = 38;   // bytes of a MotaDesc on the wire (see layout above)

} // namespace ota
} // namespace mesh
