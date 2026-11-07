#include <cassert>
#include <iostream>
#include <helpers/radiolib/RadioLibWrappers.h>

class TestRadio : public RadioLibWrapper {
public:
  TestRadio(PhysicalLayer& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }
  bool isReceivingPacket() override { return false; }
  void setParams(float, float, uint8_t, uint8_t) override { }
  float getCurrentRSSI() override { return -100; }
  void restartReceive() { startRecv(); }
};

static void activeTransmission(bool interruptPending) {
  PhysicalLayer device;
  mesh::MainBoard board;
  TestRadio radio(device, board);
  uint8_t payload = 42;
  radio.begin();
  radio.setCADEnabled(true);
  radio.restartReceive();
  assert(device.receiveCalls == 1);
  assert(radio.startSendRaw(&payload, 1));
  if (interruptPending) device.irq();

  // Polling RX must not consume TxDone as an incoming packet or restart RX.
  assert(radio.recvRaw(&payload, 1) == 0);
  assert(device.readCalls == 0 && device.receiveCalls == 1);
  radio.resetAGC();
  assert(device.sleepCalls == 0);
  radio.restartReceive();
  assert(device.receiveCalls == 1);
  assert(radio.isChannelActive());
  assert(device.scanCalls == 0);
  assert(!radio.startSendRaw(&payload, 1));
  assert(device.transmitCalls == 1 && board.before == 1 && board.after == 0);

  if (!interruptPending) {
    assert(!radio.isSendComplete());
    device.irq();
  }
  assert(radio.isSendComplete());
  assert(radio.getPacketsSent() == 1);
  radio.onSendFinished();
  assert(device.finishCalls == 1 && board.after == 1);

  // Normal RX, CAD and the next transmission still work once TX is finalized.
  radio.restartReceive();
  assert(device.receiveCalls == 2);
  device.irq();
  assert(radio.recvRaw(&payload, 1) == 1);
  assert(device.readCalls == 1 && radio.getPacketsRecv() == 1);
  assert(!radio.isChannelActive());
  assert(device.scanCalls == 1);
  assert(radio.startSendRaw(&payload, 1));
  assert(device.transmitCalls == 2 && board.before == 2);
}

int main() {
  activeTransmission(false);
  activeTransmission(true);
  std::cout << "RadioLib TX-state regressions passed\n";
}
