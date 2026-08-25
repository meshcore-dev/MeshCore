# Changelog — EnvyOS firmware

EnvyOS-owned firmware changes (the MeshEnvy overlay on MeshCore in `envycore/`). MeshCore base bumps link the upstream companion tag; do not copy upstream notes here.

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versions match `ENVYOS_VERSIONS` `firmware=` / `envycore/envyos/VERSION` and `build/firmware/<ver>/` trees. Policy: ota repo `docs/change-management.md`.

## [Unreleased]

### Changed

- OTA catalog queries (`ota ls`, jittered beacon fetch) send `filter_target = own target_id` so sources filter `OTA_HAVE` before the 12-slot serve cap.
- OTA listener catalog (`ota ls` rows) ingests only rows for the node's target (or `ota want` target). No `OTA_HAVE` wire change (delta-base filtering deferred to v0.3.0).
- OTA folder relay (`OTA_FOLDER_SERIAL`) bench-only on `RAK_WisMesh_Tag_repeater`; `MotaSourceSerial` excluded from field slim builds.
- Build identity (`FIRMWARE_VERSION`, build date, MOTA target id) codegen into `FirmwareIdentity.generated.cpp` so release stamps no longer invalidate every translation unit via global `-D` flags.

### Added

- Bench debug twins of each shipped target (`<slug>-debug`): log tail, OTA, and admin serial on boot; separate MOTA target id.
- `doctor check|stat|ls|probe|dump|gc` for wedged InternalFS; `LFS_ERR_NOSPC` surfaces as `ERR no space left on device (try: doctor gc)`.
- Atomic prefs save (`saveConfigJsonAtomic` / `writeFileAtomic`): prefs, ACL, regions, companion contacts/channels/blobs.
- Fail-fast when InternalFS is critically full (avoids multi-second LittleFS alloc retries before NOSPC).
- Docs: `docs/envyos_cli_extensions.md` (doctor, gc, prefs errors, debug twins).

### Changed

- Flat `doctor` CLI (`check|stat|gc|ls|probe|dump`); removed `doctor fs` namespace and `fix`/`format` rebuild-from-RAM.
- OTA self-serve disabled (`OTA_SELF_SERVE=0`); nodes no longer hash/serve their running firmware. **Remove self-serve code in v0.3.0.**
- OTA self-serve merkle no longer starts at boot; repeaters stay quiet until `ota announce` / `ota folder on`.
- Field `rak4631-repeater-slim` no longer enables log tail / OTA_DEBUG / ADMIN_DEBUG at boot (use the `-debug` twin).

## [0.2.0] - 2026-08-14

Targets EnvyOS distro **v0.2.0** (in progress — not yet on [GitHub Releases](https://github.com/MeshEnvy/envyos/releases)). Accumulated during internal v0.1.3 dev. MeshCore base: [companion-v1.17.0](https://github.com/meshcore-dev/MeshCore/releases/tag/companion-v1.17.0).

### Fixed

- Remote admin + Send Advert lockup on RAK4631 slim (RX-path stack overflow); remote CLI deferred off the packet RX handler.
- nRF52 watchdog gate blocks only mota-apply bootloaders lacking `MOTA_BL_FEAT_WDT_FEED`.

### Added

- SenseCAP P1-Pro slim repeater (`sensecap-p1pro-repeater-slim`).
- nRF52 repeater hardware WDT (30 s default, prefs + CLI); companions excluded.
- `ver` stamp includes envycore SHA and UTC build date.

### Improved

- OTA self-serve merkle on nRF52 (EndF RAM cache, chunked merkle).

## [0.1.2] - 2026-08-03

Shipped with EnvyOS distro v0.1.2. Pin bump only (same EnvyOS overlay as 0.1.1).

## [0.1.1] - 2026-08-03

Shipped with EnvyOS distro v0.1.1. Pin refresh (same-day follow-up to 0.1.0).

## [0.1.0] - 2026-08-03

First fleet firmware. Full history in ota repo `CHANGELOG.md` v0.1.0 (predates this file).

### Added

- LoRa OTA (`.mota` full + detools deltas, seeder / superseeder roles).
- Next-hop retry for direct-path relays (`hop.retry`, default off).
- Serial `log tail` on repeaters.
- Slim repeater roles (RAK4631, SenseCAP P1-Pro) and SD superseeder.
- Companion boot fsck for corrupt LittleFS.
