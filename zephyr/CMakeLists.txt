# MeshCore Zephyr module build glue.
#
# Only does anything when CONFIG_MESHCORE=y. Builds the mesh protocol core, the
# crypto it depends on, the Arduino compatibility shim and the Zephyr platform
# bindings, and forwards the LoRa RF parameters as compile definitions so the
# same sources compile that the PlatformIO build uses.

if(CONFIG_MESHCORE)

  set(MC_ROOT ${ZEPHYR_CURRENT_MODULE_DIR})

  # An interface library, following the esp-serial-flasher module pattern, so
  # the include paths and RF compile definitions below reach application code
  # as well as the module's own translation units. A plain zephyr_library would
  # keep them private to this library, which is why LORA_* never reached the app.
  zephyr_interface_library_named(meshcore)
  zephyr_library_named(meshcore_core)
  zephyr_library_link_libraries(meshcore)

  # Warning relaxations, scoped to THIS library only -- application code keeps
  # Zephyr's full warning set. Both the MeshCore core and the vendored crypto are
  # upstream-maintained sources built with `-w` under PlatformIO, and we do not
  # want to diverge from them to satisfy a warning flag. Twister builds with
  # -Werror, so these have to be explicit.
  #   sign-compare:     src/Dispatcher.cpp:94 compares a `long` airtime against
  #                     an `unsigned long` budget. Correct for non-negative
  #                     elapsed time, but worth raising upstream rather than
  #                     patching here.
  #   unused-variable / unused-function:
  #                     MESH_DEBUG_PRINTLN and friends (src/MeshCore.h:25-37)
  #                     expand to {} unless MESH_DEBUG && ARDUINO, leaving their
  #                     operands unread -- e.g. src/Mesh.cpp:302.
  zephyr_library_compile_options(
    -Wno-sign-compare
    -Wno-unused-variable
    -Wno-unused-function
  )

  # ---- Public include paths ------------------------------------------------
  # Mirrors the PlatformIO build's "-I src -I src/helpers" plus the Zephyr port.
  zephyr_include_directories(
    ${MC_ROOT}/src
    ${MC_ROOT}/src/helpers
  )

  # ---- Core mesh engine (protocol, dispatcher, identity, packets) ----------
  # Listed explicitly rather than globbed: a GLOB does not re-run when files are
  # added, and Zephyr's own guidance is to enumerate sources.
  zephyr_library_sources(
    ${MC_ROOT}/src/Dispatcher.cpp
    ${MC_ROOT}/src/Identity.cpp
    ${MC_ROOT}/src/Mesh.cpp
    ${MC_ROOT}/src/Packet.cpp
    ${MC_ROOT}/src/Utils.cpp
    ${MC_ROOT}/src/helpers/StaticPoolPacketManager.cpp
  )

  # ---- Crypto --------------------------------------------------------------
  # src/Utils.cpp, src/Packet.cpp and src/Identity.cpp need AES-128, SHA-256 and
  # Ed25519 verification. These come from the vendored rweather/Crypto subset --
  # the same implementation the PlatformIO build links -- so that a Zephyr node
  # and a PlatformIO node compute identical hashes and ciphertext. See
  # arch/zephyr/Crypto/README.md before changing anything here.
  zephyr_include_directories(${MC_ROOT}/arch/zephyr/Crypto)
  zephyr_library_sources(
    ${MC_ROOT}/arch/zephyr/Crypto/AES128.cpp
    ${MC_ROOT}/arch/zephyr/Crypto/AESCommon.cpp
    ${MC_ROOT}/arch/zephyr/Crypto/BigNumberUtil.cpp
    ${MC_ROOT}/arch/zephyr/Crypto/BlockCipher.cpp
    ${MC_ROOT}/arch/zephyr/Crypto/Crypto.cpp
    ${MC_ROOT}/arch/zephyr/Crypto/Curve25519.cpp
    ${MC_ROOT}/arch/zephyr/Crypto/Ed25519.cpp
    ${MC_ROOT}/arch/zephyr/Crypto/Hash.cpp
    ${MC_ROOT}/arch/zephyr/Crypto/RNG.cpp
    ${MC_ROOT}/arch/zephyr/Crypto/SHA256.cpp
    ${MC_ROOT}/arch/zephyr/Crypto/SHA512.cpp
  )

  # Ed25519 signing and key generation use the separately vendored lib/ed25519
  # (rweather's Ed25519 is used only for verify -- see src/Identity.cpp:35-38).
  # seed.c is omitted: it is entirely #ifndef ED25519_NO_SEED, and would
  # otherwise want /dev/urandom.
  zephyr_include_directories(${MC_ROOT}/lib/ed25519)
  zephyr_library_sources(
    ${MC_ROOT}/lib/ed25519/add_scalar.c
    ${MC_ROOT}/lib/ed25519/fe.c
    ${MC_ROOT}/lib/ed25519/ge.c
    ${MC_ROOT}/lib/ed25519/key_exchange.c
    ${MC_ROOT}/lib/ed25519/keypair.c
    ${MC_ROOT}/lib/ed25519/sc.c
    ${MC_ROOT}/lib/ed25519/sha512.c
    ${MC_ROOT}/lib/ed25519/sign.c
    ${MC_ROOT}/lib/ed25519/verify.c
  )
  zephyr_compile_definitions(ED25519_NO_SEED=1)

  # MeshCore's house platform macro, alongside NRF52_PLATFORM / ESP32 /
  # RP2040_PLATFORM / STM32_PLATFORM. Global rather than library-scoped because
  # application code includes headers that branch on it (IdentityStore.h).
  zephyr_compile_definitions(ZEPHYR_PLATFORM=1)

  # ---- Arduino compatibility shim (Zephyr-backed) --------------------------
  if(CONFIG_MESHCORE_ARDUINO_COMPAT)
    zephyr_include_directories(${MC_ROOT}/ports/zephyr/include)
    zephyr_library_sources(${MC_ROOT}/ports/zephyr/arduino_compat.cpp)
  endif()

  # ---- Zephyr platform helpers (clocks, RNG, board) ------------------------
  if(CONFIG_MESHCORE_ZEPHYR_HELPERS)
    zephyr_library_sources(${MC_ROOT}/ports/zephyr/MeshCoreZephyr.cpp)
  endif()

  # ---- Persistent storage (identity, prefs) --------------------------------
  # MeshCore selects its filesystem type by platform macro rather than through an
  # interface (src/helpers/IdentityStore.h), so the shim below has to be visible
  # wherever that header is included -- including application code.
  if(CONFIG_MESHCORE_STORAGE)
    zephyr_library_sources(
      ${MC_ROOT}/ports/zephyr/ZephyrFS.cpp
      ${MC_ROOT}/src/helpers/IdentityStore.cpp
    )
    # zephyr/fs/littlefs.h pulls in lfs.h, whose include path Zephyr exposes only
    # through the LITTLEFS interface target (zephyr/modules/littlefs/CMakeLists.txt)
    # rather than globally.
    zephyr_library_link_libraries(LITTLEFS)
  endif()

  # ---- LoRa RF parameters --> compile definitions --------------------------
  # Global (not library-scoped) so application code that builds a radio or reads
  # the RF settings sees the same values the library was compiled with.
  # Kconfig string values may arrive quoted; strip quotes so the value lands as
  # a numeric literal (e.g. 869.618) rather than a string in the C++ TUs.
  string(REPLACE "\"" "" MC_LORA_FREQ "${CONFIG_MESHCORE_LORA_FREQ}")
  string(REPLACE "\"" "" MC_LORA_BW   "${CONFIG_MESHCORE_LORA_BW}")
  zephyr_compile_definitions(
    LORA_FREQ=${MC_LORA_FREQ}
    LORA_BW=${MC_LORA_BW}
    LORA_SF=${CONFIG_MESHCORE_LORA_SF}
    LORA_CR=${CONFIG_MESHCORE_LORA_CR}
    LORA_TX_POWER=${CONFIG_MESHCORE_LORA_TX_POWER}
  )

endif()
