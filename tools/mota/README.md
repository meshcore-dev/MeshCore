# `tools/mota/` — OTA Python reference library & build/test tooling

The Python side of MeshCore's `.mota` OTA system. It is the **reference implementation** of the wire
spec ([`docs/ota_protocol.md`](../../docs/ota_protocol.md)) and the **build + test infrastructure** — it is
no longer a user-facing CLI.

> **Want to build / verify / inspect / serve `.mota` from the command line?** Use the self-contained C++
> tool [`tools/motatool/`](../motatool/). It supersedes the old `mota.py` / `mota_seeder.py` (now removed),
> produces byte-identical containers, and runs on small hardware. The files here are the spec oracle and
> the firmware build/test glue.

## Setup

Uses the repo's Python venv (`meshcore/`). Dependencies: `detools` (delta), `cryptography` (Ed25519),
`intelhex` (nRF52 `.hex` handling).

```bash
./meshcore/bin/pip install detools cryptography intelhex
```

## Files

| File | What |
|---|---|
| `motalib.py` | Core logic: multihash, EndF, merkle tree+proofs, manifest/container build/parse/verify. The **reference implementation** of the spec and the unit-test oracle. Imported by everything below. |
| `pio_endf.py` | **PlatformIO post-build hook** (wired in `platformio.ini`) that injects the `EndF` self-identity trailer into the flashed firmware (`-D ENABLE_OTA`). |
| `gen_vectors.py` | Generates `test/test_ota/mota_vectors.h` — the cross-check vectors the native C++ tests run against. |
| `gen_targets.py` | Generates `src/helpers/ota/OtaTargets.h` — the `target_id → env-name` table (every `ENABLE_OTA` env, resolved from `pio project config`). Shared by the firmware and `motatool` so a node can name a target seen over the air without sending the string. Regenerate when the OTA env set changes. |
| `test_mota.py` | Unit tests for `motalib` (run directly or via pytest). |

## Tests

```bash
./meshcore/bin/python tools/mota/test_mota.py      # EndF, merkle+proofs, full/delta, signing,
                                                   # tamper detection, approval enforcement
./meshcore/bin/python tools/mota/gen_vectors.py    # regenerate the native-test cross-check vectors
./meshcore/bin/python tools/mota/gen_targets.py    # regenerate src/helpers/ota/OtaTargets.h (needs `pio`)
```

## `EndF` build integration

`EndF` must live in the **flashed** firmware (not just inside the `.mota`), because a node serves its own
firmware and matches a delta's `base_hash` against its own `EndF`. Wiring (handled by `pio_endf.py`):

- **ESP32 / RP2040** (emit `firmware.bin`): `post:tools/mota/pio_endf.py` in the env's `extra_scripts` plus
  `-D ENABLE_OTA=1`. The hook appends the 56-byte `EndF` (with `target_id`/`fw_version`/`hw_id`) to the app
  `.bin` before merge.
- **nRF52 / STM32** (emit `.hex` → `.uf2`): the same hook rewrites the `.hex` with the trailer at the image
  end. The byte logic is `motalib.ensure_endf`, used everywhere.

`target_id` = `sha2-256:4(pio_env_name)`, `hw_id` = `-D MOTA_HW_ID`, `fw_version` = parsed from
`FIRMWARE_VERSION` — so a node (and `motatool`, reading the firmware's `EndF`) auto-discovers identity
without relying on filenames.
