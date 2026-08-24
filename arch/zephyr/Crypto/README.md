# Vendored rweather/Crypto subset (Zephyr)

The MeshCore protocol core needs AES-128, SHA-256 and Ed25519 verification. On
PlatformIO these come from `rweather/Crypto @ ^0.4.0` (`platformio.ini`,
`[arduino_base] lib_deps`). Zephyr has no equivalent package, and MeshCore nodes
built for Zephyr must interoperate on-air with PlatformIO-built nodes, so the
same implementation is vendored here rather than substituted with PSA/mbedTLS.
Byte-identical crypto is then true by construction instead of by testing.

## Provenance

- Upstream: <https://github.com/rweather/arduinolibs>, `libraries/Crypto/`
- Version: 0.4.0 (`library.json`), commit `37a76b8f7516568e1c575b6dc9268da1ccaac6b6`
- License: MIT (same as MeshCore)

## What is here, and why

Only the dependency closure of the three primitives MeshCore actually calls:

| Files | Needed by |
|---|---|
| `Crypto.{h,cpp}`, `Hash.{h,cpp}`, `BlockCipher.{h,cpp}` | shared base classes |
| `AES.h`, `AES128.cpp`, `AESCommon.cpp` | `mesh::Utils` AES-128 (`src/Utils.cpp:71,108`) |
| `SHA256.{h,cpp}` | `mesh::Utils::sha256`, `Packet::calculateHash` (`src/Utils.cpp:30`, `src/Packet.cpp:42`) |
| `SHA512.{h,cpp}`, `Ed25519.{h,cpp}`, `Curve25519.{h,cpp}`, `BigNumberUtil.{h,cpp}` | `Ed25519::verify` (`src/Identity.cpp:38`) |
| `utility/*.h` | internal helpers; `ProgMemUtil.h` already has a portable non-AVR/non-ESP path |

Signing and key generation do *not* come from here — those use the separately
vendored `lib/ed25519` (`ed25519_sign`, `ed25519_create_keypair`,
`ed25519_key_exchange`). Only verification uses rweather's Ed25519, because
`src/Identity.cpp:35` disables `ed25519_verify` over a known memory-corruption bug.

## Local modifications

Exactly one. `RNG.{h,cpp}` are **not** upstream files — they are a Zephyr
replacement. Upstream `RNG.cpp` needs `<Arduino.h>`, AVR EEPROM and ESP-IDF NVS.
The only references to it in the vendored set are `Ed25519::generatePrivateKey()`
and `Curve25519::dh1()`, neither of which MeshCore calls, but both of which are
compiled and so need the symbol to link. The replacement implements just
`RNGClass::rand()` on top of `sys_rand_get()`.

Every other file is byte-for-byte upstream. Keep it that way: patching them
would break the interop guarantee this directory exists to provide.
