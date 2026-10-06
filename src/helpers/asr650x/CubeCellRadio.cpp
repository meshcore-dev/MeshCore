#include "CubeCellRadio.h"
#include "RadioParams.h"
#include "TxGate.h"
#include "LoRaWan_102.h"        // LoraWan102 library headers only: ::Radio, RadioEvents_t, MODEM_LORA (LoRaWan_APP.h would also link the LoRa/AT/display libraries)
extern "C" {                    // these board headers have no C++ guards; the functions are C
#include "gpio.h"               // GpioSetInterrupt / GpioRemoveInterrupt / GpioRead
#include "sx126x-board.h"       // extern SX126x (DIO1 pin)
}
#include <string.h>

extern "C" {
void RadioOnDioIrq(void);                       // radio.c: DIO1 handler (sets IrqFired and runs RadioIrqProcess)
unsigned char CyEnterCriticalSection(void);     // CyLib.h
void CyExitCriticalSection(unsigned char saved);
}

static RadioEvents_t g_events;
static CubeCellRadio* g_self = 0;

static void cb_rx_done(uint8_t* p, uint16_t n, int16_t rssi, int8_t snr) { if (g_self) g_self->handleRxDone(p, n, rssi, snr); }
static void cb_rx_err() { if (g_self) g_self->handleRxError(); }
static void cb_tx_done() { if (g_self) g_self->handleTxDone(false); }
static void cb_tx_timeout() { if (g_self) g_self->handleTxDone(true); }

// While main-context code talks to the SX1262 over SPI the DIO1 handler must not run (it would issue its own SPI
// commands in the middle of ours). The handler is detached for the duration; on release it is re-attached and, if
// DIO1 is already high (an edge arrived meanwhile and was not latched), serviced right away from this context.
struct RadioLock {
  RadioLock() { GpioRemoveInterrupt(&SX126x.DIO1); }
  ~RadioLock() {
    GpioSetInterrupt(&SX126x.DIO1, RISING, IRQ_HIGH_PRIORITY, RadioOnDioIrq);
    if (GpioRead(&SX126x.DIO1)) RadioOnDioIrq();
  }
};

struct CriticalSection {
  unsigned char saved;
  CriticalSection() : saved(CyEnterCriticalSection()) {}
  ~CriticalSection() { CyExitCriticalSection(saved); }
};

CubeCellRadio::CubeCellRadio()
  : _state(ST_IDLE), _rx_ready(false), _tx_done(false), _tx_timed_out(false), _rx_len(0), _last_rssi(0), _last_snr(0),
    _freq(920.25f), _bw(62.5f), _sf(8), _cr(5), _pwr(14), _boost(true), _inited(false), _noise_floor(0),
    _last_nf_sample(0), _n_recv(0), _n_sent(0), _n_err(0), _n_dropped(0), _n_txto(0), _n_blocked(0) {}

void CubeCellRadio::init() {
  if (_inited) return;
  g_self = this;
  g_events.RxDone = cb_rx_done;
  g_events.RxError = cb_rx_err;
  g_events.RxTimeout = cb_rx_err;
  g_events.TxDone = cb_tx_done;
  g_events.TxTimeout = cb_tx_timeout;
  ::Radio.Init(&g_events);
  _inited = true;
}

uint32_t CubeCellRadio::random32() {
  init();
  return ::Radio.Random();
}

uint8_t asr650x_boot_pool[4];               // copy of the seed fold (watch builds print it to compare boots)

uint32_t CubeCellRadio::getRngSeed() {
  uint32_t fold = 0;
  for (int i = 0; i < 8; i++) {
    uint32_t v = random32();
    fold ^= (v << i) | (v >> (32 - i));
  }
  memcpy(asr650x_boot_pool, &fold, sizeof(asr650x_boot_pool));
  return fold;
}

static uint32_t freq_hz(float mhz) { return ((uint32_t)(mhz * 1000.0f + 0.5f)) * 1000UL; }

void CubeCellRadio::applyRx() {
  ::Radio.SetPublicNetwork(false);                                  // private sync word 0x1424 (MeshCore)
  ::Radio.SetChannel(freq_hz(_freq));
  ::Radio.SetRxConfig(MODEM_LORA, asr650x::bw_index(_bw), asr650x::sf_clamp(_sf), asr650x::cr_index(_cr), 0,
                      asr650x::preamble_for_sf(asr650x::sf_clamp(_sf)), 0, false, 0, true, 0, 0, false, true);
}

void CubeCellRadio::applyTx(uint32_t timeout_ms) {
  ::Radio.SetPublicNetwork(false);
  ::Radio.SetChannel(freq_hz(_freq));
  ::Radio.SetTxConfig(MODEM_LORA, _pwr, 0, asr650x::bw_index(_bw), asr650x::sf_clamp(_sf), asr650x::cr_index(_cr),
                      asr650x::preamble_for_sf(asr650x::sf_clamp(_sf)), false, true, 0, 0, false, timeout_ms);
}

void CubeCellRadio::startRx() {
  {
    RadioLock lock;
    applyRx();
    ::Radio.Rx(0);
  }
  _state = ST_RX;
}

void CubeCellRadio::begin() {
  init();
  _rx_ready = false;
  _tx_done = false;
  startRx();
}

void CubeCellRadio::loop() {
  if (GpioRead(&SX126x.DIO1)) { RadioLock service; }                // lost/late DIO1 edge: service it from here
  if (_state == ST_IDLE && !_rx_ready) startRx();
  if (_state == ST_RX && !_rx_ready) {                              // slow noise-floor estimate while idle in RX
    uint32_t now = millis();
    if (now - _last_nf_sample >= 100) {
      _last_nf_sample = now;
      int16_t r;
      {
        RadioLock lock;
        r = ::Radio.Rssi(MODEM_LORA);
      }
      if (_noise_floor == 0) _noise_floor = r;
      else if (r < _noise_floor + 14) _noise_floor = (int16_t)((_noise_floor * 15 + r) / 16);
    }
  }
}

int CubeCellRadio::recvRaw(uint8_t* bytes, int sz) {
  int len = 0;
  if (_rx_ready) {
    CriticalSection cs;                                             // the callback (interrupt) may overwrite the buffer
    len = _rx_len;
    if (len > sz) len = sz;
    if (len < 0) len = 0;
    memcpy(bytes, _rx_buf, len);
    _rx_ready = false;
    if (len > 0) _n_recv++;
    _state = ST_IDLE;                                              // restart RX below
  }
  if (_state == ST_IDLE) startRx();
  return len;
}

uint32_t CubeCellRadio::getEstAirtimeFor(int len_bytes) {
  return asr650x::airtime_ms(len_bytes, _bw, _sf, _cr, asr650x::preamble_for_sf(asr650x::sf_clamp(_sf)));
}

float CubeCellRadio::packetScore(float snr, int packet_len) { return asr650x::packet_score(snr, _sf, packet_len); }

bool CubeCellRadio::startSendRaw(const uint8_t* bytes, int len) {
  if (!asr650x_tx_allowed()) { _n_blocked++; return false; }           // never transmit as the unprovisioned placeholder
  if (len <= 0 || len > 255) return false;
  uint32_t timeout = getEstAirtimeFor(len) * 2 + 500;             // the driver's timer must outlast the real air time
  _tx_done = false;
  _tx_timed_out = false;
  _state = ST_TX;
  {
    RadioLock lock;
    applyTx(timeout);
    ::Radio.Send((uint8_t*)bytes, (uint8_t)len);
  }
  return true;
}

bool CubeCellRadio::isSendComplete() {
  if (_state == ST_TX && _tx_done) {
    _tx_done = false;
    if (!_tx_timed_out) _n_sent++;                                 // a timed-out send is not a completed send
    return true;
  }
  return false;
}

void CubeCellRadio::onSendFinished() { _state = ST_IDLE; }          // loop()/recvRaw() restart RX

void CubeCellRadio::setParams(float freq_mhz, float bw_khz, uint8_t sf, uint8_t cr) {
  _freq = freq_mhz;
  _bw = asr650x::bw_value(asr650x::bw_index(bw_khz));                         // the bandwidth the chip really uses
  _sf = asr650x::sf_clamp(sf);
  _cr = (uint8_t)(asr650x::cr_index(cr) + 4);
  if (_inited && _state == ST_RX) _state = ST_IDLE;                 // re-apply on the next loop()
}

void CubeCellRadio::setTxPower(int8_t dbm) { _pwr = dbm < -9 ? -9 : (dbm > 22 ? 22 : dbm); }

void CubeCellRadio::handleRxDone(uint8_t* payload, uint16_t size, int16_t rssi, int8_t snr) {
  if (_state == ST_TX) return;                                      // not expected while transmitting
  if (_rx_ready) { _n_dropped++; return; }                          // previous packet not consumed yet: drop the new one, counted
  if (size > sizeof(_rx_buf)) size = sizeof(_rx_buf);
  memcpy(_rx_buf, payload, size);
  _rx_len = size;
  _last_rssi = rssi;
  _last_snr = snr;
  _rx_ready = true;
  _state = ST_IDLE;
}

void CubeCellRadio::handleRxError() {
  if (_state == ST_TX) return;
  _n_err++;
  _state = ST_IDLE;
}

void CubeCellRadio::handleTxDone(bool timeout) {
  if (timeout) { _n_txto++; _tx_timed_out = true; }
  _tx_done = true;                                                  // a timed-out send still ends the TX state
}
