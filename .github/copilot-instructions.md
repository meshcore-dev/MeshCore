# MeshCore AI Coding Assistant Guide

## Project Overview

MeshCore is an embedded C++ library enabling multi-hop LoRa mesh networks. The architecture layers `Dispatcher` (radio I/O) → `Mesh` (routing) → application-specific classes (companion/repeater/room server). Applications use polymorphic interface classes (`Radio`, `RTCClock`, `RNG`, `MainBoard`) with hardware-specific implementations.

## Architecture Layers

**Core abstraction stack (all in `mesh::` namespace):**
- `Dispatcher` - Radio I/O, transmission queue, CAD (Channel Activity Detection), packet manager
- `Mesh` - Multi-hop routing (flood/direct modes), encryption, peer discovery, packet types
- Application classes - Extend `Mesh` or `BaseChatMesh` for specific use cases

**Key inheritance pattern:**
```cpp
// Applications extend Mesh or BaseChatMesh
MyMesh : public BaseChatMesh : public mesh::Mesh : public mesh::Dispatcher

// Hardware abstraction via polymorphism
CustomSX1262Wrapper : public mesh::Radio
ESP32Board : public mesh::MainBoard
```

## Variant-Based Build System

**PlatformIO multi-variant architecture:**
- Each hardware variant lives in `variants/<variant_name>/`
- Variants define `platformio.ini` with multiple `[env:*]` targets (companion, repeater, room_server)
- Each variant provides `target.h`/`target.cpp` defining board-specific hardware objects
- Examples include source via `build_src_filter` (e.g., `+<../examples/companion_radio>`)

**Example structure:**
```
variants/heltec_v3/
  ├── platformio.ini        # Defines env:Heltec_v3_companion, env:Heltec_v3_repeater, etc.
  ├── target.h              # Declares radio_driver, board, rtc_clock instances
  ├── target.cpp            # Implements radio_init(), radio_new_identity()
  └── HeltecV3Board.h       # Board-specific MainBoard implementation
```

**Build flags control features:**
- `-D DISPLAY_CLASS=SSD1306Display` - Enables display support
- `-D MESH_DEBUG=1` / `-D BRIDGE_DEBUG=1` - Debug logging
- `-D LORA_FREQ=869.525 -D LORA_BW=250 -D LORA_SF=11` - Radio parameters
- `-D MAX_CONTACTS=100` - Application limits
- Platform detection: `NRF52_PLATFORM`, `ESP32`, `RP2040_PLATFORM`, `STM32_PLATFORM`

## Building & Flashing

**CLI build commands (via `build.sh`):**
```bash
sh build.sh build-firmware RAK_4631_repeater          # Single target
sh build.sh build-matching-firmwares heltec_v3        # All matching targets
sh build.sh build-companion-firmwares                 # All companion variants
```

**PlatformIO direct:**
```bash
pio run -e Heltec_v3_companion                        # Build specific environment
pio run -e Heltec_v3_repeater -t upload               # Build and upload
```

**Release workflow:**
- Push git tags: `companion-v1.0.0`, `repeater-v1.0.0`, `room-server-v1.0.0`
- GitHub Actions auto-builds firmware for all variants
- Creates draft releases (requires manual publish)

## Packet Structure & Routing

**Packet anatomy (`src/Packet.h`):**
- Header byte: `[ver:2bits][type:4bits][route:2bits]`
- Route types: `FLOOD` (path-building), `DIRECT` (fixed path), `TRANSPORT_*` (with zone codes)
- Payload types: `REQ`, `RESPONSE`, `TXT_MSG`, `ACK`, `ADVERT`, `GRP_TXT`, `TRACE`, `MULTIPART`
- Max sizes: `MAX_PATH_SIZE=64`, `MAX_PACKET_PAYLOAD=184`, `MAX_TRANS_UNIT=255`

**Routing flow:**
- `Mesh::routeRecvPacket()` decides: discard, forward, or process
- `allowPacketForward()` checks if node should retransmit (subclass implements deduplication)
- `getRetransmitDelay()` calculates backoff based on SNR/hop count

## Coding Conventions

**Memory discipline (embedded focus):**
- **NO dynamic allocation** except in `begin()` / `setup()` functions
- Use static pools (see `StaticPoolPacketManager` - 8 packet pool)
- Buffers are fixed-size arrays: `uint8_t payload[MAX_PACKET_PAYLOAD]`

**Style rules:**
- Same brace/indent style as core modules (K&R-ish, 2-space indent)
- Do NOT retroactively reformat existing code (creates unhelpful diffs)
- Keep embedded mindset: concise, minimal abstraction layers
- Use `MESH_DEBUG_PRINTLN()` macros (compiled out when `MESH_DEBUG` not set)

**Platform conditionals:**
```cpp
#if defined(ESP32)
  #include <SPIFFS.h>
#elif defined(NRF52_PLATFORM)
  #include <InternalFileSystem.h>
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
#endif
```

## Creating New Applications

**Extend `BaseChatMesh` for chat/messaging apps:**
1. Override routing hooks: `filterRecvFloodPacket()`, `allowPacketForward()`, `getRetransmitDelay()`
2. Handle data: `onPeerDataRecv()` for encrypted messages
3. Manage contacts: Use `ContactInfo contacts[MAX_CONTACTS]` pattern
4. See `examples/companion_radio/MyMesh.h` as reference

**Critical virtual methods to implement:**
- `searchPeersByHash()` - Lookup known peers by hash
- `getPeerSharedSecret()` - ECDH shared secret calculation
- `onPeerDataRecv()` - Process decrypted peer messages

**Hardware abstraction via `target.h`:**
```cpp
extern HeltecV3Board board;              // MainBoard implementation
extern CustomSX1262Wrapper radio_driver; // Radio implementation  
extern AutoDiscoverRTCClock rtc_clock;   // RTCClock implementation

bool radio_init();                       // Initialize radio hardware
mesh::LocalIdentity radio_new_identity(); // Generate crypto identity
```

## Helper Classes & Utilities

**Filesystem/storage:**
- `IdentityStore` - Load/save Ed25519 keypairs
- `SimpleMeshTables` - Packet deduplication via seen-hash table
- Platform-specific: `InternalFS` (NRF52), `SPIFFS` (ESP32), `LittleFS` (RP2040)

**UI components (in `src/helpers/ui/`):**
- `MomentaryButton` - Debounced button handler
- `SSD1306Display`, etc. - Display abstractions (enabled via `-D DISPLAY_CLASS`)

**Radio wrappers (in `src/helpers/radiolib/`):**
- `RadioLibWrapper` - Generic RadioLib adapter
- `CustomSX1262Wrapper` - SX1262-specific tweaks
- `ESPNOWRadio` - ESP-NOW transport for ESP32

## Key Files Reference

- [src/MeshCore.h](src/MeshCore.h) - Core constants, macros, `MainBoard` interface
- [src/Packet.h](src/Packet.h) - Packet structure, route/payload type constants
- [src/Mesh.h](src/Mesh.h) - Routing layer, encryption, peer management
- [src/Dispatcher.h](src/Dispatcher.h) - Radio I/O, transmission queue
- [src/helpers/BaseChatMesh.h](src/helpers/BaseChatMesh.h) - Chat application base class
- [platformio.ini](platformio.ini) - Top-level PlatformIO config, base build flags
- [docs/packet_structure.md](docs/packet_structure.md) - Packet format details
- [docs/faq.md](docs/faq.md) - User FAQ (helpful for understanding use cases)

## Common Tasks

**Adding a new hardware variant:**
1. Create `variants/<variant_name>/` with `platformio.ini`, `target.h`, `target.cpp`
2. Define board-specific pins in build flags (`-D P_LORA_NSS=8`, etc.)
3. Implement board class (extend `mesh::MainBoard`)
4. Add `[env:<variant>_companion]`, `[env:<variant>_repeater]` targets
5. Link examples via `build_src_filter = +<../examples/companion_radio>`

**Debugging packet routing:**
- Enable `-D MESH_DEBUG=1` and `-D MESH_PACKET_LOGGING=1`
- Check `Mesh::routeRecvPacket()` logic
- Verify `allowPacketForward()` and `hasSeen()` deduplication
- Monitor serial output for `MESH_DEBUG_PRINTLN()` messages

**Testing changes:**
- Use `examples/simple_secure_chat` for quick terminal-based testing
- Flash multiple devices with different variants
- Monitor serial console for debug output (115200 baud)
