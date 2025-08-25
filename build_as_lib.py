from os.path import realpath

Import("env") # type: ignore
menv=env # type: ignore

src_filter = [
  '+<*.cpp>',
  '+<helpers/*.cpp>',
  '+<helpers/sensors>',
  '+<helpers/radiolib/*.cpp>',
  '+<helpers/ui/MomentaryButton.cpp>',
  '+<helpers/ui/buzzer.cpp>',
]

# add build and include dirs according to CPPDEFINES
for item in menv.get("CPPDEFINES", []):
 
    # STM32
    if isinstance(item, str) and item == "STM32_PLATFORM":
        menv.Append(CPPPATH=[realpath("src/helpers/stm32")])
        menv.Append(BUILD_FLAGS=["-I src/helpers/stm32"])
        src_filter.append("+<helpers/stm32/*>")
    
    # ESP32
    elif isinstance(item, str) and item == "ESP32":
        menv.Append(CPPPATH=[realpath("src/helpers/esp32")])
        menv.Append(BUILD_FLAGS=["-I src/helpers/esp32"])
        src_filter.append("+<helpers/esp32/*>")
    
    # NRF52
    elif isinstance(item, str) and item == "NRF52_PLATFORM":
        menv.Append(CPPPATH=[realpath("src/helpers/nrf52")])
        menv.Append(BUILD_FLAGS=["-I src/helpers/nrf52"])
        src_filter.append("+<helpers/nrf52/*>")

    # RP2040
    elif isinstance(item, str) and item == "RP2040_PLATFORM":
        menv.Append(CPPPATH=[realpath("src/helpers/rp2040")])
        menv.Append(BUILD_FLAGS=["-I src/helpers/rp2040"])
        src_filter.append("+<helpers/rp2040/*>")
    
    # DISPLAY HANDLING
    elif isinstance(item, tuple) and item[0] == "DISPLAY_CLASS":
        display_class = item[1]
        src_filter.append(f"+<helpers/ui/{display_class}.cpp>")
        if (display_class == "ST7789Display") :
            src_filter.append(f"+<helpers/ui/OLEDDisplay.cpp>")
            src_filter.append(f"+<helpers/ui/OLEDDisplayFonts.cpp>")

    # VARIANTS HANDLING
    elif isinstance(item, tuple) and item[0] == "MC_VARIANT":
        variant_name = item[1]
        menv.Append(BUILD_FLAGS=[f"-I variants/{variant_name}"])
        src_filter.append(f"+<../variants/{variant_name}>")
    
    # INCLUDE EXAMPLE CODE IN BUILD (to provide your own support files without touching the tree)
    elif isinstance(item, tuple) and item[0] == "BUILD_EXAMPLE":
        example_name = item[1]
        src_filter.append(f"+<../examples/{example_name}>")

    # EXCLUDE A SOURCE FILE FROM AN EXAMPLE (must be placed after example name or boom)
    elif isinstance(item, tuple) and item[0] == "EXCLUDE_FROM_EXAMPLE":
        exclude_name = item[1]
        src_filter.append(f"-<../examples/{example_name}/{exclude_name}>")

menv.Replace(SRC_FILTER=src_filter)

#print (menv.Dump())
