cmake_minimum_required(VERSION 3.16)
project(motatool LANGUAGES CXX)

# Self-contained MeshCore OTA host tool: build / verify / inspect / serve `.mota` containers.
# Portable C++17 — builds on Ubuntu, Raspberry Pi, and any arch with a C++17 compiler + OpenSSL.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)

# OpenSSL libcrypto provides SHA-256 + Ed25519 (sign/verify/keygen). On Debian/Ubuntu/Pi:
#   sudo apt install libssl-dev
find_package(OpenSSL REQUIRED)

# Core logic (everything except the CLI front-end) — shared by the tool and the test runner.
add_library(motatool_core STATIC
  src/mota.cpp
  src/crypto.cpp
  src/input.cpp
  src/serve.cpp
)
# `src` for the tool's own headers; the repo's helpers/ota for the shared, generated OtaTargets.h
# (target_id -> env-name table — single source of truth with the firmware, no hand-maintained copy).
target_include_directories(motatool_core PUBLIC
  src
  "${CMAKE_CURRENT_SOURCE_DIR}/../../src/helpers/ota"
)
target_link_libraries(motatool_core PUBLIC OpenSSL::Crypto)

add_executable(motatool src/main.cpp)
target_link_libraries(motatool PRIVATE motatool_core)
install(TARGETS motatool RUNTIME DESTINATION bin)

# Unit tests (no external test framework — a tiny built-in harness keeps the project self-contained).
# Run with:  cmake --build build && ctest --test-dir build --output-on-failure
#       or:  ./build/motatool_tests
enable_testing()
add_executable(motatool_tests tests/test_motatool.cpp)
target_link_libraries(motatool_tests PRIVATE motatool_core)
add_test(NAME motatool_tests COMMAND motatool_tests)
