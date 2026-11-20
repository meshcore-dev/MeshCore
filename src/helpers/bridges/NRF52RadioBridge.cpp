#include "NRF52RadioBridge.h"

#ifdef WITH_NRF52_WIRELESS_BRIDGE

#include <nrf.h>
#include <nrf_sdm.h>

// Fixed on-air network address ("MCBR" + 0xE7 prefix); isolation between
// networks sharing a channel is done by bridge_secret, like ESP-NOW
#define NRF52_WIRELESS_BRIDGE_BASE_ADDR   0x4D434252
#define NRF52_WIRELESS_BRIDGE_ADDR_PREFIX 0xE7

#define NRF52_WIRELESS_BRIDGE_IRQ_PRIORITY 3

NRF52RadioBridge *NRF52RadioBridge::_instance = nullptr;

extern "C" void RADIO_IRQHandler(void) {
  NRF52RadioBridge::handleIrq();
}

void NRF52RadioBridge::handleIrq() {
  if (_instance) {
    _instance->onRadioIrq();
  }
}

NRF52RadioBridge::NRF52RadioBridge(NodePrefs *prefs, mesh::PacketManager *mgr, mesh::RTCClock *rtc)
    : BridgeBase(prefs, mgr, rtc), _rx_head(0), _rx_tail(0), _tx_head(0), _tx_tail(0),
      _tx_in_flight(false), _tx_started_at(0), _tx_next_attempt(0), _tx_attempts(0), _state(STATE_OFF),
      _rx_busy(false), _tx_done(false), _hfxo_started(false) {
  _instance = this;
}

uint8_t NRF52RadioBridge::channelToFrequency(uint8_t channel) {
  // ~1MHz wide GFSK fits into the gaps that Wi-Fi 1/6/11 and BLE advertising (2402/2426/2480) leave free
  static const uint8_t freqs[NUM_CHANNELS] = {
    82, // 2482 MHz: above BLE adv ch39, below the 2483.5 MHz band edge (default)
    50, // 2450 MHz: between Wi-Fi ch6 and ch11
    24, // 2424 MHz: between Wi-Fi ch1 and BLE adv ch38
  };
  if (channel < 1 || channel > NUM_CHANNELS) channel = 1;
  return freqs[channel - 1];
}

int8_t NRF52RadioBridge::supportedTxPower(int8_t dbm) {
  static const int8_t steps[] = {
#if defined(RADIO_TXPOWER_TXPOWER_Pos8dBm)
    8, 7, 6, 5,
#endif
    4, 3,
#if defined(RADIO_TXPOWER_TXPOWER_Pos2dBm)
    2,
#endif
    0, -4, -8, -12, -16,
  };
  for (int8_t step : steps) {
    if (dbm >= step) return step;
  }
  return -20;
}

static uint32_t txPowerRegister(int8_t dbm) {
  // TXPOWER register values are the dBm value as two's complement
  return (uint8_t)NRF52RadioBridge::supportedTxPower(dbm);
}

void NRF52RadioBridge::configureRadio() {
  // Power cycle the peripheral to get it into a known (reset) state
  NRF_RADIO->POWER = 0;
  NRF_RADIO->POWER = 1;

  // BLE 1M PHY modulation (+-250kHz deviation) with our own address and framing, so this is not BLE
  NRF_RADIO->MODE = RADIO_MODE_MODE_Ble_1Mbit << RADIO_MODE_MODE_Pos;
  NRF_RADIO->MODECNF0 = (RADIO_MODECNF0_RU_Default << RADIO_MODECNF0_RU_Pos) |
                        (RADIO_MODECNF0_DTX_Center << RADIO_MODECNF0_DTX_Pos);
  NRF_RADIO->TXPOWER = txPowerRegister(_prefs->bridge_tx_power) << RADIO_TXPOWER_TXPOWER_Pos;
  NRF_RADIO->FREQUENCY = channelToFrequency(_prefs->bridge_channel) << RADIO_FREQUENCY_FREQUENCY_Pos;

  // 5 byte address (4 byte base + 1 byte prefix) on logical address 0
  NRF_RADIO->BASE0 = NRF52_WIRELESS_BRIDGE_BASE_ADDR;
  NRF_RADIO->PREFIX0 = NRF52_WIRELESS_BRIDGE_ADDR_PREFIX << RADIO_PREFIX0_AP0_Pos;
  NRF_RADIO->TXADDRESS = 0;
  NRF_RADIO->RXADDRESSES = RADIO_RXADDRESSES_ADDR0_Msk;

  // 8-bit length field, no S0/S1
  NRF_RADIO->PCNF0 = (8 << RADIO_PCNF0_LFLEN_Pos) | (0 << RADIO_PCNF0_S0LEN_Pos) | (0 << RADIO_PCNF0_S1LEN_Pos)
#if defined(RADIO_PCNF0_PLEN_Pos)
                     | (RADIO_PCNF0_PLEN_8bit << RADIO_PCNF0_PLEN_Pos)
#endif
      ;
  NRF_RADIO->PCNF1 = (MAX_RADIO_PACKET_SIZE << RADIO_PCNF1_MAXLEN_Pos) | (0 << RADIO_PCNF1_STATLEN_Pos) |
                     (4 << RADIO_PCNF1_BALEN_Pos) | (RADIO_PCNF1_ENDIAN_Big << RADIO_PCNF1_ENDIAN_Pos) |
                     (RADIO_PCNF1_WHITEEN_Enabled << RADIO_PCNF1_WHITEEN_Pos);
  // Fixed whitening seed. Deriving it from the frequency (like BLE does from the channel index) made the
  // hardware CRC check fail on every frame for some seeds (e.g. 0x52, 0x72) even though payload and CRC
  // bytes were received intact; 0x58 was verified on all bridge channels.
  NRF_RADIO->DATAWHITEIV = 0x58;

  // 16-bit CRC-CCITT over address + payload
  NRF_RADIO->CRCCNF = (RADIO_CRCCNF_LEN_Two << RADIO_CRCCNF_LEN_Pos) |
                      (RADIO_CRCCNF_SKIPADDR_Include << RADIO_CRCCNF_SKIPADDR_Pos);
  NRF_RADIO->CRCINIT = 0xFFFF;
  NRF_RADIO->CRCPOLY = 0x11021;

  NRF_RADIO->INTENCLR = 0xFFFFFFFF;
  NRF_RADIO->INTENSET = RADIO_INTENSET_ADDRESS_Msk | RADIO_INTENSET_END_Msk | RADIO_INTENSET_DISABLED_Msk;

  NVIC_SetPriority(RADIO_IRQn, NRF52_WIRELESS_BRIDGE_IRQ_PRIORITY);
  NVIC_ClearPendingIRQ(RADIO_IRQn);
}

void NRF52RadioBridge::startRx() {
  // radio must be DISABLED; called from ISR or with RADIO_IRQn masked
  _rx_busy = false;
  NRF_RADIO->PACKETPTR = (uint32_t)_rx_frames[_rx_head].data;
  NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_ADDRESS_RSSISTART_Msk |
                      RADIO_SHORTS_DISABLED_RSSISTOP_Msk;
  _state = STATE_RX;
  NRF_RADIO->TASKS_RXEN = 1;
}

void NRF52RadioBridge::onRadioIrq() {
  if (NRF_RADIO->EVENTS_ADDRESS) {
    NRF_RADIO->EVENTS_ADDRESS = 0;
    if (_state == STATE_RX) _rx_busy = true;
#if BRIDGE_DEBUG
    _dbg_addr++;
#endif
  }

  if (NRF_RADIO->EVENTS_END) {
    NRF_RADIO->EVENTS_END = 0;
    if (_state == STATE_RX) {
      _rx_busy = false;
      bool crc_ok = NRF_RADIO->CRCSTATUS == RADIO_CRCSTATUS_CRCSTATUS_CRCOk;
#if BRIDGE_DEBUG
      if (crc_ok) _dbg_crc_ok++; else _dbg_crc_err++;
#endif
      if (crc_ok) {
        _rx_frames[_rx_head].rssi = -(int8_t)NRF_RADIO->RSSISAMPLE;
        uint8_t next = (_rx_head + 1) % RX_SLOTS;
        if (next != _rx_tail) { // commit, otherwise ring is full and frame is dropped
          _rx_head = next;
        }
      }
      // radio sits in RXIDLE after END, re-arm with the (possibly new) slot
      NRF_RADIO->PACKETPTR = (uint32_t)_rx_frames[_rx_head].data;
      NRF_RADIO->TASKS_START = 1;
    }
  }

  if (NRF_RADIO->EVENTS_DISABLED) {
    NRF_RADIO->EVENTS_DISABLED = 0;
    if (_state == STATE_TX) {
      _tx_done = true;
      startRx();
    }
  }

  // flush the event clears before leaving the ISR, avoids spurious re-entry
  (void)NRF_RADIO->EVENTS_DISABLED;
}

void NRF52RadioBridge::begin() {
  BRIDGE_DEBUG_PRINTLN("Initializing...\n");

  uint8_t sd_enabled = 0;
  sd_softdevice_is_enabled(&sd_enabled);
  if (sd_enabled) {
    // SoftDevice owns the RADIO peripheral (BLE active), we can't touch it
    BRIDGE_DEBUG_PRINTLN("SoftDevice enabled, radio not available\n");
    return;
  }

  // RADIO requires the external high frequency crystal
  if ((NRF_CLOCK->HFCLKSTAT & (CLOCK_HFCLKSTAT_SRC_Msk | CLOCK_HFCLKSTAT_STATE_Msk)) !=
      ((CLOCK_HFCLKSTAT_SRC_Xtal << CLOCK_HFCLKSTAT_SRC_Pos) | CLOCK_HFCLKSTAT_STATE_Msk)) {
    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
    NRF_CLOCK->TASKS_HFCLKSTART = 1;
    uint32_t start = millis();
    while (!NRF_CLOCK->EVENTS_HFCLKSTARTED) {
      if (millis() - start > 10) {
        BRIDGE_DEBUG_PRINTLN("HFXO failed to start\n");
        NRF_CLOCK->TASKS_HFCLKSTOP = 1;
        return;
      }
    }
    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
    _hfxo_started = true;
  }

  _rx_head = _rx_tail = 0;
  _tx_head = _tx_tail = 0;
  _tx_in_flight = false;
  _tx_done = false;
  _tx_attempts = 0;
  _tx_next_attempt = 0;

  NVIC_DisableIRQ(RADIO_IRQn);
  configureRadio();
  startRx();
  NVIC_EnableIRQ(RADIO_IRQn);

  BRIDGE_DEBUG_PRINTLN("Listening on channel %d (%d MHz), tx power %d dBm\n", _prefs->bridge_channel,
                       2400 + channelToFrequency(_prefs->bridge_channel), supportedTxPower(_prefs->bridge_tx_power));

  // Update bridge state
  _initialized = true;
}

void NRF52RadioBridge::end() {
  BRIDGE_DEBUG_PRINTLN("Stopping...\n");

  if (!_initialized) return;

  NVIC_DisableIRQ(RADIO_IRQn);
  _state = STATE_OFF;
  NRF_RADIO->INTENCLR = 0xFFFFFFFF;
  NRF_RADIO->SHORTS = 0;
  NRF_RADIO->EVENTS_DISABLED = 0;
  NRF_RADIO->TASKS_DISABLE = 1;
  uint32_t start = micros();
  while (!NRF_RADIO->EVENTS_DISABLED && (micros() - start) < 200) {
  }
  NRF_RADIO->EVENTS_DISABLED = 0;

  // leave the peripheral in reset state (e.g. before SoftDevice is enabled for OTA)
  NRF_RADIO->POWER = 0;
  NRF_RADIO->POWER = 1;
  NVIC_ClearPendingIRQ(RADIO_IRQn);

  if (_hfxo_started) {
    NRF_CLOCK->TASKS_HFCLKSTOP = 1;
    _hfxo_started = false;
  }

  _tx_in_flight = false;

  // Update bridge state
  _initialized = false;
}

bool NRF52RadioBridge::isChannelClear() {
  if (_rx_busy) return false;

  // Only valid while the radio is in RX, which is the case whenever no TX is in flight
  NRF_RADIO->EVENTS_RSSIEND = 0;
  NRF_RADIO->TASKS_RSSISTART = 1;
  uint32_t start = micros();
  while (!NRF_RADIO->EVENTS_RSSIEND) {
    if (micros() - start > 50) return true; // no sample, don't block TX
  }
  NRF_RADIO->EVENTS_RSSIEND = 0;
  int rssi = -(int)NRF_RADIO->RSSISAMPLE;
  return rssi < NRF52_WIRELESS_BRIDGE_LBT_RSSI && !_rx_busy;
}

void NRF52RadioBridge::transmitFrame(RadioFrame *frame) {
  NVIC_DisableIRQ(RADIO_IRQn);

  // RX -> DISABLED, then ramp up TX; END_DISABLE brings us back to DISABLED and the ISR restarts RX
  _state = STATE_OFF;
  NRF_RADIO->SHORTS = 0;
  NRF_RADIO->EVENTS_DISABLED = 0;
  NRF_RADIO->TASKS_DISABLE = 1;
  uint32_t start = micros();
  while (!NRF_RADIO->EVENTS_DISABLED && (micros() - start) < 200) {
  }
  NRF_RADIO->EVENTS_DISABLED = 0;
  NRF_RADIO->EVENTS_ADDRESS = 0;
  NRF_RADIO->EVENTS_END = 0;
  _rx_busy = false;

  NRF_RADIO->PACKETPTR = (uint32_t)frame->data;
  NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_END_DISABLE_Msk;
  _tx_done = false;
  _state = STATE_TX;
  NRF_RADIO->TASKS_TXEN = 1;

  NVIC_ClearPendingIRQ(RADIO_IRQn);
  NVIC_EnableIRQ(RADIO_IRQn);
}

void NRF52RadioBridge::serviceTx() {
  if (_tx_in_flight) {
    if (_tx_done) {
      _tx_in_flight = false;
      _tx_tail = (_tx_tail + 1) % TX_SLOTS;
      BRIDGE_DEBUG_PRINTLN("TX done\n");
    } else if (millis() - _tx_started_at > TX_TIMEOUT_MS) {
      // should never happen, recover the radio and drop the frame
      BRIDGE_DEBUG_PRINTLN("TX timeout, resetting radio\n");
      NVIC_DisableIRQ(RADIO_IRQn);
      configureRadio();
      startRx();
      NVIC_EnableIRQ(RADIO_IRQn);
      _tx_in_flight = false;
      _tx_tail = (_tx_tail + 1) % TX_SLOTS;
    } else {
      return;
    }
  }

  if (_tx_tail == _tx_head) return; // nothing queued
  if ((int32_t)(millis() - _tx_next_attempt) < 0) return; // backing off

  if (_tx_attempts < MAX_LBT_ATTEMPTS && !isChannelClear()) {
    _tx_attempts++;
    _tx_next_attempt = millis() + random(1, 4 * _tx_attempts + 2);
    return;
  }

  _tx_attempts = 0;
  _tx_in_flight = true;
  _tx_started_at = millis();
  transmitFrame(&_tx_frames[_tx_tail]);
}

void NRF52RadioBridge::loop() {
  if (!_initialized) return;

  // drain received frames (ISR is the producer, we only advance the tail)
  while (_rx_tail != _rx_head) {
    processRxFrame(&_rx_frames[_rx_tail]);
    _rx_tail = (_rx_tail + 1) % RX_SLOTS;
  }

  serviceTx();

#if BRIDGE_DEBUG
  if ((int32_t)(millis() - _dbg_next_report) >= 0) {
    _dbg_next_report = millis() + 5000;
    int rssi = 0;
    if (!_tx_in_flight) {
      NRF_RADIO->EVENTS_RSSIEND = 0;
      NRF_RADIO->TASKS_RSSISTART = 1;
      uint32_t start = micros();
      while (!NRF_RADIO->EVENTS_RSSIEND && micros() - start < 50) {
      }
      rssi = -(int)NRF_RADIO->RSSISAMPLE;
    }
    // addr counts every address match (incl. own TX), crc_ok + crc_err only receptions
    BRIDGE_DEBUG_PRINTLN("stats: addr=%u crc_ok=%u crc_err=%u noise=%d\n", _dbg_addr, _dbg_crc_ok, _dbg_crc_err,
                         rssi);
  }
#endif
}

void NRF52RadioBridge::processRxFrame(const RadioFrame *frame) {
  const size_t len = frame->data[0];
  const uint8_t *data = &frame->data[1];

  // Ignore packets that are too small to contain header + checksum
  if (len < (BRIDGE_MAGIC_SIZE + BRIDGE_CHECKSUM_SIZE)) {
    BRIDGE_DEBUG_PRINTLN("RX packet too small, len=%d\n", len);
    return;
  }

  // Check packet header magic
  uint16_t received_magic = (data[0] << 8) | data[1];
  if (received_magic != BRIDGE_PACKET_MAGIC) {
    BRIDGE_DEBUG_PRINTLN("RX invalid magic 0x%04X\n", received_magic);
    return;
  }

  // Make a copy we can decrypt
  uint8_t decrypted[MAX_RADIO_PACKET_SIZE];
  const size_t encryptedDataLen = len - BRIDGE_MAGIC_SIZE;
  memcpy(decrypted, data + BRIDGE_MAGIC_SIZE, encryptedDataLen);

  // Try to decrypt (checksum + payload)
  xorCrypt(decrypted, encryptedDataLen);

  // Validate checksum
  uint16_t received_checksum = (decrypted[0] << 8) | decrypted[1];
  const size_t payloadLen = encryptedDataLen - BRIDGE_CHECKSUM_SIZE;

  if (!validateChecksum(decrypted + BRIDGE_CHECKSUM_SIZE, payloadLen, received_checksum)) {
    // Failed to decrypt - likely from a different network
    BRIDGE_DEBUG_PRINTLN("RX checksum mismatch, rcv=0x%04X\n", received_checksum);
    return;
  }

  BRIDGE_DEBUG_PRINTLN("RX, payload_len=%d, rssi=%d\n", payloadLen, frame->rssi);

  // Create mesh packet
  mesh::Packet *pkt = _mgr->allocNew();
  if (!pkt) return;

  if (pkt->readFrom(decrypted + BRIDGE_CHECKSUM_SIZE, payloadLen)) {
    onPacketReceived(pkt);
  } else {
    _mgr->free(pkt);
  }
}

void NRF52RadioBridge::sendPacket(mesh::Packet *packet) {
  // Guard against uninitialized state
  if (_initialized == false) {
    return;
  }

  // First validate the packet pointer
  if (!packet) {
    BRIDGE_DEBUG_PRINTLN("TX invalid packet pointer\n");
    return;
  }

  if (!_seen_packets.wasSeen(packet)) {
    _seen_packets.markSeen(packet);

    uint8_t next = (_tx_head + 1) % TX_SLOTS;
    if (next == _tx_tail) {
      BRIDGE_DEBUG_PRINTLN("TX queue full, packet dropped\n");
      return;
    }

    uint8_t sizingBuffer[MAX_TRANS_UNIT + 1];
    uint16_t meshPacketLen = packet->writeTo(sizingBuffer);

    // Check if packet fits within our maximum payload size
    if (meshPacketLen > MAX_PAYLOAD_SIZE) {
      BRIDGE_DEBUG_PRINTLN("TX packet too large (payload=%d, max=%d)\n", meshPacketLen, MAX_PAYLOAD_SIZE);
      return;
    }

    uint8_t *buffer = &_tx_frames[_tx_head].data[1];

    // Write magic header (2 bytes)
    buffer[0] = (BRIDGE_PACKET_MAGIC >> 8) & 0xFF;
    buffer[1] = BRIDGE_PACKET_MAGIC & 0xFF;

    // Write packet payload starting after magic header and checksum
    const size_t packetOffset = BRIDGE_MAGIC_SIZE + BRIDGE_CHECKSUM_SIZE;
    memcpy(buffer + packetOffset, sizingBuffer, meshPacketLen);

    // Calculate and add checksum (only of the payload)
    uint16_t checksum = fletcher16(buffer + packetOffset, meshPacketLen);
    buffer[2] = (checksum >> 8) & 0xFF; // High byte
    buffer[3] = checksum & 0xFF;        // Low byte

    // Encrypt payload and checksum (not including magic header)
    xorCrypt(buffer + BRIDGE_MAGIC_SIZE, meshPacketLen + BRIDGE_CHECKSUM_SIZE);

    // Length byte for the radio: magic header + checksum + payload
    _tx_frames[_tx_head].data[0] = BRIDGE_MAGIC_SIZE + BRIDGE_CHECKSUM_SIZE + meshPacketLen;
    _tx_head = next;

    BRIDGE_DEBUG_PRINTLN("TX queued, len=%d\n", meshPacketLen);

    // try to send right away instead of waiting for the next loop()
    serviceTx();
  }
}

void NRF52RadioBridge::onPacketReceived(mesh::Packet *packet) {
  handleReceivedPacket(packet);
}

#endif
