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
  include/RadioLibZephyrHal.h# SCAFFOLD: RadioLib HAL for Zephyr (next step)
  arduino_compat.cpp    # shim implementation
  MeshCoreZephyr.cpp    # platform-binding implementation
samples/zephyr/
  mesh_min/             # buildable loopback sample (no radio HW needed)
```

## How the module is wired up

`module.yml` points Zephyr at this directory's `CMakeLists.txt` and `Kconfig`.
Zephyr sources them automatically for every project listed as a module. The
CMake glue only compiles anything when `CONFIG_MESHCORE=y`, and it builds:

- the mesh **core** (`src/*.cpp`) always;
- the **Arduino shim** (`CONFIG_MESHCORE_ARDUINO_COMPAT`, default y);
- the **Zephyr platform helpers** (`CONFIG_MESHCORE_ZEPHYR_HELPERS`, default y);
- the **RadioLib wrappers** (`CONFIG_MESHCORE_RADIOLIB`, default **n** — needs the
  RadioLib module and a Zephyr HAL, see below).

LoRa RF parameters (`MESHCORE_LORA_FREQ/BW/SF/CR/TX_POWER`) are forwarded as the
same `-DLORA_*` compile definitions the PlatformIO build uses.

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
CONFIG_REQUIRES_FULL_LIBCPP=y
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

   (`ZEPHYR_CURRENT_MODULE_DIR` resolves to the MeshCore checkout.) Select the
   matching role in `prj.conf` with `CONFIG_MESHCORE_ROLE_REPEATER=y` so future
   glue can key off it.

3. **Provide a real radio.** Replace `LoopbackRadio` with a RadioLib driver:
   - add RadioLib to the workspace `west.yml`;
   - implement `ZephyrHal` in `ports/zephyr/RadioLibZephyrHal.h`;
   - set `CONFIG_MESHCORE_RADIOLIB=y` and a `CONFIG_MESHCORE_RADIO_*` choice;
   - describe the SX126x/SX127x wiring in a board `.overlay` (spi + nss/rst/busy/dio1
     GPIOs) and read it via `gpio_dt_spec`.

4. **Storage / identity.** The Arduino examples persist identity via a
   filesystem (`InternalFS`/LittleFS). On Zephyr use the `settings` subsystem or
   an `fs`/LittleFS partition and adapt `IdentityStore`.

5. **Register with CI.** `sample.yaml` makes Twister pick the sample up; add the
   boards it should build for under `platform_allow`.

## Current status / porting roadmap

Working in this baseline:

- Module discovery, Kconfig, CMake glue.
- Core mesh engine + Arduino shim + Zephyr clock/RNG/RTC/board compile and run.
- Loopback sample on `native_sim`.

Still TODO (each is an independent, well-scoped step):

- RadioLib `ZephyrHal` (GPIO/SPI/timing) — scaffolded in
  `ports/zephyr/RadioLibZephyrHal.h`.
- Persistent identity/config storage (settings/LittleFS).
- Transports beyond LoRa (BLE/serial) — the Arduino `helpers/{esp32,nrf52}`
  transports are not portable as-is.
- Displays / sensors under `helpers/ui` and `helpers/sensors`.
```
