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
| EC-010 | Companion FS wedge | — | Icebox | M | EC-004 | — | icebox |

## Icebox

| ID | Item | Notes |
|----|------|-------|
| EC-010 | Companion FS wedge | Deferred to v0.3.0 per `envyos/CHANGELOG.md` |

## Done

| ID | Item | Date | SHA / version |
|----|------|------|---------------|
| EC-000 | defer-remote-cli lockup fix | 2026-08-13 | `a2a13e18` on `envyos/main` (v0.1.3 hotfix) |
| EC-001 | MeshCore companion-v1.17 upgrade | 2026-08-25 | `1689985c` merge + FRESHEN.lock v1.17 |
| EC-002 | nRF52 hardware WDT + EnvyBoot gate | 2026-08-25 | `feature/nrf52-watchdog` → main |
| EC-003 | EndF version restamp on rebuild | 2026-08-25 | `feature/endf-restamp` → main |
| EC-004 | doctor CLI + atomic prefs | 2026-08-25 | `feature/doctor` → main |
| EC-005 | OTA self-serve policy | 2026-08-25 | `feature/ota-self-serve-policy` → main |
| EC-006 | OTA catalog filter + cache layout | 2026-08-25 | `feature/ota-catalog-filter` → main |
| EC-007 | Firmware identity codegen | 2026-08-25 | `feature/firmware-identity-codegen` → main |
| EC-008 | Bench `-debug` target twins | 2026-08-25 | `feature/debug-targets` → main |
| EC-009 | Release tooling + changelog docs | 2026-08-25 | `chore/release-tooling` → main |

### Upstream PR branches (pure track)

| ID | Branch on `origin` | PR base | Notes |
|----|-------------------|---------|-------|
| EC-000 | `feature/defer-remote-cli` | meshcore-dev `dev` | Reset from post-v1.17 main |
| EC-002 | `feature/nrf52-watchdog` | meshcore-dev `dev` | Pushed; rebase onto meshcore/dev for cross-fork PR |
| EC-004 | `feature/doctor` | meshcore-dev `dev` | Pushed; rebase onto meshcore/dev for cross-fork PR |
| EC-003 | `feature/endf-restamp` | vk496 `feature/ota-lora` | Pushed; vk496 remote not configured locally |
| EC-005 | `feature/ota-self-serve-policy` | vk496 `feature/ota-lora` | Pushed |
| EC-006 | `feature/ota-catalog-filter` | vk496 `feature/ota-lora` | Pushed |

## Log

| Date | Note |
|------|------|
| 2026-08-25 | Backlog split complete. All EC-001–EC-009 merged to `envyos/main`. Native tests 72/72. `envyos/dev` reset to main. Tag `envyos/dev-pre-split` preserves monolith. |
