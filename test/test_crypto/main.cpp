#if defined(NATIVE_PLATFORM)

#include <unity.h>
#include <stdio.h>
#include <stdlib.h>
#include "test_utils.h"
#include "test_main_common.h"

int main() {
  UNITY_BEGIN();

  printf("\n");
  printf("╔════════════════════════════════════════════════════╗\n");
  printf("║   MeshCore Crypto Test Suite                       ║\n");
  printf("╚════════════════════════════════════════════════════╝\n");
  printf("\n");
 
  run_all_tests();

  return UNITY_END();
}

#endif

