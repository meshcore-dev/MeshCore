# Building MeshCore with Zephyr RTOS

This directory turns MeshCore into a **Zephyr module**, so the mesh engine can
be built and configured from a Zephyr application instead of (or alongside) the
existing PlatformIO/Arduino build.

## Layout

```
zephyr/
  module.yml            # declares MeshCore as a Zephyr module
  Kconfig               # MESHCORE_* options (features, radio, LoRa RF params)
  CMakeLists.txt        # builds the selected sources into a zephyr_library
  README.md             # this file
ports/zephyr/
  include/Arduino.h     # tiny Arduino shim (millis/delay/random/Serial) on Zephyr
  include/Stream.h      # Print/Stream surface used by the core
  include/MeshCoreZephyr.h   # mesh::Clock/RNG/RTC/Board bound to Zephyr APIs
  arduino_compat.cpp    # shim implementation
  MeshCoreZephyr.cpp    # platform-binding implementation
arch/zephyr/Crypto/     # vendored rweather/Crypto subset (see its README)
samples/zephyr/
  mesh_min/             # buildable loopback sample (no radio HW needed)
```

## How the module is wired up

`module.yml` points Zephyr at this directory's `CMakeLists.txt` and `Kconfig`.
Zephyr sources them automatically for every project listed as a module. The
CMake glue only compiles anything when `CONFIG_MESHCORE=y`, and it builds:

- the mesh **core** (`src/Dispatcher|Identity|Mesh|Packet|Utils.cpp` plus
  `helpers/StaticPoolPacketManager.cpp`) always;
- the **crypto** it depends on — the vendored `arch/zephyr/Crypto` subset and
  `lib/ed25519` — always;
- the **Arduino shim** (`CONFIG_MESHCORE_ARDUINO_COMPAT`, default y);
- the **Zephyr platform helpers** (`CONFIG_MESHCORE_ZEPHYR_HELPERS`, default y).

MeshCore is exposed as a `zephyr_interface_library_named(meshcore)`, so its
include paths and the LoRa RF parameters (`MESHCORE_LORA_FREQ/BW/SF/CR/TX_POWER`,
forwarded as the same `-DLORA_*` definitions the PlatformIO build uses) reach
**application** code as well as the library's own sources.

Warnings are relaxed for the module's own library target only (`-Wno-sign-compare`,
`-Wno-unused-variable`, `-Wno-unused-function`) because the MeshCore core and the
vendored crypto are upstream-maintained sources built with `-w` under PlatformIO;
Twister compiles with `-Werror`. Application code keeps Zephyr's full warning set.

## Adding MeshCore to your Zephyr workspace (west submanifest)

MeshCore is intended to be pulled in as a west **submanifest / project**. In the
top-level (application or Zephyr) `west.yml`:

```yaml
manifest:
  remotes:
    - name: meshcore
      url-base: https://github.com/meshcore-dev
  projects:
    - name: MeshCore
      remote: meshcore
      revision: feature/zephyr_sub_build
      path: modules/lib/meshcore      # any path inside the workspace
```

Then:

```console
west update
```

Because `zephyr/module.yml` exists, Zephyr discovers the module with no extra
configuration. Verify with:

```console
west list | grep -i meshcore
```

If MeshCore lives outside the manifest, point Zephyr at it explicitly instead:

```console
west build -b <board> <app> -- -DEXTRA_ZEPHYR_MODULES=/abs/path/to/MeshCore
```

## Enabling it in an application

In the application's `prj.conf`:

```conf
CONFIG_MESHCORE=y
CONFIG_CPP=y
CONFIG_STD_CPP17=y
CONFIG_ENTROPY_GENERATOR=y
# RF parameters (optional, these are the defaults)
CONFIG_MESHCORE_LORA_FREQ="869.618"
CONFIG_MESHCORE_LORA_SF=8
```

## Build the sample

```console
west build -b native_sim samples/zephyr/mesh_min
west build -t run
```

Expected output: `MeshCore minimal sample: mesh engine up`.

Run it under Twister:

```console
west twister -T samples/zephyr/mesh_min -p native_sim
```

## How to add more build examples

The PlatformIO tree already ships several roles under `examples/`
(`simple_repeater`, `simple_room_server`, `companion_radio`, `simple_sensor`,
…). Mirror them as Zephyr samples like this:

1. **Copy the skeleton.** Duplicate `samples/zephyr/mesh_min` to
   `samples/zephyr/<role>` (each needs `CMakeLists.txt`, `prj.conf`,
   `sample.yaml`, `src/`).

2. **Pull in the role sources.** The example `.cpp`/`.h` live under
   `examples/<role>`. Add them to the sample's `CMakeLists.txt`:

   ```cmake
   set(MC_EXAMPLE ${ZEPHYR_CURRENT_MODULE_DIR}/examples/simple_repeater)
   target_sources(app PRIVATE ${MC_EXAMPLE}/main.cpp)
   target_include_directories(app PRIVATE ${MC_EXAMPLE})
   ```

   (`ZEPHYR_CURRENT_MODULE_DIR` resolves to the MeshCore checkout.)

3. **Provide a real radio.** Replace `LoopbackRadio` with the Zephyr SX126x
   backend (see the roadmap below). Pins come from the board's existing
   devicetree node — upstream Zephyr boards such as `heltec_wifi_lora32_v3`,
   `rak4631`, `rak3112`, `lilygo/t_deck` and `seeed/wio_tracker_l1` already
   declare a `semtech,sx1262` node with a `lora0` alias, so no board overlay is
   needed. Build with `CONFIG_LORA=n` so no Zephyr LoRa driver binds to that
   node; `SPI_DT_SPEC_GET(DT_ALIAS(lora0), ...)` and the reset/busy/dio1
   `GPIO_DT_SPEC_GET`s still resolve.

4. **Storage / identity.** The Arduino examples persist identity via a
   filesystem (`InternalFS`/LittleFS). On Zephyr use the `settings` subsystem or
   an `fs`/LittleFS partition and adapt `IdentityStore`.

5. **Register with CI.** `sample.yaml` makes Twister pick the sample up; add the
   boards it should build for under `platform_allow`.

## Current status / porting roadmap

Working in this baseline:

- Module discovery, Kconfig, CMake glue.
- Core mesh engine + real crypto (AES-128, SHA-256, Ed25519) + Arduino shim +
  Zephyr clock/RNG/RTC/board compile and run.
- Loopback sample builds and passes under Twister on `native_sim`.

**Radio strategy.** MeshCore does *not* use RadioLib on Zephyr. RadioLib only ever
supplied ~25 chip primitives (`startReceive`, `getIrqFlags`, `readRegister`,
`getRSSI`, `scanChannel`, …); everything that determines on-air behaviour — the
preamble/header-valid state machine, noise-floor sampling, CAD/AGC policy,
`packetScore` — is MeshCore's own code. So the module vendors a self-contained
SX126x access layer over Zephyr SPI/GPIO instead. Zephyr's public `lora.h` API is
LoRaWAN-shaped and hides the IRQ access this needs, and the native driver's
`sx126x_hal.h` is a private header an out-of-tree module cannot include; proposing
a public raw-LoRa API upstream is the longer-term goal.

Still TODO (each is an independent, well-scoped step):

- Persistent identity/config storage (settings/LittleFS) so `IdentityStore` works.
- Extract the chip-agnostic radio policy layer out of
  `src/helpers/radiolib/RadioLibWrappers.{h,cpp}` so the Arduino and Zephyr
  backends share one implementation — required for a Zephyr node to interoperate
  on-air with PlatformIO-built nodes.
- The Zephyr SX126x backend (`ports/zephyr/drivers/sx126x/`) and a repeater
  sample on `heltec_wifi_lora32_v3`.
- Transports beyond LoRa (BLE/serial) — the Arduino `helpers/{esp32,nrf52}`
  transports are not portable as-is.
- Displays / sensors under `helpers/ui` and `helpers/sensors`.
```
