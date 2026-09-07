#include "SerialBLEInterface.h"
#include <BluetoothLock.h>
#include <stdio.h>
#include <string.h>

// Nordic UART 6E4000xx-B5A3-F393-E0A9-E50E24DCCA9E, as raw bytes: the core lib's string
// parser uses sscanf("%llx"), which newlib-nano on the RP2040 doesn't support (yields all zeros)
#define NUS_UUID(n) { 0x6E, 0x40, 0x00, n, 0xB5, 0xA3, 0xF3, 0x93, 0xE0, 0xA9, 0xE5, 0x0E, 0x24, 0xDC, 0xCA, 0x9E }
static const uint8_t SERVICE_UUID[16]           = NUS_UUID(0x01);
static const uint8_t CHARACTERISTIC_UUID_RX[16] = NUS_UUID(0x02);
static const uint8_t CHARACTERISTIC_UUID_TX[16] = NUS_UUID(0x03);

// The BLE core lib's own setValue()/notify path reallocs the value buffer on every call,
// so a second frame queued before the radio drained the first would corrupt it. We keep our
// own frame queue and drive att_server_notify() from the can-send-now callback instead.

SerialBLEInterface::SerialBLEInterface()
  : BLEService(BLEUUID(SERVICE_UUID)),
    _rx(BLEUUID(CHARACTERISTIC_UUID_RX), BLEWrite, nullptr, ATT_SECURITY_AUTHENTICATED, ATT_SECURITY_AUTHENTICATED),
    _tx(BLEUUID(CHARACTERISTIC_UUID_TX), BLERead | BLENotify, nullptr, ATT_SECURITY_AUTHENTICATED, ATT_SECURITY_AUTHENTICATED)
{
  _isEnabled = false;
  _tx_pending = false;
  send_queue_len = 0;
  recv_queue_len = 0;
  memset(&_can_send, 0, sizeof(_can_send));
  _rx.setCallbacks(this);
  addCharacteristic(&_rx);
  addCharacteristic(&_tx);
}

void SerialBLEInterface::begin(const char* prefix, char* name, uint32_t pin_code) {
  // JustWorks here only makes the server request pairing on connect; caps are overridden below
  BLE.setSecurity(BLESecurityJustWorks);
  // adv data carries the 128-bit service UUID, leaving room for only 8 name chars;
  // the full name goes in the scan response
  BLE.begin(prefix);

  if (strcmp(name, "@@MAC") == 0) {
    bd_addr_t a;
    gap_local_bd_addr(a);
    sprintf(name, "%02X%02X%02X%02X%02X%02X", a[0], a[1], a[2], a[3], a[4], a[5]);   // modify (IN-OUT param)
  }
  char dev_name[32+16];
  snprintf(dev_name, sizeof(dev_name), "%s%s", prefix, name);

  BLE.server()->setName(dev_name);   // GAP device name characteristic
  BLE.server()->addService(this);
  BLE.server()->setCallbacks(this);

  // static passkey pairing with MITM protection, matching the esp32/nrf52 interfaces
  sm_set_io_capabilities(IO_CAPABILITY_DISPLAY_ONLY);
  sm_set_authentication_requirements(SM_AUTHREQ_MITM_PROTECTION | SM_AUTHREQ_BONDING);
  sm_use_fixed_passkey_in_display_role(pin_code);

  size_t n = strlen(dev_name);
  if (n > sizeof(_scan_rsp) - 2) n = sizeof(_scan_rsp) - 2;
  _scan_rsp[0] = n + 1;
  _scan_rsp[1] = BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME;
  memcpy(&_scan_rsp[2], dev_name, n);
  gap_scan_response_set_data(n + 2, _scan_rsp);

  BLE_DEBUG_PRINTLN("begin: name=%s", dev_name);
}

void SerialBLEInterface::clearBuffers() {
  send_queue_len = 0;
  recv_queue_len = 0;
  _tx_pending = false;   // a stale registration just fires into an empty queue; BTstack ignores double-adds
}

void SerialBLEInterface::onConnect(BLEServer* s) {
  BLE_DEBUG_PRINTLN("connected handle=0x%04X", _tx.conHandle());
  clearBuffers();
}

void SerialBLEInterface::onDisconnect(BLEServer* s) {
  BLE_DEBUG_PRINTLN("disconnected");
  clearBuffers();   // BTstack re-enables advertising on its own
}

void SerialBLEInterface::onWrite(BLECharacteristic* c) {
  if (c != &_rx) return;
  size_t len = _rx.valueLen();
  if (len == 0 || len > MAX_FRAME_SIZE) {
    BLE_DEBUG_PRINTLN("onWrite: bad frame len=%u", (unsigned)len);
    return;
  }
  if (recv_queue_len >= FRAME_QUEUE_SIZE) {
    BLE_DEBUG_PRINTLN("onWrite: recv queue full, dropping frame");
    return;
  }
  recv_queue[recv_queue_len].len = len;
  memcpy(recv_queue[recv_queue_len].buf, _rx.valueData(), len);
  recv_queue_len++;
}

// caller holds the BT lock (or is in the BT context)
void SerialBLEInterface::kickSend() {
  if (_tx_pending) return;
  _tx_pending = true;
  _can_send.callback = onCanSend;
  _can_send.context = this;
  if (att_server_register_can_send_now_callback(&_can_send, _tx.conHandle()) != 0) {
    _tx_pending = false;
  }
}

// BT context: one notification per can-send-now, re-arm while frames remain
void SerialBLEInterface::sendNext() {
  _tx_pending = false;
  if (send_queue_len == 0) return;
  if (!isConnected()) {
    BLE_DEBUG_PRINTLN("sendNext: not connected, clearing send queue");
    send_queue_len = 0;
    return;
  }
  uint16_t h = _tx.conHandle();
  Frame& f = send_queue[0];
  uint16_t mtu = att_server_get_mtu(h);
  if (f.len + 3 > mtu) {
    // att would silently truncate; drop instead (client must negotiate MTU >= MAX_FRAME_SIZE+3)
    BLE_DEBUG_PRINTLN("sendNext: frame len=%u exceeds mtu=%u, dropping", f.len, mtu);
  } else {
    uint8_t err = att_server_notify(h, _tx.valueHandle(), f.buf, f.len);
    if (err == BTSTACK_ACL_BUFFERS_FULL) {
      kickSend();
      return;
    }
    if (err) {
      BLE_DEBUG_PRINTLN("sendNext: notify failed err=%u, dropping", err);
    } else {
      BLE_DEBUG_PRINTLN("writeBytes: sz=%u, hdr=%u", f.len, f.buf[0]);
    }
  }
  send_queue_len--;
  memmove(&send_queue[0], &send_queue[1], send_queue_len * sizeof(Frame));
  if (send_queue_len > 0) kickSend();
}

void SerialBLEInterface::enable() {
  if (_isEnabled) return;
  _isEnabled = true;
  clearBuffers();
  BLE.startAdvertising(true);
}

void SerialBLEInterface::disconnect() {
  uint16_t h = _tx.conHandle();
  if (h) gap_disconnect(h);
}

void SerialBLEInterface::disable() {
  _isEnabled = false;
  BLE_DEBUG_PRINTLN("disable");
  disconnect();
  BLE.stopAdvertising();
}

bool SerialBLEInterface::isConnected() const {
  // notifications can only be enabled once the link is authenticated (CCCD inherits the perms)
  return _isEnabled && _tx.conHandle() != 0 && _tx.notifyEnabled();
}

bool SerialBLEInterface::isWriteBusy() const {
  return send_queue_len >= (FRAME_QUEUE_SIZE * 2 / 3);
}

size_t SerialBLEInterface::writeFrame(const uint8_t src[], size_t len) {
  if (len == 0 || len > MAX_FRAME_SIZE) {
    BLE_DEBUG_PRINTLN("writeFrame(), frame too big, len=%u", (unsigned)len);
    return 0;
  }
  if (!isConnected()) return 0;

  BluetoothLock lock;
  if (send_queue_len >= FRAME_QUEUE_SIZE) {
    BLE_DEBUG_PRINTLN("writeFrame(), send_queue is full!");
    return 0;
  }
  send_queue[send_queue_len].len = len;
  memcpy(send_queue[send_queue_len].buf, src, len);
  send_queue_len++;
  kickSend();
  return len;
}

size_t SerialBLEInterface::checkRecvFrame(uint8_t dest[]) {
  BluetoothLock lock;
  if (recv_queue_len == 0) return 0;

  size_t len = recv_queue[0].len;
  memcpy(dest, recv_queue[0].buf, len);
  recv_queue_len--;
  memmove(&recv_queue[0], &recv_queue[1], recv_queue_len * sizeof(Frame));
  BLE_DEBUG_PRINTLN("readBytes: sz=%u, hdr=%u", (unsigned)len, dest[0]);
  return len;
}
