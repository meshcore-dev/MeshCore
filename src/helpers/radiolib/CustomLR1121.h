#pragma once

#include <RadioLib.h>


#ifndef LR11X0_DIO3_TCXO_VOLTAGE
#define LR11X0_DIO3_TCXO_VOLTAGE 3.0
#endif

#ifndef LORA_CR
#define LORA_CR 5
#endif

class CustomLR1121 : public LR1121 {
private:
	bool _rx_boosted = false;

public:
	CustomLR1121(Module* mod) : LR1121(mod) {}

#ifdef RP2040_PLATFORM
	bool std_init(SPIClassRP2040* spi = NULL)
#else
	bool std_init(SPIClass* spi = NULL)
#endif
	{
		float tcxo = LR11X0_DIO3_TCXO_VOLTAGE;
		uint8_t cr = LORA_CR;

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
		int status = begin(LORA_FREQ, LORA_BW, LORA_SF, cr, RADIOLIB_LR11X0_LORA_SYNC_WORD_PRIVATE, 10, 16, tcxo);
		if (status != RADIOLIB_ERR_NONE) {
			Serial.print("ERROR: radio init failed: ");
			Serial.println(status);
			return false;  // fail
		}

		// Waveshare Core1121 (LR1121-HF) RFSwitch Table
		static const uint32_t rfswitch_dio_pins[] = {
			RADIOLIB_LR11X0_DIO5,
			RADIOLIB_LR11X0_DIO6,
			RADIOLIB_NC, // DIO7 NC
			RADIOLIB_NC, // DIO8 NC
			RADIOLIB_NC  // DIO9 NC
		};

		static const Module::RfSwitchMode_t rfswitch_table[] = {
			{ LR11x0::MODE_STBY,     { LOW,  LOW  } },
			{ LR11x0::MODE_RX,       { HIGH, LOW  } },
			{ LR11x0::MODE_TX,       { LOW,  HIGH } },
			{ LR11x0::MODE_TX_HP,    { LOW,  HIGH } },
			{ LR11x0::MODE_TX_HF,    { LOW,  LOW  } },
			{ LR11x0::MODE_GNSS,     { LOW,  LOW  } },
			{ LR11x0::MODE_WIFI,     { LOW,  LOW  } },
			END_OF_MODE_TABLE
		};

		setRfSwitchTable(rfswitch_dio_pins, rfswitch_table);

		setCRC(2);
		explicitHeader();

		return true;  // success
	}

	size_t getPacketLength(bool update) override {
		size_t len = LR1121::getPacketLength(update);
		if (len == 0 && getIrqStatus() & RADIOLIB_LR11X0_IRQ_HEADER_ERR) {
			// we've just received a corrupted packet
			// this may have triggered a bug causing subsequent packets to be shifted
			// call standby() to return radio to known-good state
			// recvRaw will call startReceive() to restart rx
			MESH_DEBUG_PRINTLN("LR1121: got header err, calling standby()");
			standby();
		}
		return len;
	}

	bool isReceiving() {
		uint16_t irq = getIrqFlags();
		bool detected = (irq & RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID) || (irq & RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED);
		return detected;
	}

	int16_t setRxBoostedGainMode(bool en) {
		_rx_boosted = en;
		return LR1121::setRxBoostedGainMode(en);
	}

	bool getRxBoostedGainMode() const {
		return _rx_boosted;
	}

	int16_t getSpreadingFactor() const {
		return spreadingFactor;
	}

	float getFreqMHz() const { return freqMHz; }
};