# Compiles lolren's nrf54-arduino-core with its platform.txt flags, as arduino-cli resolves them.
import os

from SCons.Script import DefaultEnvironment

env = DefaultEnvironment()
board = env.BoardConfig()

ROOT = env["NRF54_CORE_DIR"]  # fetched by builder/main.py
CORE = os.path.join(ROOT, "cores", board.get("build.core"))
VARIANT = os.path.join(ROOT, "variants", board.get("build.variant"))
SDC = os.path.join(
    ROOT, "libraries", "Nrf54L15-Clean-Implementation", "third_party", "nordic_sdc", "lib",
    board.get("build.sdc_arch"),
)

cpu = ["-mcpu=%s" % board.get("build.cpu"), "-mthumb", "-mfloat-abi=soft"]
forced = [
    "-include", os.path.join(CORE, "CoreVersionGenerated.h"),
    "-include", os.path.join(CORE, "BuildTargetGuard.h"),
]

env.Append(
    ASPPFLAGS=cpu + ["-g", "-x", "assembler-with-cpp"] + forced,
    CCFLAGS=cpu + ["-g", "-Os", "-ffunction-sections", "-fdata-sections", "-Wall"] + forced,
    CFLAGS=["-std=gnu11"],
    CXXFLAGS=[
        "-std=gnu++17", "-fpermissive", "-fno-exceptions", "-fno-rtti",
        "-fno-threadsafe-statics", "-fno-use-cxa-atexit", "-fno-sized-deallocation",
    ],
    CPPDEFINES=[
        ("F_CPU", "$BOARD_F_CPU"),
        ("ARDUINO", 10607),
        "ARDUINO_%s" % board.get("build.board"),
        "ARDUINO_ARCH_NRF54L15CLEAN",
    ],
    CPPPATH=[CORE, VARIANT],
    LINKFLAGS=cpu + [
        "-Wl,--gc-sections",
        "-T", os.path.join(env.PioPlatform().get_dir(), "ldscripts", board.get("build.arduino.ldscript")),
        "--specs=nano.specs", "--specs=nosys.specs",
    ],
    LIBSOURCE_DIRS=[os.path.join(ROOT, "libraries")],
)
env.ProcessFlags(board.get("build.extra_flags", ""))

env.Prepend(
    LIBS=[
        env.BuildLibrary(os.path.join("$BUILD_DIR", "FrameworkArduinoVariant"), VARIANT),
        env.BuildLibrary(os.path.join("$BUILD_DIR", "FrameworkArduino"), CORE),
    ]
)
env.Append(
    LIBS=[
        env.File(os.path.join(SDC, lib))
        for lib in ("libsoftdevice_controller_multirole.a", "libmpsl_fem_common.a", "libmpsl.a")
    ]
    + ["m"]
)
# The core and Nordic's libraries call into each other, as in platform.txt's link group.
env.Prepend(_LIBFLAGS="-Wl,--start-group ")
env.Append(_LIBFLAGS=" -Wl,--end-group")
