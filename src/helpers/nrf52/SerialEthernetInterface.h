
#include "helpers/BaseSerialInterface.h"
#include <SPI.h>
#include <RAK13800_W5100S.h>

// expects ETH_ENABLED = 1
#define TCP_PORT 5000
// define ETH_RAW_LINE=1 to use raw line-based CLI instead of framed packets

class SerialEthernetInterface : public BaseSerialInterface {
  bool deviceConnected;
  bool _isEnabled;
  unsigned long _last_write;
  unsigned long adv_restart_time;
  uint8_t _state;
  uint16_t _frame_len;
  uint16_t _rx_len;
  uint8_t _rx_buf[MAX_FRAME_SIZE];

  EthernetServer server;
  EthernetClient client;

  struct Frame {
    uint8_t len;
    uint8_t buf[MAX_FRAME_SIZE];
  };

  #define FRAME_QUEUE_SIZE 4
  int recv_queue_len;
  Frame recv_queue[FRAME_QUEUE_SIZE];
  int send_queue_len;
  Frame send_queue[FRAME_QUEUE_SIZE];

  void clearBuffers() {
    recv_queue_len = 0;
    send_queue_len = 0;
    _state = 0;
    _frame_len = 0;
    _rx_len = 0;
  }

  protected:

  public:
    SerialEthernetInterface() : server(EthernetServer(TCP_PORT)) { 
        deviceConnected = false;
        _isEnabled = false;
        _last_write = 0;
        send_queue_len = recv_queue_len = 0;
        _state = 0;
        _frame_len = 0;
        _rx_len = 0;
    }
    bool begin();

    // BaseSerialInterface methods
    void enable() override;
    void disable() override;
    bool isEnabled() const override { return _isEnabled; }

    bool isConnected() const override;
    bool isWriteBusy() const override;

    size_t writeFrame(const uint8_t src[], size_t len) override;
    size_t checkRecvFrame(uint8_t dest[]) override;

    void maintain();

private:
  void generateDeviceMac(uint8_t mac[6]);
};


#if ETH_DEBUG_LOGGING && ARDUINO
  #include <Arduino.h>
  #define ETH_DEBUG_PRINT(F, ...) Serial.printf("ETH: " F, ##__VA_ARGS__)
  #define ETH_DEBUG_PRINTLN(F, ...) Serial.printf("ETH: " F "\n", ##__VA_ARGS__)
  #define ETH_DEBUG_PRINT_IP(name, ip) Serial.printf(name ": %u.%u.%u.%u" "\n", ip[0], ip[1], ip[2], ip[3])
#else
  #define ETH_DEBUG_PRINT(...) {}
  #define ETH_DEBUG_PRINTLN(...) {}
  #define ETH_DEBUG_PRINT_IP(...) {}
#endif
