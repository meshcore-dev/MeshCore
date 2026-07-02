#include "target.h"
#include "zephyr_radiolib_hal.h"
#include <helpers/radiolib/RadioLibWrappers.h>
#include <RadioLib.h>
#include <zephyr/sys/reboot.h>

#define LR2021_IRQ_DIO 8
#ifndef LR2021_RX_BOOST_LEVEL
#define LR2021_RX_BOOST_LEVEL 7   /* matches Semtech usp_zephyr rx-boost-cfg / CustomLR2021 */
#endif

static ZephyrHal hal;

class LR2021Z : public LR2021 {
  public:
	explicit LR2021Z(Module *m) : LR2021(m) {}
	void setIrqDio(uint8_t n) { this->irqDioNum = n; }
	/* Match the known-good bare-metal CustomLR2021: on a corrupted LoRa header the
	 * stock driver returns len 0 and can leave RX half-wedged; force standby so the
	 * wrapper re-arms cleanly. */
	size_t getPacketLength(bool update) override {
		size_t len = LR2021::getPacketLength(update);
		if (len == 0 && (getIrqFlags() & RADIOLIB_LR2021_IRQ_LORA_HDR_CRC_ERROR)) {
			standby();
		}
		return len;
	}

	int16_t startReceive() override {
		setLoRaPacketParams(this->preambleLengthLoRa, this->headerType,
				    RADIOLIB_LR2021_MAX_PACKET_LENGTH, this->crcTypeLoRa,
				    this->invertIQEnabled);
		return LR2021::startReceive();
	}
};

static Module s_mod(&hal, LR_PIN_NSS, LR_PIN_DIO1, LR_PIN_RESET, LR_PIN_BUSY);
static LR2021Z s_lora(&s_mod);

ZBoard board;   /* defined before the radio wrapper that references it */

class LR2021Mesh : public RadioLibWrapper {
  public:
	LR2021Mesh(LR2021 &r, mesh::MainBoard &b) : RadioLibWrapper(r, b) {}

	bool isReceivingPacket() override {
		uint32_t irq = ((LR2021 *)_radio)->getIrqFlags();
		return (irq & RADIOLIB_LR2021_IRQ_PREAMBLE_DETECTED) ||
		       (irq & RADIOLIB_LR2021_IRQ_LORA_HEADER_VALID);
	}
	float getCurrentRSSI() override { return ((LR2021 *)_radio)->getRSSI(); }
};
static LR2021Mesh s_radio(s_lora, board);

RadioLibWrapper &radio_driver = s_radio;
VolatileRTCClock rtc_clock;
SensorManager    sensors;

mesh::LocalIdentity radio_new_identity()
{
	RadioNoiseListener rng(s_lora);
	return mesh::LocalIdentity(&rng);   /* new identity from LoRa RSSI noise */
}

void ZBoard::reboot() { sys_reboot(SYS_REBOOT_COLD); }

#define MC_HF_CUTOFF_MHZ  1500.0f

static float  s_freq    = LORA_FREQ;
static int8_t s_req_dbm = LORA_TX_POWER;   /* last app-requested power, pre-clamp */

static int8_t clamp_tx_for_band(float freq, int8_t dbm)
{
	if (freq > MC_HF_CUTOFF_MHZ)                       /* 2.4 GHz HF PA: -19..+12 */
		return dbm > 12 ? 12 : (dbm < -19 ? -19 : dbm);
	return dbm > 22 ? 22 : (dbm < -9 ? -9 : dbm);     /* sub-GHz LF PA: -9..+22 */
}


static bool radio_bringup(float freq, float bw, uint8_t sf, uint8_t cr)
{
	for (int attempt = 0; attempt < 8; attempt++) {

		int16_t st = s_lora.begin(freq, bw, sf, cr,
					  RADIOLIB_LR2021_LORA_SYNC_WORD_PRIVATE,
					  clamp_tx_for_band(freq, s_req_dbm), 16, /*tcxoVoltage=*/0.0f);
		if (st == RADIOLIB_ERR_NONE) {
			/* match CustomLR2021's proven config: explicit header, CRC, RX boosted gain */
			s_lora.explicitHeader();
			s_lora.setCRC(2);
			s_lora.setRxBoostedGainMode(LR2021_RX_BOOST_LEVEL);
			int16_t rx = s_lora.startReceive();      /* the operation that fails on a bad boot */
			if (rx == RADIOLIB_ERR_NONE) {
				s_lora.standby();                /* idle; the wrapper arms RX in its loop */
				s_freq = freq;
				if (attempt) printk("radio: RX ok after %d retr%s\n",
						    attempt, attempt == 1 ? "y" : "ies");
				return true;
			}
			printk("radio: begin ok but RX arm failed (%d), resetting\n", rx);
		} else {
			printk("radio: begin failed (%d), resetting\n", st);
		}
		s_lora.reset();        /* full chip reset, then retry the whole bring-up */
		k_msleep(50);
	}
	return false;
}

bool radio_init()
{
	s_lora.setIrqDio(LR2021_IRQ_DIO);
	s_freq = LORA_FREQ;
	s_req_dbm = LORA_TX_POWER;
	if (!radio_bringup(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR)) return false;
	s_radio.begin();
	return true;
}

uint32_t radio_get_rng_seed() { return s_lora.random(0x7FFFFFFF); }

void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr)
{

	bool band_changed = (freq > MC_HF_CUTOFF_MHZ) != (s_freq > MC_HF_CUTOFF_MHZ);
	s_lora.standby();
	int16_t st = s_lora.setFrequency(freq);     /* recalibrates the front-end for the band */
	if (st == RADIOLIB_ERR_NONE) st = s_lora.setBandwidth(bw);
	if (st == RADIOLIB_ERR_NONE) st = s_lora.setSpreadingFactor(sf);
	if (st == RADIOLIB_ERR_NONE) st = s_lora.setCodingRate(cr);
	s_freq = freq;
	if (band_changed) s_lora.setOutputPower(clamp_tx_for_band(freq, s_req_dbm));
	s_radio.begin();   /* reset wrapper -> dispatcher re-arms RX (startReceive) on the new config */
	if (st != RADIOLIB_ERR_NONE) {
		printk("radio_set_params: apply error %d for %u kHz bw=%u kHz sf%u cr%u\n",
		       st, (unsigned)(freq * 1000.0f), (unsigned)(bw * 1000.0f), sf, cr);
	} else {
		printk("radio_set_params: now on %u kHz bw=%u kHz sf%u cr%u\n",
		       (unsigned)(freq * 1000.0f), (unsigned)(bw * 1000.0f), sf, cr);
	}
}

void radio_set_tx_power(int8_t dbm)
{
	s_req_dbm = dbm;
	s_lora.setOutputPower(clamp_tx_for_band(s_freq, dbm));
}
