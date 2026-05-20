#ifndef SPI_H
#define SPI_H

typedef int BitOrder;


typedef enum {
  SPI_MODE0 = 0,
  SPI_MODE1 = 1,
  SPI_MODE2 = 2,
  SPI_MODE3 = 3,
} SPIMode;

class SPISettings {
public:
  SPISettings(uint32_t clock, BitOrder bitOrder, SPIMode dataMode) {}
  SPISettings(uint32_t clock, BitOrder bitOrder, uint8_t dataMode) {}
};

class SPIClass
{
public:
	uint8_t transfer(uint8_t data) { return 0; }
	uint16_t transfer16(uint16_t data) { return 0; }
	void transfer(void *buf, size_t count) {}

	void transfer(const void *txbuf, void *rxbuf, size_t count) {}

	void usingInterrupt(int interruptNumber) {}
	void notUsingInterrupt(int interruptNumber) {}
	void beginTransaction(SPISettings settings) {}
	void endTransaction(void) {}

	void attachInterrupt() {}
	void detachInterrupt() {}

	void begin() {}
	void end() {}
};

SPIClass SPI;


#endif
