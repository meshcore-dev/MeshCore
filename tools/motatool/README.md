# motatool — self-contained MeshCore OTA host tool (C++)

A small, portable C++17 tool that **builds**, **verifies**, and **serves** MeshCore `.mota`
firmware-update containers. It is an independent project (its own `CMakeLists.txt`) that compiles on a
laptop, a Raspberry Pi, or any architecture with a C++17 compiler — so it can run as a lightweight
folder-relay daemon on small hardware.

It implements the wire spec in [`../../docs/ota_protocol.md`](../../docs/ota_protocol.md) and is
cross-checked byte-for-byte against the Python reference packager `tools/mota/motalib.py` (a full signed
`.mota` built by `motatool` is identical to the reference's output).

## What it does

| Command | Purpose |
|---|---|
| `build`  | Create a **full** or **delta** `.mota` from a firmware (local file **or** http(s) URL). |
| `verify` | Validate one or more `.mota` (merkle tree, leaves vs payload, image hash, Ed25519 signature). `--pub` requires a specific signer; `--base` confirms a sequential delta rebuilds its image. |
| `inspect`| Print every field of a `.mota`'s manifest (debugging). |
| `serve`  | Serve a **folder** of `.mota` to a node over USB serial. Invalid files are warned about and skipped — one corrupt file never sinks the rest. |
| `keygen` | Generate an Ed25519 signing keypair (hex). |

Every command has detailed, example-rich help: `motatool <command> --help`.

## Build

Dependencies: a C++17 compiler, CMake, and **OpenSSL** (libcrypto: SHA-256 + Ed25519). For URL input
and delta creation, see the notes below.

```bash
sudo apt install build-essential cmake libssl-dev      # Debian / Ubuntu / Raspberry Pi OS
cmake -S tools/motatool -B tools/motatool/build
cmake --build tools/motatool/build -j
# -> tools/motatool/build/motatool
```

### Tests

Unit tests (a tiny built-in harness — no external framework) cover the crypto, EndF, build/verify,
merkle, parse rejection, the folder scanner + seeder protocol, and a real detools delta round-trip
(auto-skipped if `detools` isn't installed):

```bash
ctest --test-dir tools/motatool/build --output-on-failure
# or directly:  tools/motatool/build/motatool_tests
```

## Usage

```bash
MT=tools/motatool/build/motatool

# 1. signing keypair (64-char hex)
$MT keygen --out signer.key            # writes signer.key + signer.key.pub

# 2a. FULL .mota — payload IS the flashable image (identity is auto-read from the firmware's EndF)
$MT build --fw firmware.bin --sign signer.key --out-dir ./motas/

# 2b. FULL .mota straight from a URL
$MT build --fw https://example.org/Heltec_v3_repeater.bin --sign signer.key --out-dir ./motas/

# 2c. DELTA .mota against a previous release (codec auto-selected from the hardware tag:
#     nRF52 -> in-place, ESP32 -> sequential; override with --codec)
$MT build --fw new.bin --base old.bin --sign signer.key --out-dir ./motas/
$MT build --fw new.bin --base old.bin --codec sequential --out-dir ./motas/

# 3. validate a folder of .mota (optionally require a signer / check a delta against its base)
$MT verify ./motas/*.mota
$MT verify update.mota --pub signer.key.pub
$MT verify delta.mota  --base old_firmware.bin

# 4. dump a single .mota's manifest fields
$MT inspect ./motas/RAK4631_04D413FD_v1.16.0_full_ABCD1234.mota

# 5. serve a folder to a node over its USB serial (recursive; skips non-.mota; warns on corrupt)
$MT serve --dir ./motas --serial /dev/ttyUSB0 --baud 115200 -v
```

`build` notes:
- **Identity is self-described.** A firmware built by the project's `pio_endf.py` carries its
  `target_id`/`fw_version`/`hw_id` in its 56-byte `EndF` trailer; `build` reads them, so
  `--target-env`/`--target-id`, `--fw-version`, and `--hw-id` are **optional** (explicit flags override).
- **Cross-hardware delta guard.** A delta is refused if the base and target firmware identities differ
  (read from their `EndF`, not filenames); pass `--force` to override.
- **Delta codec** needs the `detools` encoder (the project's pinned codec — never reimplemented). Install
  it (`pip install detools`) and ensure it's on `PATH`, or pass `--detools <path>`. **Full** builds,
  **verify**, and **serve** need no detools. In-place defaults: `--inplace-memory 0xAE000`
  (nRF52 workspace), `--inplace-segment 4096`.
- **URL input** uses the system `curl` (falling back to `wget`) — no link-time dependency.
- All output is written into one folder (`--out-dir`, default `.`) with a descriptive, unique name:
  `<hw>_<target>_v<ver>_<full|seqdelta|ipdelta>_<mid>.mota`.

## Serving is transport-agnostic (USB serial today, BLE later)

The protocol is split so the serving logic is reusable across links:

- **`SeederCore`** (`src/serve.{h,cpp}`) is transport-free: it maps a request `(op, args)` to a response
  `(status, payload)` — `COUNT` / `DESCRIBE(idx)` / `READ(idx, off, len)` over the validated catalog.
- **`SerialTransport` + `serve_serial()`** wrap it with the byte-stream framing (magic + XOR checksum +
  resync) for the unreliable USB-UART link (`MotaSeederProto.h`).

To serve the same folder over **BLE** (e.g. an Android phone relaying to a MeshCore node), implement the
transport at the GATT layer — a request characteristic write hands `(op, args)` straight to
`SeederCore::handle()` and the reply is sent as a notification. No byte-stream framing is needed there
(BLE is reliable/segmented), and `SeederCore` + `Folder` are reused unchanged. OpenSSL builds for the
Android NDK, so the validation path ports as-is.

## Relationship to `tools/mota/`

`motatool` is the user-facing CLI: it replaced the old Python `mota.py` (build/verify/inspect/keygen),
`mota_seeder.py` (serve), and `dev_motas.py` (CI `.mota` packaging — its nRF52 `.hex` extraction is now a
built-in `motatool` input format), all removed. The Python `tools/mota/` directory remains as the
**reference implementation** (`motalib.py`, the spec oracle + unit tests) and the **firmware build/test
glue** (`pio_endf.py` build hook, `gen_vectors.py` test vectors). Their `.mota` output is byte-identical
(verified by cross-checks).
