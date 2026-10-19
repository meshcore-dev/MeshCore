#include "SerialBLEInterface.h"
#include "esp_mac.h"
#include <nvs.h>

extern "C" void ble_gatts_set_clt_cfg_perm_flags(uint8_t flags);   // NimBLE host, missing from its public headers

#define ADVERT_CHECK_INTERVAL    1000   // millis
#define BLE_RETRY_THROTTLE_MS    10     // back off when the stack is out of TX buffers

// bonds made by the old Bluedroid stack are useless to NimBLE, free their NVS space
static void eraseBluedroidBonds() {
  nvs_handle_t h;
  size_t used = 0;
  if (nvs_open("bt_config.conf", NVS_READONLY, &h) != ESP_OK) return;
  nvs_get_used_entry_count(h, &used);
  nvs_close(h);
  if (used > 0 && nvs_open("bt_config.conf", NVS_READWRITE, &h) == ESP_OK) {
    nvs_erase_all(h);
    nvs_commit(h);
    nvs_close(h);
  }
}

void SerialBLEInterface::begin(const char* prefix, char* name, uint32_t pin_code) {
  uint8_t addr[8];
  memset(addr, 0, sizeof(addr));
  esp_efuse_mac_get_default(addr);
  char dev_name[32+16];
  makeBLEName(dev_name, sizeof(dev_name), prefix, name, addr, true);

  eraseBluedroidBonds();

  NimBLEDevice::init(dev_name);
  NimBLEDevice::setMTU(BLE_MAX_MTU);
  NimBLEDevice::setSecurityAuth(true, true, true);   // bonding, MITM, secure connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setSecurityPasskey(pin_code);
  // phones use private addresses, so the identity key is needed to recognise them on reconnect
  NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);

  // like Bluefruit on nRF52: subscribing needs an authenticated link, so the phone's first request is
  // rejected with Insufficient Authentication and the phone starts pairing (Apple guidelines 11.10)
  ble_gatts_set_clt_cfg_perm_flags(BLE_ATT_F_READ | BLE_ATT_F_WRITE | BLE_ATT_F_WRITE_ENC | BLE_ATT_F_WRITE_AUTHEN);

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(this, false);

  NimBLEService* pService = pServer->createService(BLE_UART_SERVICE_UUID);
  pTxCharacteristic = pService->createCharacteristic(BLE_UART_TX_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN | NIMBLE_PROPERTY::NOTIFY);
  NimBLECharacteristic* pRxCharacteristic = pService->createCharacteristic(BLE_UART_RX_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN);
  pRxCharacteristic->setCallbacks(this);
  pTxCharacteristic->setCallbacks(this);   // for onSubscribe()

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_UART_SERVICE_UUID);
  pAdvertising->addTxPower();
  pAdvertising->setMinInterval(BLE_ADV_INTERVAL_MIN);
  pAdvertising->setMaxInterval(BLE_ADV_INTERVAL_MAX);
  pAdvertising->setPreferredParams(BLE_MIN_CONN_INTERVAL, BLE_MAX_CONN_INTERVAL);
  pAdvertising->enableScanResponse(true);
  pAdvertising->setName(dev_name);
}

// -------- NimBLEServerCallbacks methods

void SerialBLEInterface::onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) {
  BLE_DEBUG_PRINTLN("onConnect(), conn_handle=%d", connInfo.getConnHandle());
  _conn_handle = connInfo.getConnHandle();
  pServer->setDataLen(_conn_handle, BLE_DLE_MAX_TX_OCTETS);   // before the MTU exchange (Apple guidelines 11.11)
  // don't rely on the phone to ask; once settled (even at 23) frames too big for it can be dropped
  auto done = [](uint16_t, const ble_gatt_error*, uint16_t, void* arg) { ((SerialBLEInterface*)arg)->_mtu_done = true; return 0; };
  if (ble_gattc_exchange_mtu(_conn_handle, done, this) != 0) _mtu_done = true;   // eg. the phone already did it
}

void SerialBLEInterface::onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) {
  BLE_DEBUG_PRINTLN("onDisconnect(), reason=0x%02X", reason);
  deviceConnected = false;
  _subscribed = false;
  _mtu_done = false;
  _conn_handle = BLE_HS_CONN_HANDLE_NONE;
}

void SerialBLEInterface::onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) {
  BLE_DEBUG_PRINTLN("onMTUChange(), mtu=%d", MTU);
}

void SerialBLEInterface::onAuthenticationComplete(NimBLEConnInfo& connInfo) {
  if (connInfo.isEncrypted() && connInfo.isAuthenticated()) {
    BLE_DEBUG_PRINTLN("onAuthenticationComplete() - success");
    _conn_handle = connInfo.getConnHandle();   // bonded peers can encrypt before onConnect() arrives
    deviceConnected = true;
    pServer->updateConnParams(_conn_handle, BLE_MIN_CONN_INTERVAL, BLE_MAX_CONN_INTERVAL, BLE_SLAVE_LATENCY, BLE_CONN_SUP_TIMEOUT);
  } else {
    BLE_DEBUG_PRINTLN("onAuthenticationComplete() - failure");
    pServer->disconnect(connInfo);
  }
}

// -------- NimBLECharacteristicCallbacks methods

void SerialBLEInterface::onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
  NimBLEAttValue value = pCharacteristic->getValue();

  if (value.size() > MAX_FRAME_SIZE) {
    BLE_DEBUG_PRINTLN("ERROR: onWrite(), frame too big, len=%d", value.size());
  } else if (!recv_queue.push(value.data(), value.size())) {
    BLE_DEBUG_PRINTLN("ERROR: onWrite(), recv_queue is full!");
  }
}

void SerialBLEInterface::onSubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo, uint16_t subValue) {
  BLE_DEBUG_PRINTLN("onSubscribe(), subValue=%d", subValue);
  _subscribed = subValue & 1;
}

// ---------- public methods

void SerialBLEInterface::clearBuffers() {
  recv_queue.clear();
  send_queue.clear();
  _last_retry = 0;
}

void SerialBLEInterface::enable() {
  if (_isEnabled) return;

  _isEnabled = true;
  clearBuffers();

  pServer->advertiseOnDisconnect(true);
  NimBLEDevice::startAdvertising();
}

void SerialBLEInterface::disable() {
  _isEnabled = false;

  BLE_DEBUG_PRINTLN("SerialBLEInterface::disable");

  pServer->advertiseOnDisconnect(false);
  NimBLEDevice::stopAdvertising();
  if (_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
    pServer->disconnect(_conn_handle);
  }
  deviceConnected = false;
}

size_t SerialBLEInterface::writeFrame(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) {
    BLE_DEBUG_PRINTLN("writeFrame(), frame too big, len=%d", len);
    return 0;
  }

  if (deviceConnected && len > 0) {
    if (!send_queue.push(src, len)) {
      BLE_DEBUG_PRINTLN("writeFrame(), send_queue is full!");
      return 0;
    }
    return len;
  }
  return 0;
}

bool SerialBLEInterface::isWriteBusy() const {
  return send_queue.used() >= BLE_FRAME_QUEUE_SIZE * 2 / 3;
}

size_t SerialBLEInterface::checkRecvFrame(uint8_t dest[]) {
  if (!deviceConnected) {
    if (oldDeviceConnected) {
      clearBuffers();
      oldDeviceConnected = false;
    }
    // NimBLE drops the disconnect event of a link lost before onConnect() (eg. stale bond), so advertising never restarts.
    // Workaround for NimBLE-Arduino <= 2.5.1 only: fixed on master by h2zero/NimBLE-Arduino#1104 (see #1082),
    // delete this check, ADVERT_CHECK_INTERVAL and _last_adv_check once we're on a release that includes it.
    if (_isEnabled && millis() - _last_adv_check >= ADVERT_CHECK_INTERVAL) {
      _last_adv_check = millis();
      if (pServer->getConnectedCount() == 0 && !NimBLEDevice::getAdvertising()->isAdvertising()) {
        NimBLEDevice::startAdvertising();
      }
    }
    return 0;
  }
  oldDeviceConnected = true;

  unsigned long now = millis();
  if (!send_queue.isEmpty()   // first, check send queue
    && _subscribed            // notify() to a conn handle skips the CCCD check
    && (_last_retry == 0 || now - _last_retry >= BLE_RETRY_THROTTLE_MS)
  ) {
    const SerialBLEFrame& frame = send_queue.peek();
    uint16_t mtu = pServer->getPeerMTU(_conn_handle);

    if (frame.len + 3 > mtu) {
      if (_mtu_done) {   // else: MTU exchange not done yet, wait for it
        BLE_DEBUG_PRINTLN("writeBytes: sz=%d exceeds MTU, dropped", (uint32_t)frame.len);
        send_queue.pop();
      }
    } else if (pTxCharacteristic->notify(frame.buf, frame.len, _conn_handle)) {
      BLE_DEBUG_PRINTLN("writeBytes: sz=%d, hdr=%d", (uint32_t)frame.len, (uint32_t)frame.buf[0]);
      _last_retry = 0;
      send_queue.pop();
    } else {
      _last_retry = now;   // stack out of buffers, back off
    }
  }

  if (!recv_queue.isEmpty()) {
    size_t len = recv_queue.peek().len;
    memcpy(dest, recv_queue.peek().buf, len);
    recv_queue.pop();
    BLE_DEBUG_PRINTLN("readBytes: sz=%d, hdr=%d", (uint32_t) len, (uint32_t) dest[0]);
    return len;
  }
  return 0;
}

bool SerialBLEInterface::isConnected() const {
  return deviceConnected;
}
