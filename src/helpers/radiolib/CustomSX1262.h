#pragma once

#include <RadioLib.h>
#include "MeshCore.h"
#include <math.h>
#include "SX1262ProfileSwitchState.h"

#ifndef MC_SX1262_FAST_PROFILE_SWITCH
// CustomSX1262 targets use direct radio I/O unless a board opts out. Keep
// the owned RX-to-RX path available across those boards, including roles that
// did not previously need a local profile scanner.
#define MC_SX1262_FAST_PROFILE_SWITCH 1
#endif

#ifndef MC_SX1262_FAST_FS
#define MC_SX1262_FAST_FS MC_SX1262_FAST_PROFILE_SWITCH
#endif

#ifndef MC_SX1262_FAST_PACKET_CACHE
#define MC_SX1262_FAST_PACKET_CACHE MC_SX1262_FAST_PROFILE_SWITCH
#endif

#ifndef MC_SX1262_FAST_IQ_CACHE
// An additional optimization, enabled per board only after hardware testing.
#define MC_SX1262_FAST_IQ_CACHE 0
#endif

#ifndef MC_SX1262_FAST_MODEM_CACHE
// Reuse the LoRa modem confirmation only inside an acknowledged owned RX hop.
// Keep this board-qualified optimization off until its hardware comparison.
#define MC_SX1262_FAST_MODEM_CACHE 0
#endif

#if (MC_SX1262_FAST_FS || MC_SX1262_FAST_PACKET_CACHE || MC_SX1262_FAST_IQ_CACHE || MC_SX1262_FAST_MODEM_CACHE) && !MC_SX1262_FAST_PROFILE_SWITCH
#error "SX1262 SetFs/packet/modem reuse requires the owned fast profile-switch path"
#endif



class CustomSX1262 : public SX1262 {
  uint32_t _preambleMillis = 66;
  uint32_t _maxPayloadMillis = 3934;
  uint32_t _activityAt = 0;
  bool _headerSeen = false;

  SX1262ProfileSwitchState _profileSwitch{MC_SX1262_FAST_PROFILE_SWITCH != 0};
  uint32_t _profileFastResumes = 0;
  bool _coldStandby = true;
  bool _tcxoWakePending = false;

  public:
    CustomSX1262(Module *mod) : SX1262(mod) { }

    using SX1262::begin;
    using SX1262::beginFSK;
    using SX1262::beginLRFHSS;
    void invalidateProfileCache() {
      _profileSwitch.invalidate();
      _coldStandby = true;
      _tcxoWakePending = false;
    }

    int16_t begin(const ConfigLoRa_t& config) override {
      invalidateProfileCache();
      return SX1262::begin(config);
    }

    int16_t beginFSK(const ConfigFSK_t& config) override {
      invalidateProfileCache();
      return SX1262::beginFSK(config);
    }

    int16_t beginLRFHSS(const ConfigLRFHSS_t& config) override {
      invalidateProfileCache();
      return SX1262::beginLRFHSS(config);
    }

    int16_t setModem(ModemType_t modem) override {
      invalidateProfileCache();
      return SX1262::setModem(modem);
    }

    void setProfileSwitchOptimization(bool enabled) {
      _profileSwitch.enabled = enabled;
      _profileSwitch.invalidate();
    }

    void beginProfileSwitch(bool continuousRx) { _profileSwitch.begin(continuousRx, standbyXOSC); }

    void endProfileSwitch(bool success) { _profileSwitch.end(success); }

    bool profileSwitchFailed() const { return _profileSwitch.failed(); }

    uint32_t getOptimizedProfileSwitches() const { return _profileFastResumes; }

    int16_t restoreTcxoAfterSleep() {
      if (!_tcxoWakePending) return RADIOLIB_ERR_NONE;
      // Both RC and warm policies showed a wake-only XOSC error on the WIO
      // module. Restore the existing DIO3 voltage/delay, never disable TCXO.
      // RadioLib clears XOSC_START_ERR here; do not hide unrelated faults.
      if (getDeviceErrors() & ~RADIOLIB_SX126X_XOSC_START_ERR) return RADIOLIB_ERR_SPI_CMD_FAILED;
      _tcxoWakePending = false; // setTCXO calls our explicit standby override
      const int16_t rc = SX1262::setTCXO(tcxoVoltage, tcxoDelay);
      if (rc != RADIOLIB_ERR_NONE) _tcxoWakePending = true;
      return rc;
    }

    int16_t standby() override {
      const bool alreadyAwake = !_coldStandby && _profileSwitch.active && _profileSwitch.rxValid;
      if (!alreadyAwake) _profileSwitch.invalidate();
      int16_t rc;
      bool useFs = MC_SX1262_FAST_FS != 0;
      if (alreadyAwake && useFs) {
        getMod()->setRfSwitchState(Module::MODE_IDLE);
        rc = setFs();
      } else {
        rc = alreadyAwake
            ? SX1262::standby(RADIOLIB_SX126X_STANDBY_XOSC, false)
            : _coldStandby ? SX1262::standby(RADIOLIB_SX126X_STANDBY_RC) : SX1262::standby();
      }
      if (rc == RADIOLIB_ERR_NONE) rc = restoreTcxoAfterSleep();
      if (rc != RADIOLIB_ERR_NONE) _coldStandby = true;
      _profileSwitch.standbyResult(rc);
      return rc;
    }

    int16_t standby(uint8_t mode) override {
      _profileSwitch.invalidate();
      int16_t rc = SX1262::standby(mode);
      if (rc == RADIOLIB_ERR_NONE) rc = restoreTcxoAfterSleep();
      if (mode == RADIOLIB_SX126X_STANDBY_RC || rc != RADIOLIB_ERR_NONE) _coldStandby = true;
      _profileSwitch.standbyResult(rc);
      return rc;
    }

    int16_t sleep() override { return sleep(true); }

    int16_t sleep(bool retainConfig) {
      _profileSwitch.invalidate();
      _coldStandby = true;
      const int16_t rc = SX1262::standby(RADIOLIB_SX126X_STANDBY_RC);
      _tcxoWakePending = tcxoVoltage > 0.0f;
      return rc == RADIOLIB_ERR_NONE ? SX1262::sleep(retainConfig) : rc;
    }

    int16_t reset(bool verify = true) { _profileSwitch.invalidate(); _coldStandby = true; _tcxoWakePending = false; return SX1262::reset(verify); }

    int16_t stageMode(RadioModeType_t mode, RadioModeConfig_t* cfg) override {
      // All ordinary RX/TX staging rebuilds hardware state. In particular TX
      // changes IRQ mapping, packet length and buffer contents/base settings.
      _profileSwitch.invalidate();
      return SX1262::stageMode(mode, cfg);
    }

    int16_t setPreambleLength(size_t symbols) override {
      if (_profileSwitch.canResumeFast()) {
        // The owned hop will commit the complete packet tuple once, in RX
        // resume. A failed hop discards this capability before rollback.
        preambleLengthLoRa = symbols;
        return RADIOLIB_ERR_NONE;
      }
      _profileSwitch.invalidatePacket();
      return SX1262::setPreambleLength(symbols);
    }

    int16_t setCRC(uint8_t len, uint16_t initial = 0x1D0F,
                   uint16_t polynomial = 0x1021, bool inverted = true) {
      _profileSwitch.invalidatePacket();
      return SX1262::setCRC(len, initial, polynomial, inverted);
    }

    int16_t implicitHeader(size_t len) {
      _profileSwitch.invalidatePacket();
      return SX1262::implicitHeader(len);
    }

    int16_t explicitHeader() {
      _profileSwitch.invalidatePacket();
      return SX1262::explicitHeader();
    }

    int16_t invertIQ(bool enable) override {
      _profileSwitch.invalidatePacket();
      return SX1262::invertIQ(enable);
    }

    int16_t setBandwidth(float bw) override {
      _profileSwitch.invalidateModulation();
      return SX1262::setBandwidth(bw);
    }

    int16_t setSpreadingFactor(uint8_t sf) override {
      _profileSwitch.invalidateModulation();
      return SX1262::setSpreadingFactor(sf);
    }

    int16_t setCodingRate(uint8_t cr, bool longInterleave = false) {
      _profileSwitch.invalidateModulation();
      return SX1262::setCodingRate(cr, longInterleave);
    }

    int16_t forceLDRO(bool enabled) {
      _profileSwitch.invalidateModulation();
      return SX1262::forceLDRO(enabled);
    }

    int16_t setLoRaModulationParams(float bw, uint8_t sf, uint8_t cr) {
      if (sf < 5 || sf > 12) return RADIOLIB_ERR_INVALID_SPREADING_FACTOR;
      if (cr < 4 || cr > 8) return RADIOLIB_ERR_INVALID_CODING_RATE;
      if (!isfinite(bw)) return RADIOLIB_ERR_INVALID_BANDWIDTH;
      static constexpr float bandwidths[] = {7.8f, 10.4f, 15.6f, 20.8f, 31.25f,
                                             41.7f, 62.5f, 125.0f, 250.0f, 500.0f};
      static constexpr uint8_t codes[] = {
        RADIOLIB_SX126X_LORA_BW_7_8, RADIOLIB_SX126X_LORA_BW_10_4,
        RADIOLIB_SX126X_LORA_BW_15_6, RADIOLIB_SX126X_LORA_BW_20_8,
        RADIOLIB_SX126X_LORA_BW_31_25, RADIOLIB_SX126X_LORA_BW_41_7,
        RADIOLIB_SX126X_LORA_BW_62_5, RADIOLIB_SX126X_LORA_BW_125_0,
        RADIOLIB_SX126X_LORA_BW_250_0, RADIOLIB_SX126X_LORA_BW_500_0};
      size_t index = 0;
      while (index < sizeof(bandwidths) / sizeof(bandwidths[0])
          && fabsf(bw - bandwidths[index]) >= 0.01f) ++index;
      if (index == sizeof(bandwidths) / sizeof(bandwidths[0])) {
        return RADIOLIB_ERR_INVALID_BANDWIDTH;
      }
      bool queryModem = true;
#if MC_SX1262_FAST_MODEM_CACHE
      bool reuseModem = MC_SX1262_FAST_MODEM_CACHE != 0;
      // rxValid is established only after full RX setup confirms LoRa, or by
      // resuming that same owned context. A successful owned standby, warm
      // oscillator and continuous RX are required. TX/CAD staging, ordinary
      // standby, sleep, reset, duty cycling and command errors revoke it.
      // A caller using base-qualified GODMODE/raw SPI to change packet type
      // must revoke ownership first, just as for the packet/IQ caches.
      queryModem = !reuseModem || !_profileSwitch.canResumeFast();
#endif
      if (queryModem && getPacketType() != RADIOLIB_SX126X_PACKET_TYPE_LORA) {
        _profileSwitch.invalidate();
        return RADIOLIB_ERR_WRONG_MODEM;
      }

      // Match the effective on-wire LDRO value, not merely the auto/manual
      // policy. autoLDRO() changes policy without writing the chip; SF/BW can
      // change its computed value. Keep the pinned RadioLib >=16 ms rule.
      const uint8_t effectiveLdro = ldroAuto
          ? ((float(uint32_t(1) << sf) / bw) >= 16.0f ? 1 : 0)
          : ldrOptimize;
      const bool unchanged = _profileSwitch.matchesModulation(
          sf, codes[index], cr - 4, effectiveLdro);
      const auto oldSf = spreadingFactor;
      const auto oldBw = bandwidth;
      const auto oldBwKhz = bandwidthKhz;
      const auto oldCr = codingRate;
      const auto oldLdro = ldrOptimize;
      spreadingFactor = sf;
      bandwidth = codes[index];
      bandwidthKhz = bw;
      codingRate = cr - 4;  // ordinary LoRa CR, not long-interleaving encoding
      if (unchanged) {
        ldrOptimize = effectiveLdro;
        return RADIOLIB_ERR_NONE;  // no SetModulationParams (0x8B) needed
      }
      const int16_t state = setModulationParams(sf, bandwidth, codingRate, ldrOptimize);
      _profileSwitch.modulationResult(state, sf, bandwidth, codingRate, ldrOptimize);
      if (state != RADIOLIB_ERR_NONE) {
        // The wrapper restores the physical tuple after a failed command.
        // Do not publish a new software tuple when hardware success is unknown.
        spreadingFactor = oldSf;
        bandwidth = oldBw;
        bandwidthKhz = oldBwKhz;
        codingRate = oldCr;
        ldrOptimize = oldLdro;
      }
      return state;
    }

    bool isChipBusy() {
      uint32_t busy = this->mod->getGpio();
      return busy != RADIOLIB_NC && this->mod->hal->digitalRead(busy);
    }

  #ifdef RP2040_PLATFORM
    bool std_init(SPIClassRP2040* spi = NULL)
  #else
    bool std_init(SPIClass* spi = NULL)
  #endif
    {
  #ifdef SX126X_DIO3_TCXO_VOLTAGE
      float tcxo = SX126X_DIO3_TCXO_VOLTAGE;
  #else
      float tcxo = 1.6f;
  #endif

  #ifdef LORA_CR
      uint8_t cr = LORA_CR;
  #else
      uint8_t cr = 5;
  #endif

  #ifdef SX126X_USE_REGULATOR_LDO
      constexpr bool useRegulatorLDO = SX126X_USE_REGULATOR_LDO;
  #else
      constexpr bool useRegulatorLDO = false;
  #endif

      MESH_DEBUG_PRINTLN("SX1262 regulator requested: %s", useRegulatorLDO ? "LDO" : "DC-DC");

  #if defined(P_LORA_SCLK)
    #ifdef NRF52_PLATFORM
      if (spi) { spi->setPins(P_LORA_MISO, P_LORA_SCLK, P_LORA_MOSI); spi->begin(); }
    #elif defined(RP2040_PLATFORM)
      if (spi) {
        spi->setMISO(P_LORA_MISO);
        //spi->setCS(P_LORA_NSS); // Setting CS results in freeze
        spi->setSCK(P_LORA_SCLK);
        spi->setMOSI(P_LORA_MOSI);
        spi->begin();
      }
    #else
      if (spi) spi->begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
    #endif
  #endif
      int status = begin(LORA_FREQ, LORA_BW, LORA_SF, cr, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, LORA_TX_POWER, 16, tcxo, useRegulatorLDO);
      // if radio init fails with -707/-706, try again with tcxo voltage set to 0.0f
      if (status == RADIOLIB_ERR_SPI_CMD_FAILED || status == RADIOLIB_ERR_SPI_CMD_INVALID) {
        MESH_DEBUG_PRINTLN("SX1262 init failed with error %d, retrying with TCXO at 0.0V", status);
        tcxo = 0.0f;
        status = begin(LORA_FREQ, LORA_BW, LORA_SF, cr, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, LORA_TX_POWER, 16, tcxo, useRegulatorLDO);
      }
      if (status != RADIOLIB_ERR_NONE) {
        Serial.print("ERROR: radio init failed: ");
        Serial.println(status);
        return false;  // fail
      }
    
      setCRC(1);
  
  #ifdef SX126X_CURRENT_LIMIT
      setCurrentLimit(SX126X_CURRENT_LIMIT);
  #endif
  #ifdef SX126X_DIO2_AS_RF_SWITCH
      setDio2AsRfSwitch(SX126X_DIO2_AS_RF_SWITCH);
  #endif
  #ifdef SX126X_RX_BOOSTED_GAIN
      setRxBoostedGainMode(SX126X_RX_BOOSTED_GAIN);
  #endif
  #if defined(SX126X_RXEN) || defined(SX126X_TXEN)
    #ifndef SX126X_RXEN
      #define SX126X_RXEN RADIOLIB_NC
    #endif
    #ifndef SX126X_TXEN
      #define SX126X_TXEN RADIOLIB_NC
    #endif
      setRfSwitchPins(SX126X_RXEN, SX126X_TXEN);
  #endif 

  // for improved RX with Heltec v4
  #ifdef SX126X_REGISTER_PATCH
    uint8_t r_data = 0;
    readRegister(0x8B5, &r_data, 1);
    r_data |= 0x01;
    writeRegister(0x8B5, &r_data, 1);
  #endif

      MESH_DEBUG_PRINTLN("SX1262 status=0x%02X device_errors=0x%04X", getStatus(), getDeviceErrors());

      return true;  // success
    }

    #if MC_SX1262_FAST_IQ_CACHE
    // Firmware-level direct register/packet writes are not an acknowledged
    // owned RX tuple, even when they fail or restore the same software IQ.
    int16_t writeRegister(uint16_t address, const uint8_t* data, uint8_t size) {
      _profileSwitch.invalidatePacket();
      return SX1262::writeRegister(address, data, size);
    }
    int16_t setPacketParams(uint16_t preamble, uint8_t crc, uint8_t payload,
                            uint8_t header, uint8_t iq) {
      _profileSwitch.invalidatePacket();
      return SX1262::setPacketParams(preamble, crc, payload, header, iq);
    }
  private:
    int16_t writeProfilePacketParams(bool reuseIq) {
      if (!reuseIq) {
        return setPacketParams(preambleLengthLoRa, crcTypeLoRa, implicitLen,
                               headerType, invertIQEnabled);
      }
      // Only the acknowledged IQ workaround is retained. A changed preamble
      // still needs the COMPLETE ordinary SetPacketParams command. Preserve
      // its on-wire field order and Module BUSY/status/error handling.
      const uint8_t data[] = {
          static_cast<uint8_t>(preambleLengthLoRa >> 8),
          static_cast<uint8_t>(preambleLengthLoRa), headerType,
          static_cast<uint8_t>(implicitLen),
          crcTypeLoRa, invertIQEnabled};
      return getMod()->SPIwriteStream(RADIOLIB_SX126X_CMD_SET_PACKET_PARAMS,
                                     data, sizeof(data));
    }
  public:
#endif


    int16_t startReceive() override {
      if (_profileSwitch.canResumeFast()) {
        // Existing continuous RX already established buffer bases and the
        // MeshCore IRQ map (including preamble detection). No TX/CAD/sleep/
        // reset/staging may intervene without revoking that context.
        bool skipPacket = MC_SX1262_FAST_PACKET_CACHE != 0;
        skipPacket = skipPacket && _profileSwitch.matchesPacket(
            preambleLengthLoRa, crcTypeLoRa, implicitLen,
            headerType, invertIQEnabled);
#if MC_SX1262_FAST_IQ_CACHE
        bool reuseIq = MC_SX1262_FAST_IQ_CACHE != 0;
        // Capture this capability BEFORE revoking rxValid for the command
        // sequence. Packet/IQ setters, protocol changes, sleep, reset, TX/CAD,
        // duty cycling and command failures revoke the acknowledged tuple.
        reuseIq = reuseIq && _profileSwitch.matchesPacketIq(invertIQEnabled);
#endif
        _profileSwitch.rxValid = false;  // fail closed on any command error
        int16_t rc = clearIrqStatus();
        if (rc == RADIOLIB_ERR_NONE && !skipPacket) {
#if MC_SX1262_FAST_IQ_CACHE
          rc = writeProfilePacketParams(reuseIq);
#else
          rc = setPacketParams(preambleLengthLoRa, crcTypeLoRa, implicitLen,
                               headerType, invertIQEnabled);
#endif
          _profileSwitch.packetResult(rc, preambleLengthLoRa, crcTypeLoRa,
                                      implicitLen, headerType, invertIQEnabled);
        }
        if (rc == RADIOLIB_ERR_NONE) {
          rxTimeout = RADIOLIB_SX126X_RX_TIMEOUT_INF;
          getMod()->setRfSwitchState(Module::MODE_RX);
          rc = setRx(rxTimeout);  // original BUSY waits and status checks
        }
        stagedMode = RADIOLIB_RADIO_MODE_NONE;
        if (rc == RADIOLIB_ERR_NONE) ++_profileFastResumes;
        _coldStandby = rc != RADIOLIB_ERR_NONE;
        _profileSwitch.rxResult(rc, true);
        return rc;
      }
      // Make preamble detection visible to CAD while retaining RadioLib's
      // normal RX-complete and error events.
      const int16_t rc = SX1262::startReceive(
          RADIOLIB_SX126X_RX_TIMEOUT_INF,
          RADIOLIB_IRQ_RX_DEFAULT_FLAGS | (1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED),
          RADIOLIB_IRQ_RX_DEFAULT_MASK, 0);
      const bool reusable = rc == RADIOLIB_ERR_NONE && _profileSwitch.enabled
          && getPacketType() == RADIOLIB_SX126X_PACKET_TYPE_LORA;
      _coldStandby = rc != RADIOLIB_ERR_NONE;
      _profileSwitch.rxResult(rc, reusable);
      if (rc == RADIOLIB_ERR_NONE && reusable) {
        _profileSwitch.packetResult(rc, preambleLengthLoRa, crcTypeLoRa,
                                    implicitLen, headerType, invertIQEnabled);
      }
      return rc;
    }

    // Query airtime from the acknowledged LoRa software tuple without SPI.
    RadioLibTime_t getTimeOnAir(size_t len) override {
      uint8_t cr = this->codingRate;
      // RadioLib stores ordinary CR 4/5 through 4/8 as 0-4. Long-interleaving
      // CR values 0-4 and 5-7 map to the same time-on-air denominators.
      if (cr < 5) {
        cr += 4;
      } else if (cr == 7) {
        cr += 1;
      }

      DataRate_t data_rate = {};
      data_rate.lora.spreadingFactor = this->spreadingFactor;
      data_rate.lora.bandwidth = this->bandwidthKhz;
      data_rate.lora.codingRate = cr;

      PacketConfig_t packet_config = {};
      packet_config.lora.preambleLength = this->preambleLengthLoRa;
      packet_config.lora.crcEnabled = (bool)this->crcTypeLoRa;
      packet_config.lora.implicitHeader =
          this->headerType == RADIOLIB_SX126X_LORA_HEADER_IMPLICIT;
      packet_config.lora.ldrOptimize = (bool)this->ldrOptimize;

      return SX126x::calculateTimeOnAir(
          ModemType_t::RADIOLIB_MODEM_LORA, data_rate, packet_config, len);
    }

    bool isReceiving() {
      if (isChipBusy()) return false;
      uint32_t irq = getIrqFlags();
      bool preamble = irq & RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED; // bit 2
      bool header   = irq & RADIOLIB_SX126X_IRQ_HEADER_VALID;      // bit 4
      bool hdrErr   = irq & RADIOLIB_SX126X_IRQ_HEADER_ERR;        // bit 5
      uint32_t now  = millis();
      if (hdrErr) {
        clearIrqFlags(RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED | RADIOLIB_SX126X_IRQ_HEADER_VALID | RADIOLIB_SX126X_IRQ_HEADER_ERR | RADIOLIB_SX126X_IRQ_SYNC_WORD_VALID);
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
          clearIrqFlags(RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED | RADIOLIB_SX126X_IRQ_HEADER_VALID | RADIOLIB_SX126X_IRQ_HEADER_ERR | RADIOLIB_SX126X_IRQ_SYNC_WORD_VALID);
          _activityAt = 0; _headerSeen = false;
          return false;
        }
        return true;
      }
      if (preamble) {
        if (_activityAt == 0) _activityAt = now;
        if (now - _activityAt > _preambleMillis) {
          clearIrqFlags(RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED);
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

    bool getRxBoostedGainMode() {
      uint8_t rxGain = 0;
      readRegister(RADIOLIB_SX126X_REG_RX_GAIN, &rxGain, 1);
      return (rxGain == RADIOLIB_SX126X_RX_GAIN_BOOSTED);
    }
};