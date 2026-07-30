#include "SerialBLEInterface.h"
#include <Arduino.h>

#define BLE_HEALTH_CHECK_INTERVAL 10000

void SerialBLEInterface::onConnect(BLEServer* p) {
  (void)p;
  _isConnected = true;
  send_queue_len = 0;
  recv_queue_len = 0;
}

void SerialBLEInterface::onDisconnect(BLEServer* p) {
  (void)p;
  _isConnected = false;
  send_queue_len = 0;
  recv_queue_len = 0;
  if (_isEnabled) {
    BLE.startAdvertising();
  }
}

void SerialBLEInterface::onWrite(BLECharacteristic* c) {
  size_t len = c->valueLen();
  const uint8_t* data = (const uint8_t*)c->valueData();
  if (len > 0 && len <= MAX_FRAME_SIZE && recv_queue_len < FRAME_QUEUE_SIZE) {
    recv_queue[recv_queue_len].len = len;
    memcpy(recv_queue[recv_queue_len].buf, data, len);
    recv_queue_len++;
  }
}

void SerialBLEInterface::shiftSendQueueLeft() {
  if (send_queue_len > 0) {
    send_queue_len--;
    for (uint8_t i = 0; i < send_queue_len; i++) send_queue[i] = send_queue[i + 1];
  }
}

void SerialBLEInterface::shiftRecvQueueLeft() {
  if (recv_queue_len > 0) {
    recv_queue_len--;
    for (uint8_t i = 0; i < recv_queue_len; i++) recv_queue[i] = recv_queue[i + 1];
  }
}

void SerialBLEInterface::begin(const char* prefix, char* name, uint32_t pin_code) {
  (void)pin_code;

  char dev_name[48];
  snprintf(dev_name, sizeof(dev_name), "%s%s", prefix, name);

  BLE.begin(String(dev_name));

  _server = BLE.server();
  _server->setCallbacks(this);

  _service = new BLEService(String(SERVICE_UUID));
  _txChar = new BLECharacteristic(String(TX_UUID), BLERead | BLENotify);
  _rxChar = new BLECharacteristic(String(RX_UUID), BLEWrite | BLEWriteWithoutResponse);
  _rxChar->setCallbacks(this);

  _service->addCharacteristic(_txChar);
  _service->addCharacteristic(_rxChar);
  _server->addService(_service);

  enable();
}

void SerialBLEInterface::enable() {
  if (_isEnabled) return;
  _isEnabled = true;
  _last_health_check = millis();
  BLE.startAdvertising();
}

void SerialBLEInterface::disable() {
  _isEnabled = false;
  _isConnected = false;
  BLE.stopAdvertising();
}

size_t SerialBLEInterface::writeFrame(const uint8_t src[], size_t len) {
  if (!_isConnected || len == 0 || len > MAX_FRAME_SIZE) return 0;
  if (send_queue_len >= FRAME_QUEUE_SIZE) return 0;
  send_queue[send_queue_len].len = len;
  memcpy(send_queue[send_queue_len].buf, src, len);
  send_queue_len++;
  return len;
}

size_t SerialBLEInterface::checkRecvFrame(uint8_t dest[]) {
  // send queued frames
  if (send_queue_len > 0 && _isConnected && millis() >= _last_write + BLE_WRITE_MIN_INTERVAL) {
    _last_write = millis();
    _txChar->setValue(send_queue[0].buf, send_queue[0].len);
    shiftSendQueueLeft();
  }

  // return next received frame
  if (recv_queue_len > 0) {
    size_t len = recv_queue[0].len;
    memcpy(dest, recv_queue[0].buf, len);
    shiftRecvQueueLeft();
    return len;
  }

  // advertising watchdog
  if (_isEnabled && !_isConnected) {
    unsigned long now = millis();
    if (now - _last_health_check >= BLE_HEALTH_CHECK_INTERVAL) {
      _last_health_check = now;
      BLE.startAdvertising();
    }
  }

  return 0;
}