# Step 4e — full MeshCore companion (companion_radio MyMesh) on the Zephyr seams.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mc_zephyr_companion)

get_filename_component(MC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(CR       "${MC_ROOT}/examples/companion_radio")
set(ALFS     "${MC_ROOT}/arch/stm32/Adafruit_LittleFS_stm32/src")
set(RADIOLIB "$ENV{HOME}/Arduino/libraries/RadioLib/src")
set(CRYPTO   "$ENV{HOME}/Arduino/libraries/Crypto/src")
set(CAYENNE  "$ENV{HOME}/Arduino/libraries/CayenneLPP/src")
set(BASE64   "$ENV{HOME}/Arduino/libraries/base64/src")
set(RTCLIB   "$ENV{HOME}/Arduino/libraries/RTClib/src")
set(AJSON    "$ENV{HOME}/Arduino/libraries/ArduinoJson/src")
set(ED25519  "${MC_ROOT}/lib/ed25519")

# MeshCore core + helper set (mirrors the Arduino companion curation)
file(GLOB MC_CORE ${MC_ROOT}/src/*.cpp)
set(MC_HELPERS
  ${MC_ROOT}/src/helpers/BaseChatMesh.cpp
  ${MC_ROOT}/src/helpers/TransportKeyStore.cpp
  ${MC_ROOT}/src/helpers/IdentityStore.cpp
  ${MC_ROOT}/src/helpers/RegionMap.cpp
  ${MC_ROOT}/src/helpers/ClientACL.cpp
  ${MC_ROOT}/src/helpers/CommonCLI.cpp
  ${MC_ROOT}/src/helpers/AdvertDataHelpers.cpp
  ${MC_ROOT}/src/helpers/TxtDataHelpers.cpp
  ${MC_ROOT}/src/helpers/StaticPoolPacketManager.cpp
  ${MC_ROOT}/src/helpers/radiolib/RadioLibWrappers.cpp)

# companion firmware
set(MC_APP ${CR}/MyMesh.cpp ${CR}/DataStore.cpp)

# third-party (generic/non-Arduino mode for RadioLib)
file(GLOB_RECURSE RL_SOURCES ${RADIOLIB}/*.cpp)
list(FILTER RL_SOURCES EXCLUDE REGEX "/hal/Arduino/")
file(GLOB CRYPTO_SRC ${CRYPTO}/*.cpp ${CRYPTO}/*.c)
file(GLOB BASE64_SRC ${BASE64}/*.cpp)
# CayenneLPP + RTClib are shimmed in compat/ (header-only) — real libs pull STL/I2C.
file(GLOB ED_SRC ${ED25519}/*.c)
set(ALFS_SRC ${ALFS}/Adafruit_LittleFS.cpp ${ALFS}/Adafruit_LittleFS_File.cpp
             ${ALFS}/littlefs/lfs.c ${ALFS}/littlefs/lfs_util.c)

target_sources(app PRIVATE
  src/main.cpp src/target.cpp src/zephyr_internal_fs.cpp src/serial_ble_interface.cpp
  ${MC_CORE} ${MC_HELPERS} ${MC_APP}
  ${RL_SOURCES} ${CRYPTO_SRC} ${BASE64_SRC} ${ED_SRC} ${ALFS_SRC})

target_include_directories(app PRIVATE
  compat src ${MC_ROOT}/src ${CR} ${ALFS}
  ${RADIOLIB} ${CRYPTO} ${BASE64} ${ED25519})

target_compile_definitions(app PRIVATE
  NRF54_PLATFORM=1 RADIOLIB_GODMODE=1 MAX_GROUP_CHANNELS=8 MAX_CONTACTS=100 BLE_PIN_CODE=123456
  LFS_NO_ASSERT   # mount-fail (e.g. stale/repartitioned region) -> graceful format, not abort()
  # App TX-power slider max + firmware input check. Pin to the LR2021's absolute (sub-GHz LF)
  # PA ceiling of 22 dBm, INDEPENDENT of the band/boot default, so a 2.4 GHz build still allows
  # 22 once tuned back to sub-GHz. The real per-band limit (HF caps at +12) is enforced at the
  # radio layer by clamp_tx_for_band() in target.cpp. (Otherwise MAX would follow LORA_TX_POWER,
  # which MC_BAND_2G4 sets to 12 -> the app would be stuck at 12 even on 869 MHz.)
  MAX_LORA_TX_POWER=22)

# 2.4 GHz band: build with `-DMC_BAND_2G4=ON` (e.g. west build -- -DMC_BAND_2G4=ON) to boot on
# the LR2021's 2.4 GHz preset (target.h: 2450 MHz / 500 kHz / SF8 / CR5, TX clamped to +12 dBm)
# instead of the default 865 MHz sub-GHz. The app has no 2.4 GHz preset, so this is how to put
# the node on 2.4 GHz for the over-the-air RF test (the choice then persists in prefs).
option(MC_BAND_2G4 "Boot on the 2.4 GHz LoRa preset instead of sub-GHz" OFF)
# LoRa boot params must be global -D flags (mirroring platformio.ini), not header
# defines: core headers (e.g. RadioLibWrappers.h getSpreadingFactor) expand LORA_SF
# in translation units that never include target.h. Keep in sync with the presets
# documented in src/target.h.
if(MC_BAND_2G4)
  target_compile_definitions(app PRIVATE MC_BAND_2G4=1
    LORA_FREQ=2450.0f LORA_BW=500.0f LORA_SF=8 LORA_CR=5 LORA_TX_POWER=12)
  message(STATUS "MeshCore: building for 2.4 GHz band (2450 MHz / 500 kHz / SF8 / CR5)")
else()
  target_compile_definitions(app PRIVATE
    LORA_FREQ=869.618f LORA_BW=62.5f LORA_SF=8 LORA_CR=5 LORA_TX_POWER=22)
endif()
