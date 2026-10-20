#pragma once

#include <RadioLib.h>
#include "MeshCore.h"
#include <math.h>
#include "LR1110ProfileSwitchState.h"

#ifndef MC_LR1110_FAST_PROFILE_SWITCH
#define MC_LR1110_FAST_PROFILE_SWITCH 1
#endif

class CustomLR1110 : public LR1110 {
  uint32_t _preambleMillis = 66;
  uint32_t _maxPayloadMillis = 3934;
  uint32_t _activityAt = 0;
  bool _headerSeen = false;
  bool _rx_boosted = false;

  LR1110ProfileSwitchState _profileSwitch{MC_LR1110_FAST_PROFILE_SWITCH != 0};
  bool _profileStandbyWarm = false;
  bool _profilePacketDirty = false;
  uint32_t _profileFastResumes = 0;

  public:
    CustomLR1110(Module *mod) : LR1110(mod) { }

    void setProfileSwitchOptimization(bool enabled) {
      _profileSwitch.enabled = enabled;
      _profileSwitch.invalidate();
      _profilePacketDirty = false;
    }

    void setProfileStandbyWarm(bool enabled) {
      _profileStandbyWarm = enabled;
      if (!enabled) _profileSwitch.invalidate();
    }

    void beginProfileSwitch(bool continuousRx) {
      _profileSwitch.begin(continuousRx, _profileStandbyWarm);
    }

    void endProfileSwitch(bool success) { _profileSwitch.end(success); }

    bool profileSwitchFailed() const { return _profileSwitch.failed(); }

    uint32_t getOptimizedProfileSwitches() const { return _profileFastResumes; }

    int16_t snapshotIrqAfterRxExit(uint32_t* irq) {
      uint8_t reply[6] = {};
      auto& statusWidth = mod->spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_STATUS];
      const auto savedWidth = statusWidth;
      statusWidth = Module::BITS_0;
      const int16_t rc = mod->SPItransferStream(nullptr, 0, false, nullptr,
          reply, sizeof(reply), true);
      statusWidth = savedWidth;
      if (rc == RADIOLIB_ERR_NONE) {
        *irq = (uint32_t(reply[2]) << 24) | (uint32_t(reply[3]) << 16)
            | (uint32_t(reply[4]) << 8) | uint32_t(reply[5]);
      }
      return rc;
    }

    int16_t standby() override {
      const bool warm = _profileSwitch.active && _profileSwitch.rxValid;
      if (!warm) _profileSwitch.invalidate();
      // RadioLib 7.7.1 defines STANDBY_XOSC as 0, the same as STANDBY_RC.
      // The LR1110 command's XOSC selector is 1 (Semtech UM §2.1.1).
      bool useFs = warm;
      int16_t rc;
      if (useFs) {
        mod->setRfSwitchState(Module::MODE_IDLE);
        rc = setFs();
      } else if (warm) {
        rc = LR1110::standby(0x01, false);
      } else {
        rc = LR1110::standby();
      }
      _profileSwitch.standbyResult(rc);
      return rc;
    }

    int16_t standby(uint8_t mode) override {
      _profileSwitch.invalidate();
      return LR1110::standby(mode);
    }

    int16_t sleep() override {
      _profileSwitch.invalidate();
      return LR1110::sleep();
    }

    int16_t reset() {
      _profileSwitch.invalidate();
      return LR1110::reset();
    }

    int16_t stageMode(RadioModeType_t mode, RadioModeConfig_t* cfg) override {
      _profileSwitch.invalidate();
      return LR1110::stageMode(mode, cfg);
    }

    int16_t setPreambleLength(size_t symbols) override {
      if (_profileSwitch.canResumeFast()) {
        // Keep the acknowledged RX packet tuple until its preamble changes.
        if (preambleLengthLoRa != symbols) _profilePacketDirty = true;
        preambleLengthLoRa = symbols;
        return RADIOLIB_ERR_NONE;
      }
      _profileSwitch.invalidate();
      _profilePacketDirty = false;
      return LR1110::setPreambleLength(symbols);
    }

    int16_t setFrequency(float freq) {
      if (_profileSwitch.canResumeFast() && freq == freqMHz) return RADIOLIB_ERR_NONE;
      if (!_profileSwitch.canResumeFast()) _profileSwitch.invalidate();
      const int16_t rc = LR1110::setFrequency(freq);
      if (rc != RADIOLIB_ERR_NONE) _profileSwitch.invalidate();
      return rc;
    }

    int16_t setBandwidth(float bw, bool high = false) {
      _profileSwitch.modulationValid = false;
      return LR1110::setBandwidth(bw, high);
    }

    int16_t setSpreadingFactor(uint8_t sf, bool legacy = false) {
      _profileSwitch.modulationValid = false;
      return LR1110::setSpreadingFactor(sf, legacy);
    }

    int16_t setCodingRate(uint8_t cr, bool longInterleave = false) {
      _profileSwitch.modulationValid = false;
      return LR1110::setCodingRate(cr, longInterleave);
    }

    // Ordinary packet setters revoke the acknowledged continuous-RX tuple,
    // including failed writes. A later hop must rebuild normal RX first.
    int16_t setCRC(uint8_t len, uint32_t initial = 0x00001D0FUL,
                   uint32_t polynomial = 0x00001021UL, bool inverted = true) {
      _profileSwitch.invalidate();
      return LR1110::setCRC(len, initial, polynomial, inverted);
    }
    int16_t implicitHeader(size_t len) {
      _profileSwitch.invalidate();
      return LR1110::implicitHeader(len);
    }
    int16_t explicitHeader() {
      _profileSwitch.invalidate();
      return LR1110::explicitHeader();
    }
    int16_t invertIQ(bool enabled) override {
      _profileSwitch.invalidate();
      return LR1110::invertIQ(enabled);
    }
    int16_t setModem(ModemType_t modem) override {
      _profileSwitch.invalidate();
      return LR1110::setModem(modem);
    }

    int16_t setLoRaModulationParams(float bw, uint8_t sf, uint8_t cr) {
      if (sf < 5 || sf > 12) return RADIOLIB_ERR_INVALID_SPREADING_FACTOR;
      if (cr < 4 || cr > 8) return RADIOLIB_ERR_INVALID_CODING_RATE;
      uint8_t bwCode;
      if (fabsf(bw - 62.5f) < 0.01f) bwCode = RADIOLIB_LR11X0_LORA_BW_62_5;
      else if (fabsf(bw - 125.0f) < 0.01f) bwCode = RADIOLIB_LR11X0_LORA_BW_125_0;
      else if (fabsf(bw - 250.0f) < 0.01f) bwCode = RADIOLIB_LR11X0_LORA_BW_250_0;
      else if (fabsf(bw - 500.0f) < 0.01f) bwCode = RADIOLIB_LR11X0_LORA_BW_500_0;
      else return RADIOLIB_ERR_INVALID_BANDWIDTH;
      if (!_profileSwitch.canResumeFast()) {
        uint8_t type = RADIOLIB_LR11X0_PACKET_TYPE_NONE;
        const int16_t rc = getPacketType(&type);
        if (rc != RADIOLIB_ERR_NONE || type != RADIOLIB_LR11X0_PACKET_TYPE_LORA) {
          _profileSwitch.invalidate();
          return rc != RADIOLIB_ERR_NONE ? rc : RADIOLIB_ERR_WRONG_MODEM;
        }
      }
      const uint8_t effectiveLdro = ldroAuto
          ? (float(uint32_t(1) << sf) / bw >= 16.0f
                ? RADIOLIB_LR11X0_LORA_LDRO_ENABLED : RADIOLIB_LR11X0_LORA_LDRO_DISABLED)
          : ldrOptimize;
      const bool unchanged = _profileSwitch.matchesModulation(sf, bwCode, cr - 4, effectiveLdro);
      const auto oldSf = spreadingFactor, oldBw = bandwidth, oldCr = codingRate, oldLdro = ldrOptimize;
      const auto oldBwKhz = bandwidthKhz;
      spreadingFactor = sf;
      bandwidth = bwCode;
      bandwidthKhz = bw;
      codingRate = cr - 4;
      if (unchanged) {
        ldrOptimize = effectiveLdro;
        return RADIOLIB_ERR_NONE;
      }
      const int16_t rc = setModulationParamsLoRa(sf, bwCode, codingRate, effectiveLdro);
      _profileSwitch.modulationResult(rc, sf, bwCode, codingRate, ldrOptimize);
      if (rc != RADIOLIB_ERR_NONE) {
        spreadingFactor = oldSf;
        bandwidth = oldBw;
        bandwidthKhz = oldBwKhz;
        codingRate = oldCr;
        ldrOptimize = oldLdro;
      }
      return rc;
    }

    bool isChipBusy() {
      uint32_t busy = this->mod->getGpio();
      return busy != RADIOLIB_NC && this->mod->hal->digitalRead(busy);
    }

    int16_t begin(float freq = 434.0, float bw = 125.0, uint8_t sf = 9, uint8_t cr = 7,
                  uint8_t syncWord = RADIOLIB_LR11X0_LORA_SYNC_WORD_PRIVATE, int8_t power = 10,
                  uint16_t preambleLength = 8, float tcxoVoltage = 1.6) {
      _profileSwitch.invalidate();
      _profilePacketDirty = false;
      int16_t state = LR1110::begin(freq, bw, sf, cr, syncWord, power, preambleLength,
                                    tcxoVoltage);
      // RadioLib begin() defaults to LDO; use the LR1110 DC/DC regulator.
      if (state == RADIOLIB_ERR_NONE) state = setRegulatorDCDC();
      return state;
    }

    size_t getPacketLength(bool update) override {
      size_t len = LR1110::getPacketLength(update);
      if (len == 0 && getIrqStatus() & RADIOLIB_LR11X0_IRQ_HEADER_ERR) {
        // we've just received a corrupted packet
        // this may have triggered a bug causing subsequent packets to be shifted
        // call standby() to return radio to known-good state
        // recvRaw will call startReceive() to restart rx
        MESH_DEBUG_PRINTLN("LR1110: got header err, calling standby()");
        standby();
      }
      return len;
    }
    
    float getFreqMHz() const { return freqMHz; }

    int16_t setRxBoostedGainMode(bool en) {
      _rx_boosted = en;
      return LR1110::setRxBoostedGainMode(en);
    }

    bool getRxBoostedGainMode() const { return _rx_boosted; }

    int16_t startReceive() override {
      if (_profileSwitch.canResumeFast()) {
        _profileSwitch.rxValid = false;  // fail closed until the RX command ACKs
        int16_t rc = RADIOLIB_ERR_NONE;
        if (rc == RADIOLIB_ERR_NONE)
        {
          uint32_t pending = 0;
          rc = snapshotIrqAfterRxExit(&pending);
          if (rc == RADIOLIB_ERR_NONE && pending)
            rc = clearIrqState(RADIOLIB_LR11X0_IRQ_ALL);
        }
        if (rc == RADIOLIB_ERR_NONE && _profilePacketDirty) {
          rc = setPacketParamsLoRa(preambleLengthLoRa, headerType,
              RADIOLIB_LR11X0_MAX_PACKET_LENGTH, crcTypeLoRa, invertIQEnabled);
        }
        if (rc == RADIOLIB_ERR_NONE) {
          rxTimeout = RADIOLIB_LR11X0_RX_TIMEOUT_INF;
          mod->setRfSwitchState(Module::MODE_RX);
          rc = setRx(rxTimeout);
        }
        stagedMode = RADIOLIB_RADIO_MODE_NONE;
        if (rc == RADIOLIB_ERR_NONE) ++_profileFastResumes;
        if (rc == RADIOLIB_ERR_NONE) _profilePacketDirty = false;
        _profileSwitch.rxResult(rc, true);
        return rc;
      }
      // Keep preamble detection visible to CAD and route header errors to DIO1
      // so the wrapper can reset the RX path before another packet is read.
      const int16_t rc = LR1110::startReceive(
          RADIOLIB_LR11X0_RX_TIMEOUT_INF,
          RADIOLIB_IRQ_RX_DEFAULT_FLAGS | (1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED),
          RADIOLIB_IRQ_RX_DEFAULT_MASK | (1UL << RADIOLIB_IRQ_HEADER_ERR), 0);
      _profileSwitch.rxResult(rc, true);
      if (rc == RADIOLIB_ERR_NONE) _profilePacketDirty = false;
      return rc;
    }

    // Query airtime from the acknowledged LoRa software tuple without SPI.
    RadioLibTime_t getTimeOnAir(size_t len) override {
      return getToA(len, ModemType_t::RADIOLIB_MODEM_LORA);
    }

    bool isReceiving() {
      if (isChipBusy()) return false;
      uint32_t irq = getIrqStatus();
      bool preamble = irq & RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED;      // bit 4
      bool header   = irq & RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID; // bit 5
      bool hdrErr   = irq & RADIOLIB_LR11X0_IRQ_HEADER_ERR;             // bit 6
      uint32_t now  = millis();
      if (hdrErr) {
        clearIrqState(RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED | RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID | RADIOLIB_LR11X0_IRQ_HEADER_ERR);
        _activityAt = 0;
        _headerSeen = false;
        return false;
      }
      if (!header && _headerSeen) {
        // something cleared the header flag, reset our state.
        _activityAt = 0; _headerSeen = false;
        return false;
      }
      if (header) {
        if (!_headerSeen) { _headerSeen = true; _activityAt = now; };
        if (now - _activityAt > _maxPayloadMillis) {
          MESH_DEBUG_PRINTLN("Clearing header IRQ after %ums", _maxPayloadMillis);
          clearIrqState(RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED | RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID | RADIOLIB_LR11X0_IRQ_HEADER_ERR);
          _activityAt = 0; _headerSeen = false;
          return false;
        }
        return true;
      }
      if (preamble) {
        if (_activityAt == 0) _activityAt = now;
        if (now - _activityAt > _preambleMillis) {
          clearIrqState(RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED);
          _activityAt = 0;
          MESH_DEBUG_PRINTLN("Clearing preamble IRQ after %ums", _preambleMillis);

          return false;
        }
        return true;
      }
      _activityAt = 0; _headerSeen = false;
      return false;
    }
    
    void setPreambleMillis(uint32_t preambleMillis) {
      _preambleMillis = preambleMillis;
      MESH_DEBUG_PRINTLN("Set _preambleMillis=%u", _preambleMillis);
    }
    void setMaxPayloadMillis(uint32_t payloadMillis) {
      _maxPayloadMillis = payloadMillis;
      MESH_DEBUG_PRINTLN("Set _maxPayloadMillis=%u", _maxPayloadMillis);
    }

    uint8_t getSpreadingFactor() const { return spreadingFactor; }
};
