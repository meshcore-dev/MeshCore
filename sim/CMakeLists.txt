cmake_minimum_required(VERSION 3.16)
project(MeshCoreSim C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -------------------------------------------------------------------------
# Stub out Arduino-specific includes so MeshCore compiles on the host.
# -------------------------------------------------------------------------
add_library(arduino_stub INTERFACE)
target_include_directories(arduino_stub INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stubs)

# -------------------------------------------------------------------------
# MeshCore core sources (no radio, no board, no Arduino peripherals)
# -------------------------------------------------------------------------
set(MESHCORE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_library(meshcore_core STATIC
  ${MESHCORE_ROOT}/src/Dispatcher.cpp
  ${MESHCORE_ROOT}/src/Mesh.cpp
  ${MESHCORE_ROOT}/src/Packet.cpp
  ${MESHCORE_ROOT}/src/Identity.cpp
  ${MESHCORE_ROOT}/src/Utils.cpp
  ${MESHCORE_ROOT}/src/helpers/unishox2.cpp
  ${MESHCORE_ROOT}/src/helpers/Compression.cpp
)

target_include_directories(meshcore_core PUBLIC
  ${MESHCORE_ROOT}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)

target_link_libraries(meshcore_core PUBLIC arduino_stub)

# Disable Arduino-specific debug macros
target_compile_definitions(meshcore_core PUBLIC
  MESH_DEBUG=0
  MESH_PACKET_LOGGING=0
  BRIDGE_DEBUG=0
  NDEBUG
  ENABLE_COMPRESSION=1
)

# Force-include the Arduino stub so size_t and other types are available
# in MeshCore.h before any other headers are processed.
target_compile_options(meshcore_core PUBLIC
  -include ${CMAKE_CURRENT_SOURCE_DIR}/stubs/Arduino.h
)

# -------------------------------------------------------------------------
# Ed25519 library (Nightcracker, needed for keypair/sign/verify)
# -------------------------------------------------------------------------
file(GLOB ED25519_SRCS ${MESHCORE_ROOT}/lib/ed25519/*.c)
add_library(ed25519 STATIC ${ED25519_SRCS})
target_include_directories(ed25519 PUBLIC ${MESHCORE_ROOT}/lib/ed25519)
target_link_libraries(meshcore_core PUBLIC ed25519)

# -------------------------------------------------------------------------
# rweather Crypto library (AES, SHA256, Ed25519 — Arduino-compatible)
# -------------------------------------------------------------------------
set(CRYPTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/arduinolibs/libraries/Crypto)
file(GLOB CRYPTO_SRCS
  ${CRYPTO_DIR}/AES128.cpp
  ${CRYPTO_DIR}/AESCommon.cpp
  ${CRYPTO_DIR}/BlockCipher.cpp
  ${CRYPTO_DIR}/Cipher.cpp
  ${CRYPTO_DIR}/Crypto.cpp
  ${CRYPTO_DIR}/Ed25519.cpp
  ${CRYPTO_DIR}/SHA256.cpp
  ${CRYPTO_DIR}/SHA512.cpp
  ${CRYPTO_DIR}/Hash.cpp
  ${CRYPTO_DIR}/HMAC.cpp
  ${CRYPTO_DIR}/Curve25519.cpp
  ${CRYPTO_DIR}/BigNumberUtil.cpp
  ${CRYPTO_DIR}/AuthenticatedCipher.cpp
)
add_library(crypto STATIC ${CRYPTO_SRCS} ${CMAKE_CURRENT_SOURCE_DIR}/stubs/RNG.cpp)
target_include_directories(crypto PUBLIC ${CRYPTO_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/stubs)
target_compile_definitions(crypto PUBLIC ARDUINO=100)
target_link_libraries(meshcore_core PUBLIC crypto)

# -------------------------------------------------------------------------
# Simulation harness headers
# -------------------------------------------------------------------------
add_library(meshcore_sim INTERFACE)
target_include_directories(meshcore_sim INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(meshcore_sim INTERFACE meshcore_core)

# -------------------------------------------------------------------------
# Scenarios — each scenario builds as its own binary
# -------------------------------------------------------------------------
file(GLOB SCENARIO_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/scenarios/*.cpp)
foreach(scenario_src ${SCENARIO_SRCS})
  get_filename_component(scenario_name ${scenario_src} NAME_WE)
  add_executable(${scenario_name} ${scenario_src})
  target_link_libraries(${scenario_name} PRIVATE meshcore_sim crypto ed25519)
  target_include_directories(${scenario_name} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  )
endforeach()
