//
// MeshCore minimal Zephyr sample.
//
// Instantiates the MeshCore engine on Zephyr using the bundled platform helpers
// (clock, RNG, RTC, board) and a loopback radio, then runs the dispatcher loop.
// This proves the core builds and runs on a Zephyr target with no external LoRa
// hardware. Swap LoopbackRadio for a RadioLib-backed driver (see
// ports/zephyr/RadioLibZephyrHal.h) to talk to real hardware.
//
#include <Arduino.h>
#include <Mesh.h>
#include <MeshCoreZephyr.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/SimpleMeshTables.h>

#include <zephyr/kernel.h>

// --- A do-nothing radio: accepts sends, never receives -------------------
class LoopbackRadio : public mesh::Radio {
public:
  int recvRaw(uint8_t*, int) override { return 0; }
  uint32_t getEstAirtimeFor(int len_bytes) override { return len_bytes; /* ~1ms/byte */ }
  float packetScore(float, int) override { return 0.0f; }
  bool startSendRaw(const uint8_t*, int) override { return true; }
  bool isSendComplete() override { return true; }
  void onSendFinished() override {}
  bool isInRecvMode() const override { return true; }
};

// --- Platform bindings (Zephyr-backed) -----------------------------------
static LoopbackRadio               radio;
static mesh_zephyr::ZephyrMillisClock ms_clock;
static mesh_zephyr::ZephyrRNG       rng;
static mesh_zephyr::ZephyrRTCClock  rtc;
static StaticPoolPacketManager      pool(16);
static SimpleMeshTables             tables;

// --- The mesh engine ------------------------------------------------------
class MinimalMesh : public mesh::Mesh {
public:
  MinimalMesh()
    : mesh::Mesh(radio, ms_clock, rng, rtc, pool, tables) {}
};

static MinimalMesh the_mesh;

int main(void) {
  Serial.begin(115200);

  // Give this node a fresh random identity, then start the dispatcher.
  the_mesh.self_id = mesh::LocalIdentity(&rng);
  the_mesh.begin();

  Serial.println("MeshCore minimal sample: mesh engine up");

  for (;;) {
    the_mesh.loop();
    k_msleep(10);
  }
  return 0;
}
