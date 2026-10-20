#include "SimBus.h"
#include <cstdio>

// Scenario: N nodes in a full-mesh (all hear all), one node floods a packet,
// measure delivery rate and airtime across different network sizes.

static void runFullMesh(int num_nodes, int num_floods) {
  sim::SimBus bus;
  sim::FullMeshModel model(8.0f);
  bus.channel_model = &model;
  bus.tick_ms = 5;

  char name[32];
  for (int i = 0; i < num_nodes; i++) {
    snprintf(name, sizeof(name), "node%d", i);
    bus.addNode(name, (uint32_t)(i + 1) * 0xdeadbeef);
  }

  // Warm up: let nodes boot / advert
  bus.run(2000);
  bus.resetStats();

  // Inject floods
  for (int i = 0; i < num_floods; i++) {
    bus.sendFloodText(0, "hello mesh");
    bus.run(3000);   // 3s per flood to propagate
  }

  char label[64];
  snprintf(label, sizeof(label), "FullMesh N=%d floods=%d", num_nodes, num_floods);
  bus.printReport(label);
  bus.metrics.printReport(label, num_floods);
}

static void runChain(int num_nodes, int num_floods) {
  sim::SimBus bus;
  sim::ChainModel model(8.0f);
  bus.channel_model = &model;
  bus.tick_ms = 5;

  char name[32];
  for (int i = 0; i < num_nodes; i++) {
    snprintf(name, sizeof(name), "node%d", i);
    bus.addNode(name, (uint32_t)(i + 1) * 0xcafebabe);
  }

  bus.run(2000);
  bus.resetStats();

  for (int i = 0; i < num_floods; i++) {
    bus.sendFloodText(0, "hello chain");
    bus.run(5000);   // chains need more time
  }

  char label[64];
  snprintf(label, sizeof(label), "Chain N=%d floods=%d", num_nodes, num_floods);
  bus.printReport(label);
  bus.metrics.printReport(label, num_floods);
}

static void runGrid(int rows, int cols, int num_floods) {
  sim::SimBus bus;
  sim::PositionalModel model(1.5f, 12.0f, 3.0f);
  bus.channel_model = &model;
  bus.tick_ms = 5;

  int num_nodes = rows * cols;
  char name[32];
  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      snprintf(name, sizeof(name), "n%d_%d", r, c);
      bus.addNode(name, (uint32_t)(r * cols + c + 1) * 0x1337cafe);
      model.addNode((float)c, (float)r);
    }
  }

  bus.run(2000);
  bus.resetStats();

  for (int i = 0; i < num_floods; i++) {
    bus.sendFloodText(0, "hello grid");
    bus.run(8000);
  }

  char label[64];
  snprintf(label, sizeof(label), "Grid %dx%d floods=%d", rows, cols, num_floods);
  bus.printReport(label);
  bus.metrics.printReport(label, num_floods);
}

int main() {
  printf("MeshCore Simulation Harness\n");
  printf("===========================\n\n");

  // Scale test: full mesh at increasing sizes
  runFullMesh(5,  10);
  runFullMesh(10, 10);
  runFullMesh(20, 5);

  // Topology tests
  runChain(5,  10);
  runChain(10, 5);

  // Grid topology (realistic urban/suburban deployment)
  runGrid(3, 3, 10);
  runGrid(4, 4, 5);

  return 0;
}
