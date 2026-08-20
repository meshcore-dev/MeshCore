# EnvyOS CLI extensions

EnvyOS builds on [MeshCore CLI commands](./cli_commands.md) (companion tag **v1.17.0** in v0.2.0). This page documents **additional or changed** serial/remote admin commands in the MeshEnvy overlay.

**Scope:** repeater, room server, and sensor firmware built from the EnvyOS `envycore` fork. OTA subcommands are documented in [OTA user guide](./ota_user_guide.md) and [OTA protocol](./ota_protocol.md).

**Serial-only** commands require USB (or TCP console where compiled). They return `ERR … requires USB` over remote admin.

---

## Navigation

- [Filesystem doctor](#filesystem-doctor)
- [Filesystem garbage collection](#filesystem-garbage-collection)
- [Logging (serial mirror)](#logging-serial-mirror)
- [Prefs save errors](#prefs-save-errors)
- [Routing (next-hop retry)](#routing-next-hop-retry)
- [Bench debug builds](#bench-debug-builds)

---

## Filesystem doctor

Wedged **InternalFS** (nRF52/STM32) or full SPIFFS can make `set name`, `set radio`, ACL, and region saves fail silently or return generic errors. The `doctor` commands diagnose and recover without re-flashing when possible.

Typical workflow on a full partition:

```
doctor fs stat
doctor fs ls
doctor gc
doctor fs stat
set name MyNode
reboot
```

If `doctor gc` cannot free enough space, use `doctor fs fix` (non-destructive attempt) or `doctor fs format` (wipes FS, rebuilds prefs/identity/ACL/regions from RAM).

### `doctor fs check`

**Usage:** `doctor fs check`

Tests whether the current in-RAM prefs can be written atomically to flash (writes to `/.doctor_prefs.json`, then removes it). Does **not** change running prefs.

**Reply examples:**

- `OK prefs_writeable prefs=1 id=1 acl=0 regions=0`
- `ERR no space left on device (try: doctor gc)`
- `ERR prefs write failed lfs=-28 (try: doctor fs fix)`

Works over remote admin.

---

### `doctor fs fix`

**Usage:** `doctor fs fix`

Tries a normal prefs save first. If that fails, **formats InternalFS and rebuilds** identity, prefs, ACL, and regions from RAM.

**Reply examples:**

- `OK prefs repaired`
- `OK fs rebuilt from RAM`
- `ERR format failed` / `ERR remount failed` / prefs error string (see [Prefs save errors](#prefs-save-errors))

Works over remote admin.

---

### `doctor fs format`

**Usage:** `doctor fs format`

Same rebuild path as `fix`, but always formats first. Destructive to flash contents not held in RAM.

**Reply:** `OK fs formatted from RAM` or an error.

Works over remote admin.

---

### `doctor fs stat`

**Usage:** `doctor fs stat`

**Serial only.** Prints partition summary lines prefixed `FS_STAT` on serial, then a one-line summary reply.

**Serial output (nRF52 InternalFS example):**

```
FS_STAT begin
FS_STAT total 28672
FS_STAT used~ 28544 free~ 128
FS_STAT blocks 224 used 223 bsize 128
FS_STAT file /prefs.json 0
FS_STAT file /_main.id 96
...
FS_STAT end
```

**Reply:** `OK free~=128/28672 blk=223/224`

---

### `doctor fs ls`

**Usage:** `doctor fs ls`

**Serial only.** Recursive listing; lines prefixed `FS_LS` on serial.

**Reply:** `OK see serial FS_LS`

Use to find large cruft (`packet_log`, legacy Meshtastic `prefs/`, `com_prefs`, etc.).

---

### `doctor fs probe`

**Usage:** `doctor fs probe`

**Serial only.** Writes temporary probe files at increasing sizes (1 B through 4096 B), then runs the same prefs JSON write test as `check`. Lines prefixed `FS_PROBE` on serial.

**Reply examples:**

- `OK raw_max=512 prefs=ok`
- `OK raw_max=128 fail>=256@write prefs=fail`

---

### `doctor fs dump`

**Usage:** `doctor fs dump`

**Serial only.** Hex-dumps the raw flash region backing InternalFS (`FS_DUMP` lines). For deep corruption analysis; not needed for normal recovery.

**Reply:** `OK dumped N bytes`

---

## Filesystem garbage collection

### `doctor gc`

**Usage:** `doctor gc`

Removes reclaimable files that commonly fill InternalFS:

| Path | Reason |
|------|--------|
| `/packet_log` | Uncapped RX log from `log start` |
| `/com_prefs` | Legacy prefs path |
| `/prefs/` tree | Meshtastic protobuf leftovers |
| `/.doctor_*` | Doctor probe/check temp files |

**Reply:** `OK gc removed N item(s)`

Works over remote admin. Run before `set …` when `doctor fs stat` shows the partition nearly full.

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
| Other LFS error | `ERR prefs <stage> failed lfs=-NN (try: doctor fs fix)` |
| JSON serialize failure | `ERR prefs serialize failed (try: doctor fs fix)` |

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

## Upstream notes

See MeshEnvy `docs/upstream-prs.md` in the ota repo for PR tracking. Sensible upstream targets:

| Change | Upstream? |
|--------|-----------|
| Atomic prefs save | **Yes** — `meshcore-dev/MeshCore` `dev` |
| `log tail` | **Yes** — PR open |
| Next-hop retry | **Yes** — PR open |
| `doctor fs check` (prefs write probe only) | **Maybe** — lighter subset without dump/probe/gc |
| Full `doctor fs` + `doctor gc` | **EnvyOS-only** — bench/ops tooling |
| Adafruit LFS `fsLastErr` hooks | **EnvyOS-only** — vendor fork patch |
| Packet log size cap | **Yes** — recommended future MeshCore fix (not in v0.2.0) |
