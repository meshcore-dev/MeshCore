# MeshCore Zephyr module build glue.
#
# Only does anything when CONFIG_MESHCORE=y. Builds the mesh core plus the
# optional shim / helper / RadioLib layers selected in Kconfig, and forwards the
# LoRa RF parameters as compile definitions so the same source compiles that the
# PlatformIO build uses.

if(CONFIG_MESHCORE)

  set(MC_ROOT ${ZEPHYR_CURRENT_MODULE_DIR})

  zephyr_library_named(meshcore)

  # ---- Public include paths ------------------------------------------------
  zephyr_include_directories(
    ${MC_ROOT}/src
    ${MC_ROOT}/src/helpers
  )

  # ---- Core mesh engine (protocol, dispatcher, identity, packets) ----------
  file(GLOB MC_CORE_SRC ${MC_ROOT}/src/*.cpp)
  zephyr_library_sources(${MC_CORE_SRC})

  # ---- Arduino compatibility shim (Zephyr-backed) --------------------------
  if(CONFIG_MESHCORE_ARDUINO_COMPAT)
    zephyr_include_directories(${MC_ROOT}/ports/zephyr/include)
    zephyr_library_sources(${MC_ROOT}/ports/zephyr/arduino_compat.cpp)
  endif()

  # ---- Zephyr platform helpers (clocks, RNG, board) ------------------------
  if(CONFIG_MESHCORE_ZEPHYR_HELPERS)
    zephyr_library_sources(${MC_ROOT}/ports/zephyr/MeshCoreZephyr.cpp)
  endif()

  # ---- RadioLib wrappers (optional, needs the RadioLib module) -------------
  if(CONFIG_MESHCORE_RADIOLIB)
    zephyr_include_directories(${MC_ROOT}/src/helpers/radiolib)
    zephyr_library_sources(${MC_ROOT}/src/helpers/radiolib/RadioLibWrappers.cpp)

    if(CONFIG_MESHCORE_RADIO_SX1262)
      zephyr_library_compile_definitions(RADIO_CLASS=CustomSX1262 WRAPPER_CLASS=CustomSX1262Wrapper)
    elseif(CONFIG_MESHCORE_RADIO_SX1276)
      zephyr_library_compile_definitions(RADIO_CLASS=CustomSX1276 WRAPPER_CLASS=CustomSX1276Wrapper)
    elseif(CONFIG_MESHCORE_RADIO_LR1110)
      zephyr_library_compile_definitions(RADIO_CLASS=CustomLR1110 WRAPPER_CLASS=CustomLR1110Wrapper)
    elseif(CONFIG_MESHCORE_RADIO_STM32WLX)
      zephyr_library_compile_definitions(RADIO_CLASS=CustomSTM32WLx WRAPPER_CLASS=CustomSTM32WLxWrapper)
    endif()
  endif()

  # ---- LoRa RF parameters --> compile definitions --------------------------
  # Kconfig string values may arrive quoted; strip quotes so the value lands as
  # a numeric literal (e.g. 869.618) rather than a string in the C++ TUs.
  string(REPLACE "\"" "" MC_LORA_FREQ "${CONFIG_MESHCORE_LORA_FREQ}")
  string(REPLACE "\"" "" MC_LORA_BW   "${CONFIG_MESHCORE_LORA_BW}")
  zephyr_library_compile_definitions(
    LORA_FREQ=${MC_LORA_FREQ}
    LORA_BW=${MC_LORA_BW}
    LORA_SF=${CONFIG_MESHCORE_LORA_SF}
    LORA_CR=${CONFIG_MESHCORE_LORA_CR}
    LORA_TX_POWER=${CONFIG_MESHCORE_LORA_TX_POWER}
  )

  # RadioLib static-only knobs, matching platformio.ini's arduino_base flags.
  zephyr_library_compile_definitions(
    NDEBUG
    RADIOLIB_STATIC_ONLY=1
  )

endif()
