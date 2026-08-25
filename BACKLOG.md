# Envycore backlog

Canonical queue for **MeshEnvy firmware work** in this repo. Each item gets its own `feature/<name>` branch, bench gate, and merge to `envyos/main`. Distro semver is chosen at publish time in `envyos/envyos17/` — not pre-assigned here.

Enterprise index: `ops/initiatives/envyos-backlog.md` (summary rows only).

## How to use

| Column | Meaning |
|--------|---------|
| **ID** | Stable backlog ID (`EC-###`) |
| **Priority** | P0 field blocker; P1 high leverage; P2 polish; Icebox deferred |
| **Effort** | S / M / L / XL desk or bench days |
| **Depends** | Other EC IDs that must land on `envyos/main` first |
| **Status** | `backlog` · `in_progress` · `bench` · `merged_main` · `published` · `icebox` |

When an item merges to `envyos/main`, set status `merged_main`. When it ships in a tagged distro release, move to **Done** with date + version.

## Backlog

| ID | Item | Branch | Priority | Effort | Depends | Bench gate | Status |
|----|------|--------|----------|--------|---------|------------|--------|
| EC-001 | MeshCore companion-v1.17 upgrade | `envyos/freshen/companion-v1.17.0` | P1 | L | EC-000 | slim repeater build; native ConfigSerializer tests; FRESHEN.lock v1.17 | merged_main |
| EC-002 | nRF52 hardware WDT + EnvyBoot gate | `feature/nrf52-watchdog` | P1 | M | EC-001 | WDT trip/recover; OTA apply with EnvyBoot WDT feed | backlog |
| EC-003 | EndF version restamp on rebuild | `feature/endf-restamp` | P2 | S | EC-001 | Rebuild same version — EndF trailer matches | backlog |
| EC-004 | doctor CLI + atomic prefs | `feature/doctor` | P1 | M | EC-001 | `doctor check`; atomic prefs save under low FS space | backlog |
| EC-005 | OTA self-serve policy (merkle bench + disable) | `feature/ota-self-serve-policy` | P2 | S | EC-001 | No self-serve hash traffic at boot | backlog |
| EC-006 | OTA catalog filter + cache layout | `feature/ota-catalog-filter` | P2 | M | EC-001 | `ota ls` filtered to own target | backlog |
| EC-007 | Firmware identity codegen | `feature/firmware-identity-codegen` | P2 | S | EC-001 | Release rebuild only touches `FirmwareIdentity.generated.cpp` | backlog |
| EC-008 | Bench `-debug` target twins | `feature/debug-targets` | P2 | S | EC-001 | `-debug` twin builds and boots with log tail | backlog |
| EC-009 | Release tooling + changelog docs | `chore/release-tooling` | P2 | S | EC-001 | Build scripts + changelog present; docs render | backlog |

### Source commits (from `envyos/dev` monolith)

| ID | Cherry-pick SHAs |
|----|------------------|
| EC-001 | `origin/envyos/freshen/companion-v1.17.0` merge + `9e71d50f` (ConfigSerializer native tests) |
| EC-002 | `589c8db6`, `3674c1b7`, `aaa6e583` |
| EC-003 | `163dc2c3` |
| EC-004 | `f79f12d1`, `ad4b1265`, `62a48440` |
| EC-005 | `e15e986d`, `5b06eb74` |
| EC-006 | `56dc37ac` |
| EC-007 | `165e7277` |
| EC-008 | `830ffa4d` |
| EC-009 | `918c7ad8`, `ac4a48db`, `c79c9029` |

EC-001 includes freshen overlay: SenseCAP slim OTA env (`481c9aa6`), NOR/SD seeder allow CLI (`502b2e9e`).

### Upstream PR bases

| ID | PR base |
|----|---------|
| EC-001 | EnvyOS-only freshen |
| EC-002 | meshcore-dev `dev` |
| EC-003 | vk496 `feature/ota-lora` |
| EC-004 | meshcore-dev `dev` |
| EC-005, EC-006 | vk496 `feature/ota-lora` |
| EC-007–EC-009 | EnvyOS-only |

## Icebox

| ID | Item | Notes |
|----|------|-------|
| EC-010 | Companion FS wedge | Deferred to v0.3.0 per `envyos/CHANGELOG.md` on dev |

## Done

| ID | Item | Date | SHA / version |
|----|------|------|---------------|
| EC-000 | defer-remote-cli lockup fix | 2026-08-13 | `a2a13e18` on `envyos/main` (v0.1.3 hotfix) |

## Log

| Date | Note |
|------|------|
| 2026-08-25 | EC-001 merged to `envyos/main` (`1689985c`). Native tests 66/66 pass. FRESHEN.lock → companion-v1.17.0. |
