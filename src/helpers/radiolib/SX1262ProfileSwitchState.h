#pragma once
#include <stddef.h>
#include <stdint.h>

// A short-lived capability for reusing a fully configured continuous-RX
// context. Standby outside the owned hop, staging, sleep and reset revoke it.
struct SX1262ProfileSwitchState {
  bool enabled;
  bool rxValid = false;
  bool open = false;
  bool active = false;
  bool standbyReady = false;
  int16_t error = 0;
  bool modulationValid = false;
  uint8_t modulationSf = 0, modulationBw = 0, modulationCr = 0, modulationLdro = 0;
  bool packetValid = false;
  size_t packetPreamble = 0;
  uint8_t packetCrc = 0, packetImplicitLen = 0, packetHeader = 0, packetIq = 0;

  explicit SX1262ProfileSwitchState(bool enable) : enabled(enable) {}
  void invalidateModulation() { modulationValid = false; }
  void invalidatePacket() { packetValid = false; }
  void invalidate() {
    rxValid = active = standbyReady = false;
    invalidateModulation();
    invalidatePacket();
  }
  void begin(bool continuousRx, bool warm) {
    open = true;
    error = 0;
    standbyReady = false;
    active = enabled && rxValid && continuousRx && warm;
  }
  void standbyResult(int16_t result) {
    if (result != 0) {
      if (open) error = result;
      invalidate();
    } else if (active) {
      standbyReady = true;
    }
  }
  bool canResumeFast() const { return open && active && rxValid && standbyReady && error == 0; }
  // Only trust an acknowledged tuple inside the same owned RX context. The
  // normal RadioLib software fields alone do not prove a write succeeded.
  bool matchesModulation(uint8_t sf, uint8_t bw, uint8_t cr, uint8_t ldro) const {
    return canResumeFast() && modulationValid && modulationSf == sf
        && modulationBw == bw && modulationCr == cr && modulationLdro == ldro;
  }
  void modulationResult(int16_t result, uint8_t sf, uint8_t bw, uint8_t cr, uint8_t ldro) {
    if (result != 0) {
      if (open) error = result;
      invalidate();
      return;
    }
    modulationSf = sf;
    modulationBw = bw;
    modulationCr = cr;
    modulationLdro = ldro;
    modulationValid = true;
  }
  bool matchesPacket(size_t preamble, uint8_t crc, uint8_t implicitLen,
                     uint8_t header, uint8_t iq) const {
    return canResumeFast() && packetValid && packetPreamble == preamble
        && packetCrc == crc && packetImplicitLen == implicitLen
        && packetHeader == header && packetIq == iq;
  }
  bool matchesPacketIq(uint8_t iq) const {
    return canResumeFast() && packetValid && packetIq == iq;
  }
  void packetResult(int16_t result, size_t preamble, uint8_t crc,
                    uint8_t implicitLen, uint8_t header, uint8_t iq) {
    if (result != 0) {
      if (open) error = result;
      invalidate();
      return;
    }
    packetPreamble = preamble;
    packetCrc = crc;
    packetImplicitLen = implicitLen;
    packetHeader = header;
    packetIq = iq;
    packetValid = true;
  }
  bool failed() const { return open && error != 0; }
  void rxResult(int16_t result, bool reusable) {
    if (result != 0) {
      if (open) error = result;
      invalidate();
    } else {
      rxValid = enabled && reusable;
      standbyReady = false;
    }
  }
  void end(bool success) {
    if (!success) invalidate();
    open = active = standbyReady = false;
    error = 0;
  }
};
