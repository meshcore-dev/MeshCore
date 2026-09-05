# MeshCore Observer — Changelog

Changes to the MQTT observer / bridge work that powers the firmware offered on
[observer.gessaman.com](https://observer.gessaman.com/). Newest changes are at the top.

**Legend:** **New** = new capability · **Fix** = bug fix · **Improvement** = enhancement to
existing behavior · **Internal** = refactor / under-the-hood · **Docs** = documentation ·
**Build** / **CI** = build system & automation. **⬆ Upstream sync** marks a merge of the
upstream MeshCore `dev` branch, which generally pulls in a new MeshCore software version.

### June 2026

- **New** — AlertReporter integration in MyMesh for Room Server <sub>2026-06-17 · `985fda13`</sub>
- **Improvement** — Cumulative packet statistics (`packets_sent` / `packets_received`) added to the status message  <sub>2026-06-16 · `8bf590b1`</sub>
- **Improvement** — Packet path now published as an array of lowercase hex hop tokens  <sub>2026-06-16 · `e80a5ded`</sub>
- **CI** — Sync documentation files to the flasher site from the build workflow  <sub>2026-06-16 · `0844eee1`</sub>
- **CI** — Reworked the GitHub Actions workflow for rolling releases  <sub>2026-06-08 · `8fa38707`</sub>
- **New** — Radio Watchdog feature for the MQTT observer  <sub>2026-06-08 · `002e8d4a`</sub>
- **Improvement** — Firmware build-date format and UTC timestamps (always emit `+00:00`)  <sub>2026-06-06 · `b45373a3`</sub>
- **CI** — Build observer firmwares across 14 shards (up from 4)  <sub>2026-06-06 · `c29ede05`</sub>
- **CI** — Cache PlatformIO toolchains in CI  <sub>2026-06-06 · `6723776f`</sub>
- **CI** — Automated firmware builds via GitHub Actions  <sub>2026-06-06 · `981ed246`</sub>
- ⬆ **Upstream sync** — Synced with upstream MeshCore dev (brings MeshCore v1.16.0)  <sub>2026-06-06 · `dcc8f826`</sub>

### May 2026

- **New** — NZ Analyzer MQTT preset  <sub>2026-05-25 · `c0c845f5`</sub>
- **Internal** — Region definition helpers; docs refresh  <sub>2026-05-21 · `1296aa77`</sub>
- **Docs** — Clarified WiFi configuration instructions (password optional for open networks)  <sub>2026-05-21 · `38568eb1`</sub>
- **Improvement** — Alert PSK access now restricted by sender timestamp  <sub>2026-05-17 · `f717d673`</sub>
- **Docs** — Trimmed the fault-alert section in the implementation guide  <sub>2026-05-17 · `c41805d0`</sub>
- **Change** — Renamed CLI command `region bulk` → `region def`  <sub>2026-05-16 · `f3c6c348`</sub>
- **Internal** — Timestamp handling in MQTTMessageBuilder  <sub>2026-05-15 · `5a004f37`</sub>
- **New** — T-LoRa V2.1–1.6 observer configurations  <sub>2026-05-13 · `3cde9c10`</sub>
- **Fix** · `mqtt` — Hardened the T-LoRa MQTT observer and trimmed MQTT task stack usage  <sub>2026-05-13 · `daf9ee47`</sub>
- **Internal** — Alert PSK handling is now hex-only  <sub>2026-05-12 · `a8668840`</sub>
- **Improvement** — Alert PSK accepts both base64 and hex  <sub>2026-05-12 · `a865d0a3`</sub>
- **New** · `cli` — Bulk region hierarchy command  <sub>2026-05-12 · `19f95001`</sub>
- **Improvement** — Path hash size included in AlertReporter flood messages  <sub>2026-05-11 · `6a3ed5d4`</sub>
- **New** — T-Beam 1W observer support and partition handling  <sub>2026-05-11 · `fc99d6b3`</sub>
- **Improvement** — Fault alerts gained region-based scoping and banned channels  <sub>2026-05-11 · `16dc49fa`</sub>
- **New** — Fault alerts for WiFi and MQTT disconnections  <sub>2026-05-10 · `80355f32`</sub>
- **New** — ColoradoMesh MQTT preset  <sub>2026-05-10 · `08847342`</sub>
- **New** — MQTT origin handling and a string utility  <sub>2026-05-09 · `b9f478c9`</sub>
- **Improvement** — Repeat status included in the MQTT status message  <sub>2026-05-09 · `303cf8c2`</sub>
- **Internal** — MQTT origin now uses effective-origin logic  <sub>2026-05-09 · `ff031f48`</sub>
- ⬆ **Upstream sync** — Synced with upstream MeshCore dev  <sub>2026-05-09 · `b37db668`</sub>
- **New** · `mqtt` — T-LoRa V2.1-1.6 observer environments  <sub>2026-05-02 · `78e1500a`</sub>

### April 2026

- **Fix** · `mqtt` — Corrected ColoradoMesh preset details  <sub>2026-04-30 · `9f8f2d05`</sub>
- **New** · `platformio` — PSRAM support for the TBeam Supreme SX1262 variant  <sub>2026-04-28 · `0b05aede`</sub>
- **New** · `mqtt` — ColoradoMesh preset  <sub>2026-04-28 · `457edf69`</sub>
- **Fix** · `mqtt` — Added missing MQTTBridge error codes  <sub>2026-04-27 · `5a5e5373`</sub>
- **New** · `mqtt` — EastIdahoMesh preset and richer CLI preset listing  <sub>2026-04-25 · `753eb72f`</sub>
- **Fix** · `mqtt` — Removed errant `clearLastWill` from PsychicMqttClient  <sub>2026-04-25 · `32449e62`</sub>
- **Fix** · `mqtt` — Restored `origin` field position in packet messages  <sub>2026-04-25 · `673361b6`</sub>
- **Internal** · `mqtt` — Reorganized packet message structure  <sub>2026-04-25 · `8be27e4e`</sub>
- **Internal** · `mqtt` — Default TX packet settings and documentation  <sub>2026-04-25 · `bfcf0176`</sub>
- **New** · `platformio` — MQTT observer configs for the Heltec V4 expansion kit  <sub>2026-04-24 · `0a4f002f`</sub>
- **New** · `cli` — Radio watchdog command  <sub>2026-04-24 · `7b21be3e`</sub>
- **Internal** · `mqtt` — Standardized MQTT preset formatting  <sub>2026-04-24 · `6211885c`</sub>
- **Internal** · `heltec_tracker_v2` — Aligned FEM with meshcore-dev KCT8103L  <sub>2026-04-24 · `0d280257`</sub>
- **New** · `heltec_tracker_v2` — Restored LoRaFEMControl for GC1109  <sub>2026-04-24 · `255fce7f`</sub>
- ⬆ **Upstream sync** — Synced with upstream MeshCore dev  <sub>2026-04-24 · `c1989d6f`</sub>
- **New** · `mqtt` — chimesh and meshat.se presets  <sub>2026-04-24 · `ee4e39bc`</sub>
- **Fix** · `mqtt` — Restored legacy outbox behavior for QoS0 async publishes  <sub>2026-04-23 · `70722a58`</sub>
- **Fix** · `mqtt` — Config marked dirty on setter calls; applied on connect/reconnect  <sub>2026-04-23 · `bb67b04e`</sub>
- **Improvement** · `mqtt` — Better QoS handling and retry logic  <sub>2026-04-23 · `7d0c5bce`</sub>
- **Internal** · `mqtt` — Removed legacy `mqtt.server`/`port`/`username`/`password` CLI  <sub>2026-04-21 · `cfec3a5b`</sub>
- **Fix** · `mqtt` — Legacy CLI aligned with slot prefs; restored `isConfigValid`  <sub>2026-04-21 · `ac2662c5`</sub>
- **Internal** — PsychicMqttClient memory management; fixed-size callback arrays (v0.2.2)  <sub>2026-04-21 · `e9ff1ae0`</sub>
- **Improvement** — MQTTBridge memory management, deferred logging, and QoS on publish  <sub>2026-04-19 · `1ff9437f`</sub>
- ⬆ **Upstream sync** — Synced with upstream MeshCore dev  <sub>2026-04-19 · `563d9a7d`</sub>
- **Build** — Disabled MQTT debug config for heltec_v4 builds  <sub>2026-04-17 · `0ed29cb7`</sub>
- **Change** — gitignore generated cert files; meshmapper timeout reset to 0  <sub>2026-04-17 · `ab121708`</sub>
- **Change** — MeshMapper preset update; removed generated cert files  <sub>2026-04-17 · `074ac3e7`</sub>
- **Improvement** — Critical heap monitoring and deferred TLS; hard restart on prolonged low memory  <sub>2026-04-17 · `f4a4fd7c`</sub>
- **Internal** — Inline `StaticJsonDocument` allocations to reduce heap fragmentation  <sub>2026-04-16 · `ab6685a1`</sub>
- **New** — nashmesh preset  <sub>2026-04-14 · `13fbcd91`</sub>
- **Internal** — MQTTMessageBuilder takes an external JsonDocument to cut stack usage  <sub>2026-04-14 · `ff2df2b7`</sub>
- **Internal** — Removed the raw-data mutex in favor of core-specific staging  <sub>2026-04-14 · `0c190dac`</sub>
- **Improvement** — Radio watchdog avoids false alarms in quiet meshes  <sub>2026-04-14 · `961f8293`</sub>
- **New** — Radio diagnostics in MyMesh and CommonCLI  <sub>2026-04-10 · `7e4f75c9`</sub>
- **Improvement** — Internal heap stats in status messages  <sub>2026-04-06 · `74e45a79`</sub>
- **New** — TennMesh preset (fixed username/password auth)  <sub>2026-04-05 · `3b46e9ee`</sub>
- **Docs** — Single `set radio` command in the guide  <sub>2026-04-04 · `1c524a93`</sub>
- **Internal** — RX/TX packet logging controlled by MQTT settings (`mqtt_rx_enabled` pref)  <sub>2026-04-04 · `47b632aa`</sub>
- **Improvement** — Runtime-configurable MQTT slot limits based on PSRAM  <sub>2026-04-04 · `258a0d72`</sub>
- **Improvement** — Disconnect count/time diagnostics; per-variant WiFi TX power  <sub>2026-04-02 · `f0962eff`</sub>

### March 2026

- **Improvement** — Pre-allocated status JSON buffer in PSRAM; non-blocking NTP refresh  <sub>2026-03-31 · `cf4ec3f6`</sub>
- **New** — WiFi disconnect diagnostics  <sub>2026-03-31 · `0e82271e`</sub>
- **Fix** — Audience command parsing handles the 9-character prefix  <sub>2026-03-30 · `a4324a83`</sub>
- **New** — JWT audience configuration for custom slots  <sub>2026-03-30 · `1263e71d`</sub>
- **Improvement** — Partition-table guidance, merged-firmware flashing, embedded CA bundle  <sub>2026-03-30 · `da775bb2`</sub>
- **Docs** — Removed obsolete MQTT design docs  <sub>2026-03-28 · `384b80fc`</sub>
- **Docs** — README links to the MQTT Implementation Guide  <sub>2026-03-28 · `ff49d28f`</sub>
- **New** — Optional SNMP monitoring; concurrent MQTT slots 3 → 5  <sub>2026-03-28 · `202acacf`</sub>
- ⬆ **Upstream sync** — Synced with upstream MeshCore dev  <sub>2026-03-28 · `cc17c6b7`</sub>
- **New** — meshomatic preset; disable slots with no server configured  <sub>2026-03-27 · `3eacf56e`</sub>
- **Improvement** — Pre-allocated JSON publish buffer in PSRAM  <sub>2026-03-26 · `47dd2182`</sub>
- **New** — `reconnect()` added to PsychicMqttClient  <sub>2026-03-25 · `1b5884bd`</sub>
- **Improvement** — Lightweight JWT reconnect (token refresh without full teardown)  <sub>2026-03-25 · `af52a989`</sub>
- **Internal** — Full teardown/setup for JWT slots on reconnect  <sub>2026-03-24 · `e29c3d96`</sub>
- **Internal** — Standardized MQTT slot log format  <sub>2026-03-24 · `2bbca206`</sub>
- **New** — cascadiamesh preset; keepalive configuration  <sub>2026-03-24 · `eaf5fcbc`</sub>
- **Improvement** — Re-create JWT tokens when NTP reveals stale time  <sub>2026-03-24 · `1c98d1c6`</sub>
- **Internal** — Serial-availability checks before logging; circuit-breaker reconnect logic  <sub>2026-03-23 · `4a60f166`</sub>
- **New** — MeshRank preset (token auth, custom topic templates)  <sub>2026-03-22 · `95874f0c`</sub>
- **Fix** — Dynamic MQTTBridge allocation to avoid an ESP32 static-init crash  <sub>2026-03-21 · `c67fb12b`</sub>
- **Build** — Build script: cached project config, artifact handling, ESP32 binary merge  <sub>2026-03-21 · `bf8a3a7a`</sub>
- **Improvement** — Active connection slots limited by available memory (PSRAM-aware)  <sub>2026-03-20 · `542f8a31`</sub>
- **New** — Up to 3 configurable connection slots with built-in presets (LetsMesh US/EU, MeshMapper)  <sub>2026-03-20 · `b43e9618`</sub>
- ⬆ **Upstream sync** — Synced with upstream MeshCore dev  <sub>2026-03-20 · `abe6e046`</sub>
- **Change** — Stopped enabling MQTT analyzer servers by default in the example sketches  <sub>2026-03-07 · `304719e8`</sub>
- ⬆ **Upstream sync** — Synced with upstream MeshCore dev  <sub>2026-03-06 · `db8b4419`</sub>
- ⬆ **Upstream sync** — Synced with upstream MeshCore dev  <sub>2026-03-04 · `56ac85fa`</sub>
- ⬆ **Upstream sync** — Synced with upstream MeshCore dev  <sub>2026-03-03 · `dcbbe5a6`</sub>

### February 2026

- ⬆ **Upstream sync** — Synced with upstream MeshCore dev (Packet.cpp fix)  <sub>2026-02-26 · `8b359452`</sub>
- **Internal** — Multibyte path handling in Packet / MQTTMessageBuilder  <sub>2026-02-25 · `c8e220b1`</sub>
- ⬆ **Upstream sync** — Synced with upstream MeshCore dev  <sub>2026-02-25 · `51e9907f`</sub>
- **Change** — Removed an unused preprocessor directive in MyMesh  <sub>2026-02-25 · `ee1a9b4a`</sub>
- ⬆ **Upstream sync** — Synced with upstream MeshCore dev  <sub>2026-02-24 · `397a48fc`</sub>
- **Improvement** — Keepalive set to 45s to hold WebSocket connections through Cloudflare  <sub>2026-02-16 · `2d4b7a64`</sub>
- **Build** — heltec_v4 uses the `heltec_v4_oled` config for observer / room-server  <sub>2026-02-16 · `88ce0d33`</sub>
- ⬆ **Upstream sync** — Synced with upstream MeshCore dev  <sub>2026-02-15 · `e89cbee8`</sub>
- **Improvement** — Simplified KISS noise-floor sampling  <sub>2026-02-07 · `776131e2`</sub>
- **Fix** · `kiss_modem` — Improved RX delivery and noise-floor sampling  <sub>2026-02-06 · `f445b5ac`</sub>
- **Improvement** — Better NTP synchronization in MQTTBridge  <sub>2026-02-06 · `ba4243b5`</sub>
- **Improvement** — WiFi management and fragmentation recovery  <sub>2026-02-04 · `26aaace9`</sub>
- **New** — PSRAM-backed memory management in MQTTBridge  <sub>2026-02-04 · `1d0d33f9`</sub>
- **New** — PSRAM diagnostics at MQTTBridge init  <sub>2026-02-04 · `abcfe609`</sub>
- **Build** — mbedTLS configuration tuned across variants  <sub>2026-02-04 · `21158e1f`</sub>
- **Fix** · `kiss` — Periodic noise-floor calibration and AGC reset  <sub>2026-02-03 · `0fb57033`</sub>
- **Fix** — Memory use and status reporting in the MQTT bridge  <sub>2026-02-02 · `498566e6`</sub>

### January 2026

- **Internal** — Clarified packet ownership / memory management in MQTTBridge  <sub>2026-01-31 · `f91b715e`</sub>
- **New** — `recv_errors` in the `CMD_GET_STATS` packets response  <sub>2026-01-29 · `019bbf74`</sub>
- **New** — Receive-error tracking in Dispatcher and the message builder  <sub>2026-01-29 · `ee577314`</sub>
- **Fix** — Display header include for heltec_v4 when `DISPLAY_CLASS` is set directly  <sub>2026-01-27 · `bb27cded`</sub>
- ⬆ **Upstream sync** — Synced with upstream MeshCore dev  <sub>2026-01-27 · `d96c900f`</sub>
- ⬆ **Upstream sync** — Synced with upstream MeshCore dev  <sub>2026-01-26 · `2a84e58b`</sub>
- **Improvement** — MQTTBridge memory usage and NTP handling  <sub>2026-01-24 · `9e1ce23e`</sub>
- **Improvement** — MQTTBridge status handling and publish logic  <sub>2026-01-23 · `c67c3295`</sub>
- **Internal** — MQTTBridge connection handling for stability / responsiveness  <sub>2026-01-22 · `c055228c`</sub>
- ⬆ **Upstream sync** — Synced with upstream MeshCore dev  <sub>2026-01-21 · `39ba020f`</sub>
- **Internal** — MQTTBridge moved onto a FreeRTOS task for responsiveness  <sub>2026-01-21 · `ed27ebd0`</sub>
- **New** — Configurable WiFi power-save mode preference  <sub>2026-01-18 · `56f98997`</sub>
- **Internal** — HeltecTrackerV2 / HeltecV4 init for GC1109 FEM  <sub>2026-01-13 · `3eaca31f`</sub>
- **Docs** — Owner configuration commands documented  <sub>2026-01-12 · `58cdd60c`</sub>
- **Internal** — Advert timer type consistency in MyMesh / SensorMesh  <sub>2026-01-08 · `ffa2a1ec`</sub>
- **New** — Device metadata setup at bridge init / restart  <sub>2026-01-03 · `2bd61c13`</sub>
- **Internal** — Config validation allows optional username / password  <sub>2026-01-03 · `d1093b43`</sub>
- **Internal** — JWTHelper / MQTTBridge efficiency cleanup  <sub>2026-01-02 · `e754317c`</sub>
- **Fix** — MQTT connection stability and CLI responsiveness  <sub>2026-01-02 · `98f176c7`</sub>
- **New** — Neighbour-info sorting; MQTTBridge memory tuning  <sub>2026-01-01 · `2185523d`</sub>

### December 2025

- **Docs** — WiFi power-save configuration documented  <sub>2025-12-30 · `915f8b8e`</sub>
- **Internal** — `loadPrefs` handles fresh installs and upgrades; migrates old prefs  <sub>2025-12-30 · `8fa99611`</sub>
- **Improvement** — Loop prioritizes radio RX for responsiveness  <sub>2025-12-29 · `617e3b17`</sub>
- **Internal** — Defer broker connect until WiFi + NTP ready; socket error logging  <sub>2025-12-17 · `5d4f1e4c`</sub>
- **Improvement** — Event-driven WiFi diagnostics and faster reconnect  <sub>2025-12-12 · `f7f1f838`</sub>
- **New** — WiFi power-save mode configuration (none / min / max)  <sub>2025-12-08 · `34c8bea7`</sub>
- **Fix** — LPS22HB pressure now reported in hPa (was kPa)  <sub>2025-12-08 · `b91b854a`</sub>
- **New** — WiFi TX power configuration (default 11 dBm)  <sub>2025-12-07 · `b82d7e43`</sub>
- **New** — Default bridge settings for fresh installs; JSON reuse to avoid fragmentation  <sub>2025-12-07 · `0b7c0888`</sub>
- **Improvement** — MQTT debug macros check Serial to prevent hangs  <sub>2025-12-06 · `3589043b`</sub>
- **Change** — Removed redundant settings writes in `savePrefs`  <sub>2025-12-04 · `03d519f8`</sub>
- **Build** — Source filter for the Xiao S3 WIO variant  <sub>2025-12-01 · `4f548ffc`</sub>

### November 2025

- **New** — `adc_multiplier` added to NodePrefs  <sub>2025-11-23 · `8fbdc98a`</sub>
- **Change** — IATA codes uppercased for consistency  <sub>2025-11-23 · `5cadf3d9`</sub>
- **Change** — `CMD_GET_STATS` split into core / radio / packet sub-types  <sub>2025-11-17 · `a3c9a073`</sub>
- **New** — IP address shown for MQTT bridge devices in UITask  <sub>2025-11-16 · `6b0c0e93`</sub>
- **New** — T-Beam ESP32 support; error log when source = tx but TX logging is off  <sub>2025-11-16 · `3c7ac9d1`</sub>
- **Build** — `MQTT_DEBUG` enabled for several build configs  <sub>2025-11-16 · `3726c472`</sub>
- **New** — MQTT observer support for Room Servers  <sub>2025-11-15 · `0cd0c36a`</sub>
- **Change** — Switched MQTT configs to observer mode across variants  <sub>2025-11-13 · `304a32db`</sub>
- **Internal** — Client version string generation  <sub>2025-11-13 · `aa242e78`</sub>
- **Improvement** — WiFi power-save + reduced TX power to limit LoRa interference; retry fix when no brokers connected  <sub>2025-11-10 · `bafa5426`</sub>
- **Internal** — Statistics use binary frames instead of JSON  <sub>2025-11-09 · `80d6dd43`</sub>
- **New** — IATA validation blocks publishing without an IATA code  <sub>2025-11-08 · `25d40f02`</sub>
- **Fix** — Restored status-publish retry delay to prevent server spam  <sub>2025-11-08 · `9e06fffc`</sub>
- **New** — Added missing token-expiration / reconnection member variables  <sub>2025-11-08 · `453c4184`</sub>
- **Change** — Removed WiFi credentials from platformio.ini (use CLI / build flags)  <sub>2025-11-08 · `921c6112`</sub>
- **Internal** — MQTT preferences handling in CommonCLI  <sub>2025-11-08 · `5a816be8`</sub>
- **Internal** — `formatStatsReply` uses the new member variables  <sub>2025-11-08 · `c9aa536c`</sub>
- **Build** — Explicit `paulstoffregen/Time` dependency to fix PIO compile  <sub>2025-11-07 · `72ca50b9`</sub>
- **Build** — Dropped the CayenneLPP patch script; updated ArduinoJson  <sub>2025-11-07 · `fef9225b`</sub>
- **Fix** — Timezone command parsing lengths corrected  <sub>2025-11-07 · `b89ceeea`</sub>
- **New** — Statistics commands and response handling in MyMesh  <sub>2025-11-07 · `df4dab85`</sub>
- **New** — MQTT email support in CommonCLI / JWTHelper  <sub>2025-11-06 · `1ed18f9e`</sub>
- **New** — MQTT owner public-key support  <sub>2025-11-06 · `e5dbd56b`</sub>
- **Improvement** — Status reporting and stats collection  <sub>2025-11-01 · `e4ff06cb`</sub>

### October 2025

- **Improvement** — MQTTBridge logging and connection handling  <sub>2025-10-31 · `7df28557`</sub>
- **Improvement** — Packet handling and memory management in MQTTMessageBuilder / MQTTBridge  <sub>2025-10-31 · `eee02025`</sub>
- **New** — Memory monitoring and queue-size reporting  <sub>2025-10-29 · `e77beff0`</sub>
- **Internal** — In-place base64url encoding; cleanup frees queued packets / timezone objects  <sub>2025-10-27 · `1219668f`</sub>
- **Change** — Defer MQTT connect until WiFi credentials are valid (requires reboot)  <sub>2025-10-27 · `3851a84b`</sub>
- **Improvement** — Dynamic build date and client versioning  <sub>2025-10-26 · `c23663da`</sub>
- **New** — CLI config for `mqtt.server` / `username` / `password` / `port`  <sub>2025-10-26 · `eab70c3c`</sub>
- **New** — Let's Mesh Analyzer integration  <sub>2025-10-26 · `a6453556`</sub>
- **New** — Station G2 repeater bridge build target; docs  <sub>2025-10-26 · `f6a77d2f`</sub>
- **New** — Timezone handling; raw-data packet output with proper hashes / timestamps  <sub>2025-10-25 · `787af7b0`</sub>
- **New** — Timezone support in the MQTT Bridge and CLI  <sub>2025-10-25 · `26c4b970`</sub>
- **New** — Initial MQTT Bridge implementation with WiFi CLI configuration  <sub>2025-10-25 · `efc15eb6`</sub>

<!-- gen_changelog:hashes — auto-maintained list of every processed commit; do not edit by hand
985fda13 8bf590b1 e80a5ded 0844eee1 8fa38707 002e8d4a b45373a3 c29ede05
6723776f 981ed246 dcc8f826 c0c845f5 1296aa77 38568eb1 c41805d0 f717d673
f3c6c348 5a004f37 3cde9c10 daf9ee47 a8668840 a865d0a3 19f95001 6a3ed5d4
fc99d6b3 16dc49fa 80355f32 08847342 b9f478c9 303cf8c2 ff031f48 b37db668
78e1500a 9f8f2d05 0b05aede 457edf69 5a5e5373 753eb72f 32449e62 673361b6
8be27e4e bfcf0176 0a4f002f 7b21be3e 6211885c 0d280257 255fce7f c1989d6f
ee4e39bc 70722a58 bb67b04e 7d0c5bce cfec3a5b ac2662c5 d6712c69 e9ff1ae0
1ff9437f 563d9a7d 0ed29cb7 ab121708 074ac3e7 f4a4fd7c ab6685a1 13fbcd91
ff2df2b7 0c190dac 961f8293 7e4f75c9 74e45a79 3b46e9ee 1c524a93 47b632aa
258a0d72 f0962eff cf4ec3f6 0e82271e a4324a83 1263e71d da775bb2 384b80fc
ff49d28f 202acacf cc17c6b7 3eacf56e 47dd2182 1b5884bd af52a989 e29c3d96
2bbca206 eaf5fcbc 1c98d1c6 4a60f166 95874f0c c67fb12b bf8a3a7a 542f8a31
b43e9618 abe6e046 22eb9b87 2b637655 304719e8 db8b4419 56ac85fa dcbbe5a6
8b359452 c8e220b1 51e9907f ee1a9b4a 397a48fc 2d4b7a64 88ce0d33 e89cbee8
776131e2 f445b5ac ba4243b5 26aaace9 1d0d33f9 abcfe609 21158e1f 0fb57033
498566e6 f91b715e 019bbf74 ee577314 bb27cded d96c900f 066df192 2c99967c
2a84e58b 9e1ce23e c67c3295 c055228c 39ba020f ed27ebd0 56f98997 3eaca31f
58cdd60c ffa2a1ec 2bd61c13 d1093b43 e754317c 98f176c7 2185523d 915f8b8e
8fa99611 617e3b17 5d4f1e4c f7f1f838 34c8bea7 b82d7e43 0b7c0888 3589043b
03d519f8 4f548ffc 8fbdc98a 5cadf3d9 3726c472 6b0c0e93 3c7ac9d1 158748d4
44c24515 304a32db aa242e78 bafa5426 25d40f02 9e06fffc 3feb2cd5 ad68a29f
cf3a9c31 453c4184 b04a1a1d 0de7da84 921c6112 c5e2f18d 5a816be8 72ca50b9
fef9225b b89ceeea 1ed18f9e e5dbd56b e4ff06cb 7df28557 e77beff0 eee02025
2b047599 1219668f 3851a84b eab70c3c c23663da a6453556 f6a77d2f 787af7b0
26c4b970 efc15eb6 0cd0c36a bffbbb76 72811df6 a3875d87 dd9edbde a4aace61
09bc9cbc 4d7282c3 77169810 c0c56039 c36aac9a b15912fe 76a5caa0 a0e14f4b
2876ea6e ca55c9bf 97815834 fbc499e7 39850fa4 a8474c7c 8f34d966 8ca969be
9352db1e 08fb3046 ea134e43 46041130 8261633c bba6d896 58b73343 ed70ceb2
180c1d49 e9fe66c1 f39d2299 91cc4f50 ee6a7565 b91b854a a3c9a073 39f83efb
80d6dd43 c9aa536c df4dab85
-->
