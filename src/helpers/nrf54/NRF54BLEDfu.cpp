#include "NRF54BLEDfu.h"
#include <nrf54l15_hal.h>

#define DFU_REV_APPMODE       0x0001
#define DFU_OP_START          0x01
#define DFU_MAGIC_OTA_APPJUM  0xB1      // the bootloader's "started from the app" BLE DFU magic
#define DFU_MIN_BOOTLOADER    0x000500  // 0.5.0: the hand-off below matches this version

static const uint8_t UUID128_SVC_DFU_OTA[16] = {
  0x23, 0xD1, 0xBC, 0xEA, 0x5F, 0x78, 0x23, 0x15, 0xDE, 0xEF, 0x12, 0x12, 0x30, 0x15, 0x00, 0x00
};
static const uint8_t UUID128_CHR_DFU_CONTROL[16] = {
  0x23, 0xD1, 0xBC, 0xEA, 0x5F, 0x78, 0x23, 0x15, 0xDE, 0xEF, 0x12, 0x12, 0x31, 0x15, 0x00, 0x00
};
static const uint8_t UUID128_CHR_DFU_PACKET[16] = {
  0x23, 0xD1, 0xBC, 0xEA, 0x5F, 0x78, 0x23, 0x15, 0xDE, 0xEF, 0x12, 0x12, 0x32, 0x15, 0x00, 0x00
};
static const uint8_t UUID128_CHR_DFU_REVISION[16] = {
  0x23, 0xD1, 0xBC, 0xEA, 0x5F, 0x78, 0x23, 0x15, 0xDE, 0xEF, 0x12, 0x12, 0x34, 0x15, 0x00, 0x00
};

// The bootloader's dfu_ble_peer_data_t, as the s145 headers lay it out, and the CRC after it.
// It lives in RAM the app doesn't use (see arch/nrf54l/ldscripts), and survives the reset.
struct PeerData {
  uint8_t addr_type;       // ble_gap_addr_t: addr_id_peer (bit 0), addr_type (bits 1-7)
  uint8_t addr[6];
  uint8_t irk[16];
  uint8_t pad0;
  uint8_t ltk[16];         // ble_gap_enc_key_t.enc_info
  uint8_t ltk_flags;       // lesc (bit 0), auth (bit 1), ltk_len (bits 2-7)
  uint8_t pad1;
  uint16_t ediv;           // ble_gap_enc_key_t.master_id
  uint8_t rand[8];
  uint8_t sys_attr[8];     // Service Changed CCCD, in the SoftDevice's system attribute format
};
static_assert(sizeof(PeerData) == 60 && offsetof(PeerData, ediv) == 42 &&
              offsetof(PeerData, sys_attr) == 52, "must match the bootloader's dfu_ble_peer_data_t");

#define PEER_DATA      ((PeerData*)0x2003FF80UL)
#define PEER_DATA_CRC  (*(uint16_t*)0x2003FFBCUL)

// Service Changed CCCD handle in the bootloader's GATT table. nRF54_Bootloader 0.5.0 keeps the s145
// defaults: GAP 0x01-0x09 (name, appearance, PPCP, CAR), GATT 0x0A, Service Changed 0x0B-0x0C and
// its CCCD 0x0D. If this were wrong the bootloader would reject the hand-off and reset to the app.
#define BOOTLOADER_SC_CCCD_HANDLE  0x000D

uint32_t NRF54BLEDfu::bootloader_version = 0;

static uint32_t reboot_at = 0;
static bool reboot_pending = false;

// CRC-16-CCITT, as the bootloader's crc16_compute()
static uint16_t crc16(const uint8_t* data, uint32_t len) {
  uint16_t crc = 0xFFFF;
  for (uint32_t i = 0; i < len; i++) {
    crc = (uint8_t)(crc >> 8) | (crc << 8);
    crc ^= data[i];
    crc ^= (uint8_t)(crc & 0xFF) >> 4;
    crc ^= (crc << 8) << 4;
    crc ^= ((crc & 0xFF) << 4) << 1;
  }
  return crc;
}

// Hand the peer's address, and keys if bonded, to the bootloader, so it keeps our address and the
// DFU client can reconnect to it (encrypted, when bonded) after the reboot
static void savePeerData(uint16_t conn_hdl) {
  PeerData* pd = PEER_DATA;
  memset(pd, 0, sizeof(*pd));

  xiao_nrf54l15::BleConnectionInfo info;
  if (Bluefruit.rawRadio().getConnectionInfo(&info)) {
    pd->addr_type = (info.peerAddressRandom ? BLE_GAP_ADDR_TYPE_RANDOM_STATIC
                                            : BLE_GAP_ADDR_TYPE_PUBLIC) << 1;
    memcpy(pd->addr, info.peerAddress, 6);
  }

  bool bonded = false;
  BLEConnection* conn = Bluefruit.Connection(conn_hdl);
  xiao_nrf54l15::BleRadio& radio = Bluefruit.rawRadio();
  xiao_nrf54l15::BleBondRecord bond;
  xiao_nrf54l15::BleBondInfo bond_info;
  uint8_t bond_id = radio.activeBondId();
  if (conn && conn->secured() && bond_id != xiao_nrf54l15::BleRadio::kBleInvalidBondId &&
      radio.getBondRecord(&bond) && radio.getBondInfo(bond_id, &bond_info)) {
    bonded = true;
    if (bond.peerIrkValid) {
      pd->addr_type = (bond.peerIdentityAddressRandom ? BLE_GAP_ADDR_TYPE_RANDOM_STATIC
                                                      : BLE_GAP_ADDR_TYPE_PUBLIC) << 1;
      memcpy(pd->addr, bond.peerIdentityAddress, 6);
      memcpy(pd->irk, bond.peerIrk, 16);
    }
    memcpy(pd->ltk, bond.ltk, 16);
    bool lesc = bond_info.flags & xiao_nrf54l15::kBleBondInfoSecureConnections;
    bool auth = bond_info.flags & xiao_nrf54l15::kBleBondInfoAuthenticated;
    pd->ltk_flags = (lesc ? 0x01 : 0) | (auth ? 0x02 : 0) | ((bond.keySize & 0x3F) << 2);
    pd->ediv = bond.ediv;
    memcpy(pd->rand, bond.rand, 8);
  }

  // one attribute: handle, length 2, CCCD value (indications on for a bonded peer, which caches our
  // GATT table and so needs to be told it changed), then the CRC over the lot
  uint16_t cccd = bonded ? 0x0002 : 0x0000;
  uint8_t* sa = pd->sys_attr;
  sa[0] = BOOTLOADER_SC_CCCD_HANDLE & 0xFF;
  sa[1] = BOOTLOADER_SC_CCCD_HANDLE >> 8;
  sa[2] = 2;
  sa[3] = 0;
  sa[4] = cccd & 0xFF;
  sa[5] = cccd >> 8;
  uint16_t sa_crc = crc16(sa, 6);
  sa[6] = sa_crc & 0xFF;
  sa[7] = sa_crc >> 8;

  PEER_DATA_CRC = crc16((const uint8_t*)pd, sizeof(*pd));
}

void NRF54BLEDfu::onControlWrite(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data,
                                 uint16_t len) {
  if (len == 0 || data[0] != DFU_OP_START || !chr->notifyEnabled(conn_hdl)) return;
  // the hand-off format is only known for our bootloader, 0.5.0 or later
  if (bootloader_version < DFU_MIN_BOOTLOADER || bootloader_version > 0xFFFFFF) return;

  savePeerData(conn_hdl);
  NRF_POWER->GPREGRET[0] = DFU_MAGIC_OTA_APPJUM;

  // let the write response and disconnect go out before rebooting
  Bluefruit.Advertising.restartOnDisconnect(false);
  Bluefruit.disconnect(conn_hdl);
  reboot_at = millis() + 500;
  reboot_pending = true;
}

NRF54BLEDfu::NRF54BLEDfu()
    : BLEService(UUID128_SVC_DFU_OTA), _chr_packet(UUID128_CHR_DFU_PACKET),
      _chr_control(UUID128_CHR_DFU_CONTROL), _chr_revision(UUID128_CHR_DFU_REVISION) { }

err_t NRF54BLEDfu::begin() {
  err_t err = BLEService::begin();
  if (err != ERROR_NONE) return err;

  _chr_packet.setProperties(CHR_PROPS_WRITE_WO_RESP);
  _chr_packet.setMaxLen(20);
  if ((err = _chr_packet.begin()) != ERROR_NONE) return err;

  _chr_control.setProperties(CHR_PROPS_WRITE | CHR_PROPS_NOTIFY);
  _chr_control.setMaxLen(23);
  _chr_control.setWriteCallback(onControlWrite);
  if ((err = _chr_control.begin()) != ERROR_NONE) return err;

  _chr_revision.setProperties(CHR_PROPS_READ);
  _chr_revision.setFixedLen(2);
  if ((err = _chr_revision.begin()) != ERROR_NONE) return err;
  _chr_revision.write16(DFU_REV_APPMODE);

  return ERROR_NONE;
}

void NRF54BLEDfu::loop() {
  if (reboot_pending && (int32_t)(millis() - reboot_at) >= 0) NVIC_SystemReset();
}
