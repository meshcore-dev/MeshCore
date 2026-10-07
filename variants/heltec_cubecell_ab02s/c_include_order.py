# The CubeCell framework's LoRaWAN library (which provides the SX1262 driver used here) includes "aes.h" from C files.
# On case-insensitive filesystems that name also matches rweather/Crypto's C++ "AES.h". Put the framework's C header
# first for C sources only (CFLAGS are not passed to the C++ compiler, so MeshCore keeps Crypto's AES.h).
Import("env")
import os

fw = env.PioPlatform().get_package_dir("framework-arduinocubecell")
env.Append(CFLAGS=["-I" + os.path.join(fw, "cores", "asr650x", "lora", "system", "crypto")])
