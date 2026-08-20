# EnvyOS CLI extensions

EnvyOS builds on [MeshCore CLI commands](./cli_commands.md) (companion tag **v1.17.0** in v0.2.0). This page documents **additional or changed** serial/remote admin commands in the MeshEnvy overlay.

**Scope:** repeater, room server, and sensor firmware built from the EnvyOS `envycore` fork. OTA subcommands are documented in [OTA user guide](./ota_user_guide.md) and [OTA protocol](./ota_protocol.md).

**Serial-only** commands require USB (or TCP console where compiled). They return `ERR … requires USB` over remote admin.

---

## Navigation

- [Doctor (filesystem)](#doctor-filesystem)
- [Logging (serial mirror)](#logging-serial-mirror)
- [Prefs save errors](#prefs-save-errors)
- [Routing (next-hop retry)](#routing-next-hop-retry)
- [Bench debug builds](#bench-debug-builds)

---

## Doctor (filesystem)

Wedged **InternalFS** (nRF52/STM32) or full SPIFFS can make `set name`, `set radio`, ACL, and region saves fail silently or return generic errors.

| Command | Remote admin | Role |
|---------|--------------|------|
| `doctor stat` | No (USB) | Partition headroom |
| `doctor gc` | Yes | Remove common cruft |
| `doctor check` | Yes | Prefs write probe |
| `doctor ls` | No (USB) | Bench: recursive listing |
| `doctor probe` | No (USB) | Bench: raw write sizing |
| `doctor dump` | No (USB) | Bench: hex dump flash region |

Typical recovery on a full partition:

```
doctor stat
doctor gc
doctor stat
doctor check
set name MyNode
reboot
```

Wipe without rebuild-from-RAM: serial **`erase`** (formats FS; reboot required).

### `doctor check`

Tests whether the current in-RAM prefs can be written atomically to flash (writes to `/.doctor_prefs.json`, then removes it). Does **not** change running prefs.

**Reply examples:**

- `OK prefs_writeable prefs=1 id=1 acl=0 regions=0`
- `ERR no space left on device (try: doctor gc)`
- `ERR prefs write failed lfs=-28`

### `doctor stat`

**Serial only.** Prints partition summary lines prefixed `FS_STAT` on serial, then a one-line summary reply.

**Reply:** `OK free~=128/28672 blk=223/224`

### `doctor gc`

Removes reclaimable files that commonly fill InternalFS:

| Path | Reason |
|------|--------|
| `/packet_log` | Uncapped RX log from `log start` |
| `/com_prefs` | Legacy prefs path |
| `/prefs/` tree | Meshtastic protobuf leftovers |
| `/.doctor_*` | Doctor probe/check temp files |

**Reply:** `OK gc removed N item(s)`

### `doctor ls` (bench)

**Serial only.** Recursive listing; lines prefixed `FS_LS` on serial. **Reply:** `OK see serial FS_LS`

### `doctor probe` (bench)

**Serial only.** Writes temporary probe files at increasing sizes, then runs the prefs JSON write test. Lines prefixed `FS_PROBE` on serial.

### `doctor dump` (bench)

**Serial only.** Hex-dumps the raw flash region backing InternalFS. **Reply:** `OK dumped N bytes`

---

## Logging (serial mirror)

Stock MeshCore captures packets to `/packet_log` with `log start` / `log stop` but only dumps the file over serial with bare `log`. EnvyOS adds live mirror (upstream PR [#2991](https://github.com/meshcore-dev/MeshCore/pull/2991)):

| Command | Description |
|---------|-------------|
| `log tail on` | Stream captured log lines to USB serial as they arrive; starts logging if inactive |
| `log tail off` | Stop serial mirror (logging may continue to file) |

**Serial only.** Field `-debug` repeater builds enable this at boot; production slims do not.

---

## Prefs save errors

Atomic saves (`writeFileAtomic`: temp file + rename) apply to `/prefs.json`, `/s_contacts`, `/regions2`, and companion contact/channel blobs.

When a `set …` or `password …` save fails, replies are specific instead of a generic write failure:

| Condition | Reply |
|-----------|--------|
| Partition critically full (≤2 free blocks on InternalFS) | `ERR no space left on device (try: doctor gc)` |
| LittleFS returned NOSPC | Same as above |
| Other LFS error | `ERR prefs <stage> failed lfs=-NN (try: doctor gc)` |
| JSON serialize failure | `ERR prefs serialize failed (try: doctor gc)` |

Stages: `open`, `write`, `rename`, `serialize`, `nospc`.

Debug builds log `savePrefs: end ok=0 stage=… lfs_err=…` on serial.

---

## Routing (next-hop retry)

EnvyOS ships [next-hop retry](https://github.com/meshcore-dev/MeshCore/pull/2980) (opt-in, default off):

| Command | Description |
|---------|-------------|
| `get hop.retry` / `set hop.retry <0-5>` | Extra direct-path retransmits if next-hop echo/HOP_ACK missing |
| `get hop.retry.ms` / `set hop.retry.ms <200-10000>` | Listen window before each retry (ms) |
| `get hop.ignore` / `set hop.ignore <0-255>` | **Test hook:** drop next N direct forwards (not persisted) |

Documented in [CLI commands § Routing](./cli_commands.md#view-or-change-next-hop-retry-for-direct-path-relays).

---

## Bench debug builds

EnvyOS `-debug` repeater/superseeder twins (e.g. `wismesh-tag-repeater-debug`) differ from field slugs:

- `ADMIN_DEBUG` / `OTA_DEBUG` enabled at boot
- `log tail` available without extra flags
- Separate MOTA target id (do not deploy to field nodes)

Use field slugs for production; debug twins for bench, OTA, and FS troubleshooting.

---

## Companion gap (deferred v0.3.0)

Companions use the same **28 KB InternalFS** on nRF52840 but **do not expose `doctor` commands** (no `CommonCLI` on that path). WisMesh Tag BLE builds also omit `EXTRAFS=1`, so contacts/channels/blobs share InternalFS with prefs/identity. A full partition wedges clients the same way as repeaters (failed saves, lost data on reboot).

**Deferred to v0.3.0** (see ota repo `docs/planned/v0.3.0.md`):

- Enable `EXTRAFS=1` on WisMesh companion (match RAK4631: contacts on secondary volume).
- Expose `doctor check` / `doctor gc` on companion (USB serial debug at minimum).
- Align with multi-volume FS CLI naming work.

v0.2.0 ships atomic saves and boot fsck on companion only.

---

## Upstream notes

See MeshEnvy `docs/good-upstream-contributor-policy.md` in the ota repo for GUCP tracking. Sensible upstream targets:

| Change | Upstream? |
|--------|-----------|
| Atomic prefs save (tmp + rename) | **Yes** — `meshcore-dev/MeshCore` `dev` |
| FS save error surfacing (`lfs=-NN`, NOSPC hint) | **Yes** — prerequisite for useful `doctor check` / `set` failures |
| `doctor stat`, `doctor gc`, `doctor check` | **Yes** — flat CLI |
| `doctor ls` / `probe` / `dump` | **No** — EnvyOS bench tooling |
| Adafruit LFS `fsLastErr` hooks | **Maybe** — ship as part of error-surfacing PR or upstream equivalent |
| `log tail` | **Yes** — PR open |
| Next-hop retry | **Yes** — PR open |
| Packet log size cap | **Yes** — recommended future MeshCore fix (not in v0.2.0) |
