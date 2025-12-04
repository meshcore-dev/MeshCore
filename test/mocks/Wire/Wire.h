#ifndef Wire_h
#define Wire_h

#include "Stream.h"

class TwoWire : public Stream
{
public:
  TwoWire(uint8_t bus_num){};
  ~TwoWire(){};
  bool setPins(int sda, int scl){};
  bool begin(){return true;}
  bool begin(uint8_t addr){return true;}
	void beginTransmission(uint16_t address){}
	void beginTransmission(uint8_t address){}
	void beginTransmission(int address){}
	uint8_t endTransmission(bool sendStop) { return 0; }
	uint8_t endTransmission(void) { return 0; }
	size_t requestFrom(uint16_t address, size_t size, bool sendStop) { return 0; }
	uint8_t requestFrom(uint16_t address, uint8_t size, bool sendStop) { return 0; }
	uint8_t requestFrom(uint16_t address, uint8_t size, uint8_t sendStop) { return 0; }
	size_t requestFrom(uint8_t address, size_t len, bool stopBit) { return 0; }
	uint8_t requestFrom(uint16_t address, uint8_t size) { return 0; }
	uint8_t requestFrom(uint8_t address, uint8_t size, uint8_t sendStop) { return 0; }
	uint8_t requestFrom(uint8_t address, uint8_t size) { return 0; }
	uint8_t requestFrom(int address, int size, int sendStop) { return 0; }
  size_t write(uint8_t) { return 1; }
	size_t write(const uint8_t *b, size_t n) { return n; }
  int available(){ return 0; }
	int read(void) { return 0; }
	int peek(void) { return 0; }
  bool end(){};
};

extern TwoWire Wire;
extern TwoWire Wire1;

#endif

