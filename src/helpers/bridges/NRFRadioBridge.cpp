#include "NRFRadioBridge.h"

#ifdef WITH_NRF_BRIDGE

#include <Arduino.h>
#include <nrf.h>
#include <nrf_sdm.h>
#include <string.h>

// Static member to handle the interrupt
NRFRadioBridge *NRFRadioBridge::_instance = nullptr;

// Bounded, so a peripheral that never reports ready cannot hang the node. Both
// events are specified in microseconds, so these are generous.
static const uint32_t HFCLK_START_SPINS = 100000;
static const uint32_t RADIO_DISABLE_SPINS = 100000;

// The RADIO vector. Only the SoftDevice would otherwise claim it, and begin()
// refuses to run while the SoftDevice is enabled.
extern "C" void RADIO_IRQHandler(void) {
  NRFRadioBridge::radio_isr();
}

void NRFRadioBridge::radio_isr() {
  if (_instance) {
    _instance->onRadioEvent();
  }
}

NRFRadioBridge::NRFRadioBridge(NodePrefs *prefs, mesh::PacketManager *mgr, mesh::RTCClock *rtc)
    : BridgeBase(prefs, mgr, rtc), _radio_state(RADIO_OFF), _rx_head(0), _rx_tail(0) {
  _instance = this;
}

bool NRFRadioBridge::startHighFreqClock() {
  const uint32_t running = (CLOCK_HFCLKSTAT_STATE_Running << CLOCK_HFCLKSTAT_STATE_Pos) |
                           (CLOCK_HFCLKSTAT_SRC_Xtal << CLOCK_HFCLKSTAT_SRC_Pos);
  const uint32_t mask = CLOCK_HFCLKSTAT_STATE_Msk | CLOCK_HFCLKSTAT_SRC_Msk;

  // Re-triggering the task with the crystal already running produces no
  // HFCLKSTARTED event, so the wait below would spin out for nothing. That is the
  // path `set bridge.channel` takes when it restarts the bridge.
  if ((NRF_CLOCK->HFCLKSTAT & mask) == running) return true;

  NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
  NRF_CLOCK->TASKS_HFCLKSTART = 1;
  for (uint32_t i = 0; i < HFCLK_START_SPINS && NRF_CLOCK->EVENTS_HFCLKSTARTED == 0; i++) {
    __NOP();
  }

  return (NRF_CLOCK->HFCLKSTAT & mask) == running;
}

void NRFRadioBridge::configureRadio(uint8_t frequency) {
  NRF_RADIO->MODE = RADIO_MODE_MODE_Nrf_1Mbit << RADIO_MODE_MODE_Pos;
  NRF_RADIO->FREQUENCY = frequency;

#if defined(RADIO_TXPOWER_TXPOWER_Pos8dBm)
  NRF_RADIO->TXPOWER = RADIO_TXPOWER_TXPOWER_Pos8dBm << RADIO_TXPOWER_TXPOWER_Pos;
#else
  NRF_RADIO->TXPOWER = RADIO_TXPOWER_TXPOWER_Pos4dBm << RADIO_TXPOWER_TXPOWER_Pos;
#endif

  // 8-bit LENGTH field, no S0/S1: the frame is a bare length byte plus payload
  NRF_RADIO->PCNF0 = (8 << RADIO_PCNF0_LFLEN_Pos) | (0 << RADIO_PCNF0_S0LEN_Pos) |
                     (0 << RADIO_PCNF0_S1LEN_Pos);

  // 5-byte address (1 prefix + 4 base), dynamic length, no whitening
  NRF_RADIO->PCNF1 = (MAX_NRF_PACKET_SIZE << RADIO_PCNF1_MAXLEN_Pos) |
                     (0 << RADIO_PCNF1_STATLEN_Pos) | (4 << RADIO_PCNF1_BALEN_Pos) |
                     (RADIO_PCNF1_ENDIAN_Big << RADIO_PCNF1_ENDIAN_Pos) |
                     (RADIO_PCNF1_WHITEEN_Disabled << RADIO_PCNF1_WHITEEN_Pos);

  // One shared address on logical address 0: transmit on it, listen for it
  NRF_RADIO->BASE0 = ADDRESS_BASE;
  NRF_RADIO->PREFIX0 = ADDRESS_PREFIX;
  NRF_RADIO->TXADDRESS = 0;
  NRF_RADIO->RXADDRESSES = 1;

  // CRC-16 over the payload, so a corrupted frame never reaches the bridge
  NRF_RADIO->CRCCNF = (RADIO_CRCCNF_LEN_Two << RADIO_CRCCNF_LEN_Pos) |
                      (RADIO_CRCCNF_SKIPADDR_Skip << RADIO_CRCCNF_SKIPADDR_Pos);
  NRF_RADIO->CRCPOLY = 0x11021;
  NRF_RADIO->CRCINIT = 0xFFFF;

  // Ramp up and start without CPU involvement, and disable once the frame ends,
  // so DISABLED is the single event the interrupt has to handle.
  NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_END_DISABLE_Msk;

  NRF_RADIO->INTENCLR = 0xFFFFFFFF;
  NRF_RADIO->INTENSET = RADIO_INTENSET_DISABLED_Msk;
}

void NRFRadioBridge::armReceive() {
  NRF_RADIO->PACKETPTR = (uint32_t)_rx_raw;
  NRF_RADIO->EVENTS_END = 0;
  NRF_RADIO->EVENTS_DISABLED = 0;
  _radio_state = RADIO_RX;
  NRF_RADIO->TASKS_RXEN = 1;
}

void NRFRadioBridge::disableRadio() {
  if (NRF_RADIO->STATE == RADIO_STATE_STATE_Disabled) {
    return;  // already stopped; TASKS_DISABLE would produce no event to wait for
  }

  NRF_RADIO->EVENTS_DISABLED = 0;
  NRF_RADIO->TASKS_DISABLE = 1;

  for (uint32_t i = 0; i < RADIO_DISABLE_SPINS && NRF_RADIO->EVENTS_DISABLED == 0; i++) {
    __NOP();
  }

  NRF_RADIO->EVENTS_DISABLED = 0;
  _radio_state = RADIO_OFF;
}

void NRFRadioBridge::onRadioEvent() {
  if (NRF_RADIO->EVENTS_DISABLED == 0) return;
  NRF_RADIO->EVENTS_DISABLED = 0;

  if (_radio_state == RADIO_RX && NRF_RADIO->CRCSTATUS == 1) {
    const uint8_t len = _rx_raw[0];
    const uint8_t next = (uint8_t)((_rx_head + 1) % RX_QUEUE_DEPTH);

    // Copy the frame out rather than parsing it here: the packet pool and the
    // seen-packet table are not safe to touch from an interrupt. A full queue
    // drops the frame, which the radio would have done anyway.
    if (len > 0 && next != _rx_tail) {
      memcpy(_rx_queue[_rx_head], &_rx_raw[1], len);
      _rx_queue_len[_rx_head] = len;
      _rx_head = next;
    }
  }

  // Back to listening, unless the bridge is being torn down: re-arming then
  // would leave a receiver running past end(), filling a queue nothing drains.
  if (_initialized) {
    armReceive();
  }
}

void NRFRadioBridge::begin() {
  BRIDGE_DEBUG_PRINTLN("Initializing...\n");

  // One RADIO, and the SoftDevice owns it whenever BLE is up
  uint8_t sd_enabled = 0;
  sd_softdevice_is_enabled(&sd_enabled);
  if (sd_enabled) {
    BRIDGE_DEBUG_PRINTLN("Cannot start while the SoftDevice owns the radio\n");
    return;
  }

  // Refused rather than corrected, so a pref predating bridge_channel behaves as
  // it does on ESP-NOW, where esp_wifi_set_channel() rejects the same value.
  const int frequency = NRFRadioChannel::frequencyFor(_prefs->bridge_channel);
  if (frequency < 0) {
    BRIDGE_DEBUG_PRINTLN("Error setting radio channel to %d\n", _prefs->bridge_channel);
    return;
  }

  if (!startHighFreqClock()) {
    BRIDGE_DEBUG_PRINTLN("HF clock did not start\n");
    return;
  }

  configureRadio((uint8_t)frequency);

  NVIC_ClearPendingIRQ(RADIO_IRQn);
  // Priority 2 stays available to the application if the SoftDevice is ever up
  NVIC_SetPriority(RADIO_IRQn, 2);
  NVIC_EnableIRQ(RADIO_IRQn);

  _initialized = true;

  armReceive();
}

void NRFRadioBridge::end() {
  BRIDGE_DEBUG_PRINTLN("Stopping...\n");

  _initialized = false;

  // Silence the interrupt before tearing down, so nothing can push into the queue
  NVIC_DisableIRQ(RADIO_IRQn);
  __DSB();
  __ISB();
  NRF_RADIO->INTENCLR = 0xFFFFFFFF;

  disableRadio();
  NRF_RADIO->SHORTS = 0;
  NVIC_ClearPendingIRQ(RADIO_IRQn);

  // The crystal is left running on purpose. TASKS_HFCLKSTOP is not reference
  // counted and USB requests the same crystal (TinyUSB's hfclk_enable), so
  // stopping it would cut off the console of whoever just disabled the bridge.

  _rx_head = _rx_tail = 0;
}

void NRFRadioBridge::loop() {
  if (!_initialized) return;

  // USB releases the same crystal on suspend without telling anyone. One
  // register read once it is running.
  startHighFreqClock();

  for (uint8_t i = 0; i < RX_DRAIN_PER_LOOP && _rx_tail != _rx_head; i++) {
    handleFrame(_rx_queue[_rx_tail], _rx_queue_len[_rx_tail]);
    _rx_tail = (uint8_t)((_rx_tail + 1) % RX_QUEUE_DEPTH);
  }
}

void NRFRadioBridge::xorCrypt(uint8_t *data, size_t len) {
  size_t keyLen = strlen(_prefs->bridge_secret);
  if (keyLen == 0) return;
  for (size_t i = 0; i < len; i++) {
    data[i] ^= _prefs->bridge_secret[i % keyLen];
  }
}

void NRFRadioBridge::handleFrame(const uint8_t *frame, size_t len) {
  // Ignore frames that are too small to contain header + checksum
  if (len < (BRIDGE_MAGIC_SIZE + BRIDGE_CHECKSUM_SIZE)) {
    BRIDGE_DEBUG_PRINTLN("RX packet too small, len=%d\n", (int)len);
    return;
  }

  // Check packet header magic
  uint16_t received_magic = (frame[0] << 8) | frame[1];
  if (received_magic != BRIDGE_PACKET_MAGIC) {
    BRIDGE_DEBUG_PRINTLN("RX invalid magic 0x%04X\n", received_magic);
    return;
  }

  // Make a copy we can decrypt
  uint8_t decrypted[MAX_NRF_PACKET_SIZE];
  const size_t encryptedDataLen = len - BRIDGE_MAGIC_SIZE;
  memcpy(decrypted, frame + BRIDGE_MAGIC_SIZE, encryptedDataLen);

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

  BRIDGE_DEBUG_PRINTLN("RX, payload_len=%d\n", (int)payloadLen);

  // Create mesh packet
  mesh::Packet *pkt = _mgr->allocNew();
  if (!pkt) return;

  if (pkt->readFrom(decrypted + BRIDGE_CHECKSUM_SIZE, payloadLen)) {
    onPacketReceived(pkt);
  } else {
    _mgr->free(pkt);
  }
}

bool NRFRadioBridge::sendFrame(const uint8_t *frame, size_t len) {
  if (len == 0 || len > MAX_NRF_PACKET_SIZE) return false;

  // The interrupt drives the same state machine, so keep it out while the radio
  // turns around. It runs above thread mode, so once these barriers retire it
  // cannot be mid-execution: it either finished already or can no longer start.
  NVIC_DisableIRQ(RADIO_IRQn);
  __DSB();
  __ISB();

  if (_radio_state == RADIO_TX) {
    NVIC_EnableIRQ(RADIO_IRQn);
    return false;  // already transmitting
  }

  // Stop listening. A frame arriving now is lost, which is inherent to a
  // half-duplex radio and no different to a collision on air.
  disableRadio();

  // Clear the pending bit left by the disable above BEFORE arming, never after: a
  // short frame completes in tens of microseconds, so a clear after TASKS_TXEN can
  // drop its completion, and nothing re-latches the NVIC bit.
  NVIC_ClearPendingIRQ(RADIO_IRQn);

  _tx_raw[0] = (uint8_t)len;
  memcpy(&_tx_raw[1], frame, len);

  NRF_RADIO->PACKETPTR = (uint32_t)_tx_raw;
  NRF_RADIO->EVENTS_END = 0;
  NRF_RADIO->EVENTS_DISABLED = 0;
  _radio_state = RADIO_TX;

  NVIC_EnableIRQ(RADIO_IRQn);
  NRF_RADIO->TASKS_TXEN = 1;
  return true;
}

void NRFRadioBridge::sendPacket(mesh::Packet *packet) {
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
    // Create a temporary buffer just for size calculation and reuse for actual writing
    uint8_t sizingBuffer[MAX_PAYLOAD_SIZE];
    uint16_t meshPacketLen = packet->writeTo(sizingBuffer);

    // Check if packet fits within our maximum payload size
    if (meshPacketLen > MAX_PAYLOAD_SIZE) {
      BRIDGE_DEBUG_PRINTLN("TX packet too large (payload=%d, max=%d)\n", meshPacketLen,
                           MAX_PAYLOAD_SIZE);
      return;
    }

    uint8_t buffer[MAX_NRF_PACKET_SIZE];

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

    // Total packet size: magic header + checksum + payload
    const size_t totalPacketSize = BRIDGE_MAGIC_SIZE + BRIDGE_CHECKSUM_SIZE + meshPacketLen;

    if (sendFrame(buffer, totalPacketSize)) {
      BRIDGE_DEBUG_PRINTLN("TX, len=%d\n", meshPacketLen);
    } else {
      BRIDGE_DEBUG_PRINTLN("TX FAILED!\n");
    }
  }
}

void NRFRadioBridge::onPacketReceived(mesh::Packet *packet) {
  handleReceivedPacket(packet);
}

#endif
