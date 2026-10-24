#include "gps_test_support.h"

// Defined in EnvironmentSensorManager.cpp, which is not part of this build.
void LocationProvider::sendSentence(const char*) {}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
